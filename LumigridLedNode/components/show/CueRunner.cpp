// components/show/CueRunner.cpp
#include "CueRunner.hpp"
#include <algorithm>
#include <cstring>
#include <inttypes.h>
#include "esp_random.h"
#include "esp_log.h"

namespace lg {

static const char* TAG = "CueRunner";

CueRunner::CueRunner(ConfigStore& config, LightingRuntime& signage)
    : config_(config), signage_(signage) {}

// ── Transport lifecycle ──────────────────────────────────────────────────────

esp_err_t CueRunner::start(const CueList& list, uint8_t start_cue, uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (list.count == 0) {
        ESP_LOGW(TAG, "Cue list is empty");
        return ESP_ERR_INVALID_ARG;
    }

    // LG-034 §11: RUN copies the persisted list into the immutable active
    // snapshot. Later saves to the same slot never touch active_list_.
    active_list_ = list;
    active_list_index_ = 0;
    current_cue_index_ = (start_cue < list.count) ? start_cue : 0;
    step_direction_ = 1;
    manual_fade_q15_ = 0;
    remote_sync_active_ = false;

    if (active_list_.mode == CueRunMode::Manual) {
        state_ = CueRunnerState::Manual;
        next_cue_index_ = compute_next_index(current_cue_index_);
        transport_generation_++;
        signage_.activate_scene_custom(active_list_.cues[current_cue_index_].scene_index, 0, TransitionCurve::Linear, &last_applied_generation_);
        signage_.prepare_manual_transition(active_list_.cues[next_cue_index_].scene_index);
        signage_.set_manual_transition_progress(0);
        emit_event_locked(Event::CueChanged);
        ESP_LOGI(TAG, "Started cue list in Manual mode, initial cue %u, next %u", current_cue_index_, next_cue_index_);
        return ESP_OK;
    }

    trigger_cue_locked(current_cue_index_, current_ms);
    ESP_LOGI(TAG, "Started cue list (\"%s\"), mode=%s, count=%u",
             active_list_.name, cue_run_mode_name(active_list_.mode), active_list_.count);
    return ESP_OK;
}

esp_err_t CueRunner::start(uint8_t list_index, uint8_t start_cue, uint64_t current_ms) {
    if (list_index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    CueList list;
    esp_err_t err = config_.load_cue_list(list_index, list);
    if (err != ESP_OK || list.count == 0) {
        ESP_LOGW(TAG, "Failed to load cue list %u: %s", list_index, esp_err_to_name(err));
        return (err == ESP_OK) ? ESP_ERR_INVALID_ARG : err;
    }

    {
        // start(list,...) re-locks; hold only while updating the slot index.
        std::lock_guard<std::mutex> lock(mutex_);
        active_list_index_ = list_index;
    }
    esp_err_t start_err = start(list, start_cue, current_ms);
    if (start_err == ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_list_index_ = list_index;
    }
    return start_err;
}

esp_err_t CueRunner::restart(uint8_t list_index, uint8_t start_cue, uint64_t current_ms) {
    // LG-034 §13: load the newest saved list, build a fresh active snapshot,
    // bump the transport generation, begin at the requested cue. No stop
    // transient, no end action from the superseded run.
    if (list_index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    CueList list;
    esp_err_t err = config_.load_cue_list(list_index, list);
    if (err != ESP_OK || list.count == 0) {
        return (err == ESP_OK) ? ESP_ERR_INVALID_ARG : err;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    active_list_ = list;
    active_list_index_ = list_index;
    current_cue_index_ = (start_cue < list.count) ? start_cue : 0;
    step_direction_ = 1;
    manual_fade_q15_ = 0;
    remote_sync_active_ = false;

    if (active_list_.mode == CueRunMode::Manual) {
        state_ = CueRunnerState::Manual;
        next_cue_index_ = compute_next_index(current_cue_index_);
        transport_generation_++;
        signage_.activate_scene_custom(active_list_.cues[current_cue_index_].scene_index, 0, TransitionCurve::Linear, &last_applied_generation_);
        signage_.prepare_manual_transition(active_list_.cues[next_cue_index_].scene_index);
        signage_.set_manual_transition_progress(0);
        emit_event_locked(Event::CueChanged);
        return ESP_OK;
    }

    trigger_cue_locked(current_cue_index_, current_ms);
    ESP_LOGI(TAG, "Restarted cue list %u (\"%s\") at cue %u", list_index, active_list_.name, current_cue_index_);
    return ESP_OK;
}

esp_err_t CueRunner::stop(CueStopReason reason, uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) return ESP_OK;
    stop_locked(reason, current_ms);
    return ESP_OK;
}

void CueRunner::stop_locked(CueStopReason reason, uint64_t current_ms) {
    (void)current_ms;
    state_ = CueRunnerState::Stopped;
    paused_from_ = CueRunnerState::Stopped;
    remote_sync_active_ = false;
    transport_generation_++;
    emit_event_locked(Event::Stopped);

    // LG-034 §2/§3: the configured end action applies ONLY on natural
    // completion of a Single Shot. Operator stop, preemption and sync loss
    // simply freeze/hold the current output for the next owner.
    if (reason != CueStopReason::NaturalCompletion) {
        ESP_LOGI(TAG, "CueRunner stopped (%s), output held", cue_stop_reason_name(reason));
        return;
    }

    switch (active_list_.end_action) {
        case CueEndAction::Blackout: {
            SceneConfig bo{};
            strncpy(bo.name, "Blackout", sizeof(bo.name) - 1);
            signage_.activate_scene_direct(bo, true);
            ESP_LOGI(TAG, "CueRunner natural completion: blackout");
            break;
        }
        case CueEndAction::RestoreFallback: {
            DeviceConfig cfg = signage_.get_config_snapshot();
            signage_.activate_scene_checked(cfg.fallback_preset_index, &last_applied_generation_);
            ESP_LOGI(TAG, "CueRunner natural completion: restore fallback scene");
            break;
        }
        case CueEndAction::HoldLast:
        default:
            ESP_LOGI(TAG, "CueRunner natural completion: hold last scene");
            break;
    }
}

esp_err_t CueRunner::pause(uint64_t current_ms) {
    // LG-034 §5: capture phase elapsed/remaining BEFORE changing state.
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state_ == CueRunnerState::Paused) {
        return ESP_OK; // idempotent
    }
    if (state_ == CueRunnerState::Manual) {
        return ESP_ERR_INVALID_STATE; // Manual mode has no timed phases
    }

    const uint32_t elapsed = cue_elapsed_ms_unsafe(current_ms);
    const uint32_t remaining = cue_remaining_ms_unsafe(current_ms);

    paused_from_ = state_;
    paused_elapsed_ms_ = elapsed;
    paused_remaining_ms_ = remaining;
    state_ = CueRunnerState::Paused;

    // §6: physically freeze any in-flight fade so output stops progressing.
    if (paused_from_ == CueRunnerState::Fading) {
        signage_.freeze_transition();
    }

    transport_generation_++;
    emit_event_locked(Event::Paused);
    ESP_LOGI(TAG, "CueRunner paused from %s (elapsed %" PRIu32 " ms, remaining %" PRIu32 " ms)",
             cue_runner_state_name(paused_from_), elapsed, remaining);
    return ESP_OK;
}

esp_err_t CueRunner::resume(uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != CueRunnerState::Paused) {
        return ESP_ERR_INVALID_STATE;
    }

    state_ = paused_from_;
    // Keep the original phase duration and cumulative elapsed time. Rebase the
    // anchor to exclude the pause, including after repeated pause/resume cycles.
    cue_start_ms_ = (current_ms >= paused_elapsed_ms_)
        ? current_ms - paused_elapsed_ms_ : 0;
    if (state_ == CueRunnerState::Fading) {
        // §6: continue toward the existing target over the remaining duration,
        // starting from the frozen visual state (not the original source).
        signage_.resume_frozen_transition(paused_remaining_ms_,
                                          static_cast<uint32_t>(current_ms));
    }
    paused_from_ = CueRunnerState::Stopped;

    transport_generation_++;
    emit_event_locked(Event::Resumed);
    ESP_LOGI(TAG, "CueRunner resumed into %s with %" PRIu32 " ms remaining",
             cue_runner_state_name(state_), paused_remaining_ms_);
    return ESP_OK;
}

esp_err_t CueRunner::next(uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) return ESP_ERR_INVALID_STATE;

