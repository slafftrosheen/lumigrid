// components/show/CueRunner.cpp
#include "CueRunner.hpp"
#include <algorithm>
#include <cstring>
#include "esp_random.h"
#include "esp_log.h"

namespace lg {

static const char* TAG = "CueRunner";

CueRunner::CueRunner(ConfigStore& config, LightingRuntime& signage)
    : config_(config), signage_(signage) {}

esp_err_t CueRunner::start(const CueList& list, uint8_t start_cue, uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (list.count == 0) {
        ESP_LOGW(TAG, "Cue list is empty");
        return ESP_ERR_INVALID_STATE;
    }

    list_ = list;
    current_cue_index_ = (start_cue < list.count) ? start_cue : 0;
    step_direction_ = 1;
    manual_fade_q15_ = 0;

    if (list_.mode == CueRunMode::Manual) {
        state_ = CueRunnerState::Manual;
        next_cue_index_ = compute_next_index(current_cue_index_);
        signage_.activate_scene_custom(list_.cues[current_cue_index_].scene_index, 0, TransitionCurve::Linear, &last_applied_generation_);
        signage_.prepare_manual_transition(list_.cues[next_cue_index_].scene_index);
        signage_.set_manual_transition_progress(0);
        ESP_LOGI(TAG, "Started cue list in Manual mode, initial cue %u, next %u", current_cue_index_, next_cue_index_);
        return ESP_OK;
    }

    trigger_cue(current_cue_index_, current_ms);
    ESP_LOGI(TAG, "Started cue list (\"%s\"), mode=%s, count=%u",
             list_.name, cue_run_mode_name(list_.mode), list_.count);
    return ESP_OK;
}

esp_err_t CueRunner::start(uint8_t list_index, uint8_t start_cue, uint32_t current_ms) {
    if (list_index >= kMaxCueLists) return ESP_ERR_INVALID_ARG;

    CueList list;
    esp_err_t err = config_.load_cue_list(list_index, list);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load cue list %u: %s", list_index, esp_err_to_name(err));
        return err;
    }
    active_list_index_ = list_index;
    return start(list, start_cue, current_ms);
}

void CueRunner::stop(uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped) return;

    state_ = CueRunnerState::Stopped;
    if (list_.end_action == 1) {
        // Blackout
        SceneConfig bo{};
        strncpy(bo.name, "Blackout", sizeof(bo.name) - 1);
        signage_.activate_scene_direct(bo, true);
    } else if (list_.end_action == 2) {
        // Restore fallback preset
        DeviceConfig cfg = signage_.get_config_snapshot();
        signage_.activate_scene_checked(cfg.fallback_preset_index, &last_applied_generation_);
    }
    ESP_LOGI(TAG, "CueRunner stopped");
}

void CueRunner::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Holding || state_ == CueRunnerState::Fading) {
        paused_state_ = state_;
        state_ = CueRunnerState::Paused;
        uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        paused_remaining_ms_ = cue_remaining_ms(now);
        ESP_LOGI(TAG, "CueRunner paused");
    }
}

void CueRunner::resume(uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Paused) {
        state_ = paused_state_;
        cue_start_ms_ = current_ms;
        if (state_ == CueRunnerState::Holding) {
            hold_duration_ms_ = paused_remaining_ms_;
        } else if (state_ == CueRunnerState::Fading) {
            fade_duration_ms_ = paused_remaining_ms_;
        }
        ESP_LOGI(TAG, "CueRunner resumed");
    }
}

void CueRunner::next(uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (list_.count == 0) return;
    uint8_t n = compute_next_index(current_cue_index_);
    if (list_.mode == CueRunMode::SingleShot && current_cue_index_ + 1 >= list_.count) {
        stop(current_ms);
        return;
    }
    trigger_cue(n, current_ms);
}

void CueRunner::previous(uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (list_.count == 0) return;
    uint8_t p = compute_prev_index(current_cue_index_);
    trigger_cue(p, current_ms);
}

void CueRunner::jump(uint8_t cue_index, uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cue_index >= list_.count) return;
    trigger_cue(cue_index, current_ms);
}