    if (state_ == CueRunnerState::Manual) {
        manual_advance_locked(next_cue_index_, current_ms);
        return ESP_OK;
    }
    if (state_ == CueRunnerState::Paused) return ESP_ERR_INVALID_STATE;

    const uint8_t next_idx = compute_next_index(current_cue_index_);
    trigger_cue_locked(next_idx, current_ms);
    return ESP_OK;
}

esp_err_t CueRunner::previous(uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) return ESP_ERR_INVALID_STATE;

    if (state_ == CueRunnerState::Manual) {
        manual_advance_locked(compute_prev_index(current_cue_index_), current_ms);
        return ESP_OK;
    }
    if (state_ == CueRunnerState::Paused) return ESP_ERR_INVALID_STATE;

    const uint8_t prev_idx = compute_prev_index(current_cue_index_);
    trigger_cue_locked(prev_idx, current_ms);
    return ESP_OK;
}

esp_err_t CueRunner::jump(uint8_t cue_index, uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) return ESP_ERR_INVALID_STATE;
    if (state_ == CueRunnerState::Paused) return ESP_ERR_INVALID_STATE;
    if (cue_index >= active_list_.count) return ESP_ERR_INVALID_ARG;

    if (state_ == CueRunnerState::Manual) {
        manual_advance_locked(cue_index, current_ms);
        return ESP_OK;
    }

    trigger_cue_locked(cue_index, current_ms);
    return ESP_OK;
}

esp_err_t CueRunner::set_manual_fade(uint16_t fade_q15) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != CueRunnerState::Manual) {
        // §17/§16: crossfade is only operational in Manual mode.
        return ESP_ERR_INVALID_STATE;
    }
    manual_fade_q15_ = std::min<uint16_t>(32768, fade_q15);
    signage_.set_manual_transition_progress(manual_fade_q15_);
    if (manual_fade_q15_ >= 32768) {
        // Reaching 100% advances to the next pair (single generation bump;
        // intermediate T-Bar movement never changes generation, §26/§61).
        const uint8_t advanced = next_cue_index_;
        manual_advance_locked(advanced, 0);
        emit_event_locked(Event::ManualCueCompleted);
    }
    return ESP_OK;
}

// ── State machine ────────────────────────────────────────────────────────────

void CueRunner::update(uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped ||
        state_ == CueRunnerState::Paused ||
        state_ == CueRunnerState::Manual) {
        return;
    }

    if (remote_sync_active_) {
        // While remote-driven by sync leader, timing and cues are derived from
        // leader snapshots. Follower never computes next cue or auto-advances.
        if (state_ == CueRunnerState::Fading) {
            const uint64_t elapsed = (current_ms >= cue_start_ms_) ? (current_ms - cue_start_ms_) : 0;
            if (elapsed >= fade_duration_ms_ || !signage_.is_transitioning()) {
                state_ = CueRunnerState::Holding;
                emit_event_locked(Event::PhaseChanged);
            }
        }
        return;
    }

    if (state_ == CueRunnerState::Fading) {
        const uint64_t elapsed = current_ms - cue_start_ms_;
        if (elapsed >= fade_duration_ms_ || !signage_.is_transitioning()) {
            state_ = CueRunnerState::Holding;
            cue_start_ms_ = current_ms;
            emit_event_locked(Event::PhaseChanged);
        }
    } else if (state_ == CueRunnerState::Holding) {
        const uint64_t elapsed = current_ms - cue_start_ms_;
        if (elapsed >= hold_duration_ms_) {
            // §1: Single Shot reaching its final hold must NOT re-enter the
            // lock via stop(); stop_locked() runs under the already-held lock.
            if (active_list_.mode == CueRunMode::SingleShot && current_cue_index_ + 1 >= active_list_.count) {
                stop_locked(CueStopReason::NaturalCompletion, current_ms);
                return;
            }
            trigger_cue_locked(next_cue_index_, current_ms);
        }
    }
}