void CueRunner::set_manual_fade(uint16_t fade_q15) {
    std::lock_guard<std::mutex> lock(mutex_);
    manual_fade_q15_ = std::min<uint16_t>(32768, fade_q15);
    if (state_ == CueRunnerState::Manual) {
        signage_.set_manual_transition_progress(manual_fade_q15_);
        if (manual_fade_q15_ >= 32768) {
            // Reached target cue, advance
            current_cue_index_ = next_cue_index_;
            next_cue_index_ = compute_next_index(current_cue_index_);
            manual_fade_q15_ = 0;
            signage_.prepare_manual_transition(list_.cues[next_cue_index_].scene_index);
            signage_.set_manual_transition_progress(0);
        }
    }
}

void CueRunner::trigger_cue(uint8_t cue_idx, uint32_t current_ms) {
    if (cue_idx >= list_.count) return;
    current_cue_index_ = cue_idx;
    next_cue_index_ = compute_next_index(cue_idx);

    const auto& cue = list_.cues[cue_idx];
    cue_start_ms_ = current_ms;
    fade_duration_ms_ = cue.fade_ms;
    hold_duration_ms_ = cue.hold_ms;

    if (fade_duration_ms_ > 0) {
        state_ = CueRunnerState::Fading;
        signage_.activate_scene_custom(cue.scene_index, cue.fade_ms, cue.curve, &last_applied_generation_);
    } else {
        state_ = CueRunnerState::Holding;
        signage_.activate_scene_custom(cue.scene_index, 0, cue.curve, &last_applied_generation_);
    }
}

uint8_t CueRunner::compute_next_index(uint8_t from_idx) const {
    if (list_.count == 0) return 0;
    if (list_.count == 1) return 0;

    switch (list_.mode) {
        case CueRunMode::SingleShot:
            return std::min<uint8_t>(list_.count - 1, from_idx + 1);
        case CueRunMode::Loop:
            return (from_idx + 1) % list_.count;
        case CueRunMode::PingPong:
            if (step_direction_ > 0) {
                if (from_idx + 1 >= list_.count) {
                    step_direction_ = -1;
                    return (from_idx > 0) ? from_idx - 1 : 0;
                }
                return from_idx + 1;
            } else {
                if (from_idx == 0) {
                    step_direction_ = 1;
                    return (list_.count > 1) ? 1 : 0;
                }
                return from_idx - 1;
            }
        case CueRunMode::Random:
            return (from_idx + 1 + (esp_random() % (list_.count - 1))) % list_.count;
        case CueRunMode::Manual:
            return (from_idx + 1) % list_.count;
    }
    return 0;
}

uint8_t CueRunner::compute_prev_index(uint8_t from_idx) const {
    if (list_.count == 0) return 0;
    if (list_.count == 1) return 0;
    return (from_idx == 0) ? list_.count - 1 : from_idx - 1;
}

void CueRunner::update(uint32_t current_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == CueRunnerState::Stopped || state_ == CueRunnerState::Paused || state_ == CueRunnerState::Manual) {
        return;
    }

    if (state_ == CueRunnerState::Fading) {
        const uint32_t elapsed = current_ms - cue_start_ms_;
        if (elapsed >= fade_duration_ms_ || !signage_.is_transitioning()) {
            state_ = CueRunnerState::Holding;
            cue_start_ms_ = current_ms;
        }
        return;
    }

    if (state_ == CueRunnerState::Holding) {
        const uint32_t elapsed = current_ms - cue_start_ms_;
        if (elapsed >= hold_duration_ms_) {
            if (list_.mode == CueRunMode::SingleShot && current_cue_index_ + 1 >= list_.count) {
                // End of single shot
                stop(current_ms);
                return;
            }
            uint8_t next_idx = compute_next_index(current_cue_index_);
            trigger_cue(next_idx, current_ms);
        }
    }
}

uint32_t CueRunner::cue_elapsed_ms(uint32_t current_ms) const {
    if (state_ == CueRunnerState::Stopped) return 0;
    if (current_ms <= cue_start_ms_) return 0;
    return current_ms - cue_start_ms_;
}

uint32_t CueRunner::cue_remaining_ms(uint32_t current_ms) const {
    if (state_ == CueRunnerState::Stopped) return 0;
    if (state_ == CueRunnerState::Paused) return paused_remaining_ms_;
    uint32_t dur = (state_ == CueRunnerState::Fading) ? fade_duration_ms_ : hold_duration_ms_;
    uint32_t elapsed = cue_elapsed_ms(current_ms);
    return (elapsed >= dur) ? 0 : (dur - elapsed);
}

} // namespace lg