// ── Internal helpers (lock-aware) ────────────────────────────────────────────

void CueRunner::trigger_cue_locked(uint8_t cue_idx, uint64_t current_ms) {
    if (cue_idx >= active_list_.count) return;
    current_cue_index_ = cue_idx;
    next_cue_index_ = compute_next_index(cue_idx);
    transport_generation_++;

    const CueEntry& cue = active_list_.cues[cue_idx];
    cue_start_ms_ = current_ms;
    fade_duration_ms_ = cue.fade_ms;
    hold_duration_ms_ = cue.hold_ms;
    paused_elapsed_ms_ = 0;

    if (fade_duration_ms_ > 0) {
        state_ = CueRunnerState::Fading;
        signage_.activate_scene_custom(cue.scene_index, cue.fade_ms, cue.curve, &last_applied_generation_);
    } else {
        state_ = CueRunnerState::Holding;
        signage_.activate_scene_custom(cue.scene_index, 0, cue.curve, &last_applied_generation_);
    }
}

void CueRunner::manual_advance_locked(uint8_t new_current, uint64_t current_ms) {
    (void)current_ms;
    if (new_current >= active_list_.count) new_current = 0;
    current_cue_index_ = new_current;
    next_cue_index_ = compute_next_index(current_cue_index_);
    manual_fade_q15_ = 0;
    transport_generation_++;
    signage_.activate_scene_custom(active_list_.cues[current_cue_index_].scene_index, 0, TransitionCurve::Linear, &last_applied_generation_);
    signage_.prepare_manual_transition(active_list_.cues[next_cue_index_].scene_index);
    signage_.set_manual_transition_progress(0);
}

void CueRunner::emit_event_locked(Event event) {
    if (event_callback_) {
        event_callback_(event);
    }
}

uint8_t CueRunner::compute_next_index(uint8_t from_idx) const {
    if (active_list_.count == 0) return 0;
    if (active_list_.count == 1) return 0;

    switch (active_list_.mode) {
        case CueRunMode::SingleShot:
            return std::min<uint8_t>(active_list_.count - 1, from_idx + 1);
        case CueRunMode::Loop:
            return (from_idx + 1) % active_list_.count;
        case CueRunMode::PingPong:
            if (step_direction_ > 0) {
                if (from_idx + 1 >= active_list_.count) {
                    step_direction_ = -1;
                    return (from_idx > 0) ? from_idx - 1 : 0;
                }
                return from_idx + 1;
            } else {
                if (from_idx == 0) {
                    step_direction_ = 1;
                    return (active_list_.count > 1) ? 1 : 0;
                }
                return from_idx - 1;
            }
        case CueRunMode::Random:
            return (from_idx + 1 + (esp_random() % (active_list_.count - 1))) % active_list_.count;
        case CueRunMode::Manual:
            return (from_idx + 1) % active_list_.count;
    }
    return 0;
}

uint8_t CueRunner::compute_prev_index(uint8_t from_idx) const {
    if (active_list_.count == 0) return 0;
    if (active_list_.count == 1) return 0;
    return (from_idx == 0) ? active_list_.count - 1 : from_idx - 1;
}

// ── Timing helpers (must be called with mutex_ held or during pause capture) ─

uint32_t CueRunner::cue_elapsed_ms_unsafe(uint64_t current_ms) const {
    if (state_ == CueRunnerState::Stopped) return 0;
    if (state_ == CueRunnerState::Paused) return paused_elapsed_ms_;
    if (current_ms < cue_start_ms_) return 0;
    return static_cast<uint32_t>(current_ms - cue_start_ms_);
}

uint32_t CueRunner::cue_remaining_ms_unsafe(uint64_t current_ms) const {
    if (state_ == CueRunnerState::Stopped) return 0;
    if (state_ == CueRunnerState::Paused) return paused_remaining_ms_;
    const uint32_t dur = (state_ == CueRunnerState::Fading) ? fade_duration_ms_ : hold_duration_ms_;
    const uint32_t elapsed = cue_elapsed_ms_unsafe(current_ms);
    return (elapsed >= dur) ? 0 : (dur - elapsed);
}

// ── Observation ──────────────────────────────────────────────────────────────

CueRunnerSnapshot CueRunner::snapshot(uint64_t current_ms) const {
    // Lock once, copy everything, unlock. Consumers own the copy (§9).
    std::lock_guard<std::mutex> lock(mutex_);

    CueRunnerSnapshot snap{};
    snap.running = (state_ != CueRunnerState::Stopped);
    snap.state = state_;
    snap.paused_from = (state_ == CueRunnerState::Paused) ? paused_from_ : CueRunnerState::Stopped;
    snap.list_index = active_list_index_;
    snap.mode = active_list_.mode;
    snap.current_cue = current_cue_index_;
    snap.next_cue = next_cue_index_;
    snap.current_scene = (current_cue_index_ < active_list_.count)
        ? active_list_.cues[current_cue_index_].scene_index : 0;
    snap.next_scene = (next_cue_index_ < active_list_.count)
        ? active_list_.cues[next_cue_index_].scene_index : 0;
    snap.elapsed_ms = cue_elapsed_ms_unsafe(current_ms);
    snap.remaining_ms = cue_remaining_ms_unsafe(current_ms);
    snap.manual_fade_q15 = manual_fade_q15_;
    snap.transition_progress_q15 = signage_.transition_progress_q15();
    snap.generation = transport_generation_;

    // §11/§12: active (running) vs saved (persisted) content checksums.
    snap.active_checksum = active_list_.checksum;
    if (active_list_index_ < kMaxCueLists) {
        CueList saved{};
        if (config_.load_cue_list(active_list_index_, saved) == ESP_OK) {
            snap.saved_checksum = saved.checksum;
        }
    }
    snap.content_stale = snap.running &&
        snap.saved_checksum != 0 &&
        snap.active_checksum != snap.saved_checksum;
    snap.remote_driven = remote_sync_active_;
    return snap;
}

CueSyncSnapshot CueRunner::sync_snapshot(uint64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    CueSyncSnapshot snap{};
    snap.active = (state_ != CueRunnerState::Stopped);
    snap.state = state_;
    snap.paused_from = (state_ == CueRunnerState::Paused) ? paused_from_ : CueRunnerState::Stopped;
    snap.list_index = active_list_index_;
    snap.current_cue = current_cue_index_;
    snap.next_cue = next_cue_index_;
    snap.cue_anchor_ms = cue_start_ms_;
    snap.paused_elapsed_ms = (state_ == CueRunnerState::Paused) ? paused_elapsed_ms_ : cue_elapsed_ms_unsafe(now_ms);
    snap.manual_fade_q15 = manual_fade_q15_;
    snap.generation = transport_generation_;

    // LG-034 §27/§28: v6 fields for follower frozen-fade reconstruction.
    snap.list_checksum = active_list_.checksum;
    snap.transition_progress_q15 = signage_.transition_progress_q15();
    snap.source_scene = (current_cue_index_ < active_list_.count)
        ? active_list_.cues[current_cue_index_].scene_index : 0;
    return snap;
}

esp_err_t CueRunner::apply_remote_sync(const CueSyncSnapshot& snapshot, uint64_t current_ms,
                                     CueSyncError* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error) *error = CueSyncError::None;
    const auto reject = [error](CueSyncError detail, esp_err_t result) {
        if (error) *error = detail;
        return result;
    };
    if (!snapshot.active || snapshot.state == CueRunnerState::Stopped) {
        if (state_ != CueRunnerState::Stopped) {
            state_ = CueRunnerState::Stopped;
            transport_generation_ = snapshot.generation;
            emit_event_locked(Event::Stopped);
        }
        remote_sync_active_ = false;
        return ESP_OK;
    }

    if (snapshot.state != CueRunnerState::Holding &&
        snapshot.state != CueRunnerState::Fading &&
        snapshot.state != CueRunnerState::Paused &&
        snapshot.state != CueRunnerState::Manual) {
        return reject(CueSyncError::StateInvalid, ESP_ERR_INVALID_ARG);
    }
    if (snapshot.list_index >= kMaxCueLists) {
        return reject(CueSyncError::ListMissing, ESP_ERR_INVALID_ARG);
    }

    // Admit the exact content that will run before changing either ownership or
    // the immutable active snapshot. A saved edit may differ from a still-valid
    // active copy; a new checksum in the same slot must load the matching edit.
    CueList loaded{};
    const bool replace_active = active_list_index_ != snapshot.list_index ||
        active_list_.count == 0 ||
        (snapshot.list_checksum != 0 && active_list_.checksum != snapshot.list_checksum);
    const CueList* candidate = &active_list_;
    if (replace_active) {
        const esp_err_t err = config_.load_cue_list(snapshot.list_index, loaded);
        if (err != ESP_OK || loaded.count == 0) {
            ESP_LOGW(TAG, "Sync failed: follower missing cue list %u", snapshot.list_index);
            return reject(CueSyncError::ListMissing,
                          (err == ESP_OK) ? ESP_ERR_NOT_FOUND : err);
        }
        candidate = &loaded;
    }
    if (candidate->count > kMaxCuesPerList) {
        return reject(CueSyncError::ListMissing, ESP_ERR_INVALID_ARG);
    }
    if (snapshot.list_checksum != 0 && candidate->checksum != snapshot.list_checksum) {
        return reject(CueSyncError::ListMismatch, ESP_ERR_INVALID_CRC);
    }

    if (snapshot.current_cue >= candidate->count || snapshot.next_cue >= candidate->count) {
        return reject(CueSyncError::CueInvalid, ESP_ERR_INVALID_ARG);
    }

    const CueEntry& candidate_cue = candidate->cues[snapshot.current_cue];
    const uint8_t scene_idx = candidate_cue.scene_index;
    if (!config_.scene_exists(scene_idx)) {
        ESP_LOGW(TAG, "Sync failed: follower missing scene %u for cue %u", scene_idx, snapshot.current_cue);
        return reject(CueSyncError::SceneMissing, ESP_ERR_NOT_FOUND);
    }
    const bool needs_next_scene = snapshot.state == CueRunnerState::Manual ||
        (snapshot.state == CueRunnerState::Paused && candidate_cue.fade_ms > 0 &&
         (snapshot.paused_from == CueRunnerState::Fading ||
          snapshot.paused_from == CueRunnerState::Stopped));
    if (needs_next_scene && !config_.scene_exists(candidate->cues[snapshot.next_cue].scene_index)) {
        return reject(CueSyncError::SceneMissing, ESP_ERR_NOT_FOUND);
    }

    const bool cue_changed = !remote_sync_active_ || replace_active ||
        current_cue_index_ != snapshot.current_cue ||
        transport_generation_ != snapshot.generation ||
        state_ == CueRunnerState::Stopped || state_ == CueRunnerState::Paused;
    if (replace_active) active_list_ = loaded;
    active_list_index_ = snapshot.list_index;
    remote_sync_active_ = true;
    remote_generation_ = snapshot.generation;

    if (snapshot.state == CueRunnerState::Manual) {
        state_ = CueRunnerState::Manual;
        current_cue_index_ = snapshot.current_cue;
        next_cue_index_ = snapshot.next_cue;
        manual_fade_q15_ = snapshot.manual_fade_q15;
        transport_generation_ = snapshot.generation;

        signage_.activate_scene_custom(active_list_.cues[current_cue_index_].scene_index, 0, TransitionCurve::Linear, &last_applied_generation_);
        if (next_cue_index_ < active_list_.count) {
            signage_.prepare_manual_transition(active_list_.cues[next_cue_index_].scene_index);
            signage_.set_manual_transition_progress(manual_fade_q15_);
        }
        return ESP_OK;
    }

    if (snapshot.state == CueRunnerState::Paused) {
        // LG-034 §31: reconstruct the paused visual exactly, including a frozen
        // in-flight fade. The transition must not continue locally.
        current_cue_index_ = snapshot.current_cue;
        next_cue_index_ = snapshot.next_cue;
        transport_generation_ = snapshot.generation;

        const CueEntry& cue = active_list_.cues[snapshot.current_cue];
        fade_duration_ms_ = cue.fade_ms;
        hold_duration_ms_ = cue.hold_ms;

        const bool was_fading = (snapshot.paused_from == CueRunnerState::Fading) ||
                                (snapshot.paused_from == CueRunnerState::Stopped && cue.fade_ms > 0);
        if (was_fading && cue.fade_ms > 0) {
            state_ = CueRunnerState::Paused;
            paused_from_ = CueRunnerState::Fading;
            paused_elapsed_ms_ = snapshot.paused_elapsed_ms;
            const uint32_t remaining = (snapshot.paused_elapsed_ms >= cue.fade_ms)
                ? 0 : (cue.fade_ms - snapshot.paused_elapsed_ms);
            paused_remaining_ms_ = remaining;

            signage_.activate_scene_custom(cue.scene_index, 0, cue.curve, &last_applied_generation_);
            if (snapshot.next_cue < active_list_.count) {
                signage_.prepare_manual_transition(active_list_.cues[snapshot.next_cue].scene_index);
                signage_.set_manual_transition_progress(0);
            }
            // §30/§31: use the leader's authoritative transition progress when
            // provided (v6), else derive it from elapsed/fade duration.
            uint16_t progress = snapshot.transition_progress_q15;
            if (progress == 0 && cue.fade_ms > 0) {
                progress = static_cast<uint16_t>(std::min<uint32_t>(32768,
                    (static_cast<uint64_t>(snapshot.paused_elapsed_ms) * 32768ULL) / cue.fade_ms));
            }
            signage_.set_manual_transition_progress(std::min<uint16_t>(32768, progress));
            signage_.freeze_transition();
        } else {
            state_ = CueRunnerState::Paused;
            paused_from_ = CueRunnerState::Holding;
            paused_elapsed_ms_ = snapshot.paused_elapsed_ms;
            const uint32_t remaining = (snapshot.paused_elapsed_ms >= cue.hold_ms)
                ? 0 : (cue.hold_ms - snapshot.paused_elapsed_ms);
            paused_remaining_ms_ = remaining;
            signage_.activate_scene_custom(cue.scene_index, 0, cue.curve, &last_applied_generation_);
        }
        emit_event_locked(Event::Paused);
        return ESP_OK;
    }

    // Holding or Fading state
    current_cue_index_ = snapshot.current_cue;
    next_cue_index_ = snapshot.next_cue;
    state_ = snapshot.state;
    cue_start_ms_ = snapshot.cue_anchor_ms;
    transport_generation_ = snapshot.generation;

    const CueEntry& cue = active_list_.cues[snapshot.current_cue];
    fade_duration_ms_ = cue.fade_ms;
    hold_duration_ms_ = cue.hold_ms;

    if (cue_changed) {
        if (snapshot.state == CueRunnerState::Fading && cue.fade_ms > 0) {
            // Phase 5.3: Remote fade catch-up
            uint32_t adjusted_elapsed = snapshot.paused_elapsed_ms;
            if (snapshot.cue_anchor_ms > 0 && current_ms > snapshot.cue_anchor_ms) {
                adjusted_elapsed += static_cast<uint32_t>(current_ms - snapshot.cue_anchor_ms);
            }
            uint32_t remaining = (cue.fade_ms > adjusted_elapsed) ? (cue.fade_ms - adjusted_elapsed) : 0;
            signage_.activate_scene_custom(cue.scene_index, remaining, cue.curve, &last_applied_generation_);
        } else {
            // Phase 5.4: Holding state: ensure correct Scene is active with 0 duration
            signage_.activate_scene_custom(cue.scene_index, 0, cue.curve, &last_applied_generation_);
        }
        emit_event_locked(Event::CueChanged);
    }

    return ESP_OK;
}

void CueRunner::clear_remote_sync(uint64_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_sync_active_ = false;
    remote_generation_ = 0;
    (void)current_ms;
}

} // namespace lg
