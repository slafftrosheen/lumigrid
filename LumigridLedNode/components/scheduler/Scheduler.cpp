#include "Scheduler.hpp"
#include "TimelineEvaluator.hpp"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <ctime>
#include <algorithm>

static const char* TAG = "scheduler";

namespace lg {

Scheduler::Scheduler(
    ConfigStore& config,
    LightingRuntime& engine,
    ClockDiscipline& clock,
    CueRunner* cue_runner
) : config_(config), engine_(engine), clock_(clock), cue_runner_(cue_runner) {}

esp_err_t Scheduler::start(const char* timezone) {
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", timezone);

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized, waiting for sync...");

    running_ = true;
    if (shutdown_sem_ == nullptr) {
        shutdown_sem_ = xSemaphoreCreateBinary();
        if (shutdown_sem_ == nullptr) {
            running_ = false;
            esp_sntp_stop();
            return ESP_ERR_NO_MEM;
        }
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "scheduler", 4096, this, 2, &task_handle_, 0
    );

    if (created != pdPASS) {
        running_ = false;
        task_handle_ = nullptr;
        esp_sntp_stop();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Scheduler::apply_sync_transport(SyncTransportMode mode, uint16_t bpm, uint32_t now_ms) {
    portENTER_CRITICAL(&sync_transport_mutex_);
    transport_state_.mode = mode;
    transport_state_.bpm = bpm;
    transport_state_.updated_ms = now_ms;

    sync_transport_.active = (mode != SyncTransportMode::Standalone);
    if (mode == SyncTransportMode::Scene) {
        sync_transport_.mode = RemoteShowMode::Scene;
        sync_transport_.timeline_enabled = false;
        remote_timeline_running_ = false;
    } else if (mode == SyncTransportMode::Timeline) {
        sync_transport_.mode = RemoteShowMode::Timeline;
        sync_transport_.timeline_enabled = true;
    } else if (mode == SyncTransportMode::Cue) {
        sync_transport_.mode = RemoteShowMode::Cue;
        sync_transport_.timeline_enabled = false;
        remote_timeline_running_ = false;
    } else {
        sync_transport_.mode = RemoteShowMode::None;
        sync_transport_.timeline_enabled = false;
        remote_timeline_running_ = false;
    }
    sync_transport_.bpm = bpm;
    sync_transport_.updated_ms = now_ms;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    // Phase 0.3: Stop local CueRunner when switching to remote transport if it was local-driven
    if (mode != SyncTransportMode::Standalone && cue_runner_) {
        if (mode != SyncTransportMode::Cue && cue_runner_->snapshot(clock_.now_ms()).running &&
            !cue_runner_->snapshot(clock_.now_ms()).remote_driven) {
            cue_runner_->stop(CueStopReason::Preempted, clock_.now_ms());
        }
    }
}

void Scheduler::apply_sync_transport(bool timeline_enabled, uint16_t bpm, uint32_t now_ms) {
    apply_sync_transport(
        timeline_enabled ? SyncTransportMode::Timeline : SyncTransportMode::Scene,
        bpm,
        now_ms
    );
}

void Scheduler::clear_sync_transport() {
    portENTER_CRITICAL(&sync_transport_mutex_);
    transport_state_.mode = SyncTransportMode::Standalone;
    transport_state_.bpm = 120;
    transport_state_.updated_ms = 0;

    sync_transport_.active = false;
    sync_transport_.mode = RemoteShowMode::None;
    sync_transport_.timeline_enabled = false;
    sync_transport_.bpm = 120;
    remote_timeline_running_ = false;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    if (cue_runner_) {
        cue_runner_->clear_remote_sync(clock_.now_ms());
    }
}

SyncTransportState Scheduler::sync_transport_state() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    SyncTransportState st = transport_state_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return st;
}

void Scheduler::apply_remote_show_mode(RemoteShowMode mode, uint16_t bpm, uint64_t updated_ms) {
    SyncTransportMode m = SyncTransportMode::Standalone;
    if (mode == RemoteShowMode::Scene) m = SyncTransportMode::Scene;
    else if (mode == RemoteShowMode::Timeline) m = SyncTransportMode::Timeline;
    else if (mode == RemoteShowMode::Cue) m = SyncTransportMode::Cue;
    apply_sync_transport(m, bpm, static_cast<uint32_t>(updated_ms));
}

void Scheduler::clear_remote_show_mode() {
    clear_sync_transport();
}

Scheduler::RemoteShowMode Scheduler::remote_show_mode() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    RemoteShowMode m = sync_transport_.mode;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return m;
}

Scheduler::SyncTimelineTransport Scheduler::sync_transport() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    SyncTimelineTransport copy = sync_transport_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return copy;
}

void Scheduler::apply_remote_timeline(
    uint32_t generation,
    uint64_t anchor_ms,
    uint32_t checksum,
    uint16_t bpm,
    uint64_t now_ms
) {
    portENTER_CRITICAL(&sync_transport_mutex_);
    const bool is_new_generation = (!remote_timeline_running_ || generation != remote_timeline_generation_);
    if (is_new_generation) {
        // Snapshot stored timeline at trigger time
        active_sequence_ = persisted_sequence_;
        if (bpm > 0) {
            active_sequence_.bpm = bpm;
        }
        timeline_anchor_ms_ = anchor_ms;
        remote_timeline_generation_ = generation;
        transport_generation_ = generation;
        for (uint8_t& step_index : last_step_index_) {
            step_index = 255;
        }
    }
    remote_timeline_running_ = true;
    remote_timeline_bpm_ = bpm > 0 ? bpm : (active_sequence_.bpm > 0 ? active_sequence_.bpm : 120);

    transport_state_.mode = SyncTransportMode::Timeline;
    transport_state_.bpm = remote_timeline_bpm_;
    transport_state_.updated_ms = static_cast<uint32_t>(now_ms);

    sync_transport_.active = true;
    sync_transport_.mode = RemoteShowMode::Timeline;
    sync_transport_.timeline_enabled = true;
    sync_transport_.bpm = remote_timeline_bpm_;
    sync_transport_.updated_ms = now_ms;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    if (cue_runner_ && cue_runner_->snapshot(now_ms).running) {
        cue_runner_->stop(CueStopReason::Preempted, now_ms);
    }
}

ShowTransportSnapshot Scheduler::transport_snapshot(uint64_t clock_ms) const {
    if (clock_ms == 0) {
        clock_ms = clock_.now_ms();
    }
    ShowTransportSnapshot snap{};

    portENTER_CRITICAL(&sync_transport_mutex_);
    const SyncTransportState remote = transport_state_;
    const bool is_timeline_active = (local_timeline_running_ || remote_timeline_running_ || calendar_sequence_active_);
    const uint32_t current_generation = transport_generation_;
    const uint64_t anchor = timeline_anchor_ms_;
    const uint16_t effective_bpm = local_timeline_running_
        ? (active_sequence_.bpm > 0 ? active_sequence_.bpm : (local_timeline_bpm_ > 0 ? local_timeline_bpm_ : 120))
        : (remote_timeline_running_ ? (active_sequence_.bpm > 0 ? active_sequence_.bpm : (remote_timeline_bpm_ > 0 ? remote_timeline_bpm_ : 120))
        : (active_sequence_.bpm > 0 ? active_sequence_.bpm : 120));
    const uint32_t active_cs = active_sequence_.checksum;
    const uint32_t saved_cs = persisted_sequence_.checksum;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    // Phase 4.1: Deterministic Show transport precedence:
    // Cue active -> Cue; else Timeline active -> Timeline; else Scene
    if (cue_runner_) {
        // LG-034 §9: single locked snapshot; no unlocked per-field reads.
        const auto cue = cue_runner_->snapshot(clock_ms);
        if (cue.running) {
            snap.mode = ShowTransportMode::Cue;
            snap.cue_list = cue.list_index;
            snap.cue_index = cue.current_cue;
            snap.cue_next = cue.next_cue;
            snap.cue_state = cue.state;
            snap.cue_generation = cue.generation;
            snap.cue_elapsed_ms = cue.elapsed_ms;
            snap.manual_fade_q15 = cue.manual_fade_q15;
            snap.bpm = persisted_sequence_.bpm > 0 ? persisted_sequence_.bpm : 120;
            snap.active_scene = engine_.active_scene_index();

            snap.cue.active = true;
            snap.cue.list_index = snap.cue_list;
            snap.cue.state = static_cast<uint8_t>(snap.cue_state);
            snap.cue.current_cue = snap.cue_index;
            snap.cue.next_cue = snap.cue_next;
            snap.cue.generation = snap.cue_generation;
            snap.cue.paused_elapsed_ms = snap.cue_elapsed_ms;
            snap.cue.manual_fade_q15 = snap.manual_fade_q15;

            // LG-034 v6: parity + frozen-fade fields (§27)
            snap.cue.list_checksum = cue.active_checksum;
            snap.cue.transition_progress_q15 = cue.transition_progress_q15;
            snap.cue.paused_from = static_cast<uint8_t>(cue.paused_from);
            snap.cue.source_scene = cue.current_scene;
        } else if (is_timeline_active &&
               (remote.mode == SyncTransportMode::Standalone || remote.mode == SyncTransportMode::Timeline)) {
            snap.mode = ShowTransportMode::Timeline;
            snap.bpm = effective_bpm;
            snap.active_scene = engine_.active_scene_index();
            snap.cue_state = CueRunnerState::Stopped;

            snap.transport_generation = current_generation;
            snap.timeline_anchor_ms = anchor;
            snap.timeline_active_checksum = active_cs;
            snap.timeline_saved_checksum = saved_cs;

            const uint64_t elapsed = clock_ms >= anchor ? clock_ms - anchor : 0;
            snap.timeline_elapsed_ms = elapsed;
            const uint32_t ms_per_beat = 60000 / (snap.bpm > 0 ? snap.bpm : 120);
            snap.timeline_current_beat = ms_per_beat > 0 ? static_cast<uint32_t>(elapsed / ms_per_beat) : 0;
            snap.timeline_content_stale = (active_cs != saved_cs && active_cs != 0);
        } else {
            snap.mode = ShowTransportMode::Scene;
            snap.active_scene = engine_.active_scene_index();
            snap.bpm = persisted_sequence_.bpm > 0 ? persisted_sequence_.bpm : 120;
            snap.cue_state = CueRunnerState::Stopped;
            snap.timeline_saved_checksum = saved_cs;
        }
    }
    return snap;
}

esp_err_t Scheduler::stop() {
    if (!running_) return ESP_OK;
    running_ = false;
    clear_sync_transport();
    if (task_handle_) {
        xTaskAbortDelay(task_handle_);
    }
    esp_sntp_stop();
    if (shutdown_sem_ && task_handle_) {
        if (xSemaphoreTake(shutdown_sem_, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGW(TAG, "Scheduler task did not exit within 3s — possible resource leak");
        }
    }
    task_handle_ = nullptr;
    return ESP_OK;
}

void Scheduler::task_entry(void* arg) {
    static_cast<Scheduler*>(arg)->task();
    Scheduler* self = static_cast<Scheduler*>(arg);
    if (self->shutdown_sem_) {
        xSemaphoreGive(self->shutdown_sem_);
    }
    vTaskDelete(nullptr);
}

void Scheduler::task() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!running_) return;
        if (clock_.has_epoch()) {
            time_synced_ = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!running_) return;

        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t now_epoch;
            time(&now_epoch);
            clock_.set_epoch(uint64_t(now_epoch) * 1000ULL);
            time_synced_ = true;
            break;
        }
    }

    if (!time_synced_) {
        ESP_LOGW(TAG, "SNTP sync failed, sequencer will use local uptime");
    }

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (running_) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));

        if (!time_synced_) {
            if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                time_t now_epoch;
                time(&now_epoch);
                clock_.set_epoch(uint64_t(now_epoch) * 1000ULL);
                time_synced_ = true;
            } else if (clock_.has_epoch()) {
                time_synced_ = true;
            }
        }

        const uint64_t clock_ms = clock_.now_ms();
        refresh_config(clock_ms);
        if (cue_runner_) {
            cue_runner_->update(clock_ms);
        }
        evaluate_calendar(calendar_, clock_ms);

        // Phase 10: Local precedence: if CueRunner is running, do not evaluate Timeline this tick
        if (cue_runner_ && cue_runner_->snapshot(clock_ms).running) {
            engine_.release_control(ControlSource::Scheduler);
            continue;
        }

        bool timeline_running = false;
        uint16_t seq_bpm = 120;
        uint64_t anchor_ms = 0;

        portENTER_CRITICAL(&sync_transport_mutex_);
        if (transport_state_.mode != SyncTransportMode::Standalone) {
            if (transport_state_.mode == SyncTransportMode::Timeline && remote_timeline_running_) {
                timeline_running = true;
                seq_bpm = active_sequence_.bpm > 0 ? active_sequence_.bpm : (remote_timeline_bpm_ > 0 ? remote_timeline_bpm_ : 120);
                anchor_ms = timeline_anchor_ms_;
            }
        } else if (local_timeline_running_) {
            timeline_running = true;
            seq_bpm = active_sequence_.bpm > 0 ? active_sequence_.bpm : (local_timeline_bpm_ > 0 ? local_timeline_bpm_ : 120);
            anchor_ms = timeline_anchor_ms_;
        } else if (calendar_sequence_active_) {
            timeline_running = true;
            seq_bpm = active_sequence_.bpm > 0 ? active_sequence_.bpm : 120;
            anchor_ms = calendar_sequence_started_ms_;
        }
        portEXIT_CRITICAL(&sync_transport_mutex_);

        if (!timeline_running || seq_bpm == 0) {
            engine_.release_control(ControlSource::Scheduler);
            continue;
        }

        const uint64_t elapsed_ms = clock_ms >= anchor_ms ? clock_ms - anchor_ms : 0;
        evaluate(active_sequence_, elapsed_ms);
    }
}

void Scheduler::refresh_config(uint64_t clock_ms) {
    if (last_config_refresh_ms_ != 0 &&
        clock_ms - last_config_refresh_ms_ < 1000) {
        return;
    }

    Sequence sequence;
    if (config_.load_sequence(sequence) == ESP_OK) {
        portENTER_CRITICAL(&sync_transport_mutex_);
        persisted_sequence_ = sequence;
        // In idle state, keep active snapshot aligned with persisted content
        if (!local_timeline_running_ && !remote_timeline_running_ && !calendar_sequence_active_) {
            active_sequence_ = sequence;
        }
        portEXIT_CRITICAL(&sync_transport_mutex_);
    }

    CalendarSchedule calendar;
    if (config_.load_calendar_schedule(calendar) == ESP_OK) {
        calendar_ = calendar;
    }

    last_config_refresh_ms_ = clock_ms;
}

void Scheduler::evaluate_calendar(
    const CalendarSchedule& calendar,
    uint64_t clock_ms
) {
    if (!clock_.has_epoch()) return;

    // Phase 0.2: Suppress follower-local calendar triggers when under remote transport
    portENTER_CRITICAL(&sync_transport_mutex_);
    const bool is_remote = (transport_state_.mode != SyncTransportMode::Standalone);
    portEXIT_CRITICAL(&sync_transport_mutex_);
    if (is_remote) return;

    const time_t epoch = static_cast<time_t>(clock_ms / 1000ULL);
    struct tm local_time;
    localtime_r(&epoch, &local_time);
    const uint64_t current_minute = clock_ms / 60000ULL;
    const uint8_t day_bit = static_cast<uint8_t>(1U << local_time.tm_wday);

    for (uint8_t index = 0; index < 8; ++index) {
        const CalendarScheduleEntry& entry = calendar.entries[index];
        const bool matches =
            entry.enabled &&
            entry.hour == local_time.tm_hour &&
            entry.minute == local_time.tm_min &&
            (entry.days_of_week & day_bit) != 0;
        if (!matches || last_calendar_minute_[index] == current_minute) {
            continue;
        }

        last_calendar_minute_[index] = current_minute;
        if (entry.action == CalendarAction::Sequence) {
            portENTER_CRITICAL(&sync_transport_mutex_);
            active_sequence_ = persisted_sequence_;
            calendar_sequence_active_ = true;
            calendar_sequence_started_ms_ = clock_ms;
            timeline_anchor_ms_ = clock_ms;
            transport_generation_++;
            for (uint8_t& step_index : last_step_index_) {
                step_index = 255;
            }
            portEXIT_CRITICAL(&sync_transport_mutex_);
            ESP_LOGI(TAG, "Calendar started sequence");
        } else if (entry.action == CalendarAction::CueList) {
            calendar_sequence_active_ = false;
            engine_.release_control(ControlSource::Scheduler);
            engine_.release_control(ControlSource::ManualOverride);
            if (cue_runner_) {
                cue_runner_->start(entry.cue_list_index, 0, clock_ms);
                ESP_LOGI(TAG, "Calendar triggered cue list %u", entry.cue_list_index);
            }
        } else {
            calendar_sequence_active_ = false;
            engine_.release_control(ControlSource::Scheduler);
            engine_.release_control(ControlSource::ManualOverride);
            if (engine_.activate_scene_checked(entry.preset_index)) {
                ESP_LOGI(TAG, "Calendar activated preset %u", entry.preset_index);
            } else {
                ESP_LOGW(TAG, "Calendar rejected preset %u: scene unavailable or runtime stopping", entry.preset_index);
            }
        }
    }
}

void Scheduler::evaluate(const Sequence& seq, uint64_t elapsed_ms) {
    const uint16_t bpm = seq.bpm > 0 ? seq.bpm : 120;
    const uint32_t ms_per_beat = 60000 / bpm;
    const uint64_t total_beats = elapsed_ms / ms_per_beat;
    engine_.set_beat_period(static_cast<uint16_t>(
        std::min<uint32_t>(ms_per_beat, UINT16_MAX)
    ));

    SceneConfig active_composite{};
    strncpy(active_composite.name, "Sequenced", sizeof(active_composite.name) - 1);
    active_composite.name[sizeof(active_composite.name) - 1] = '\0';
    active_composite.global_brightness = 255;
    active_composite.transition_ms = 0;

    bool any_track_enabled = false;
    bool state_changed = false;

    for (uint8_t i = 0; i < 16; ++i) {
        const auto& track = seq.tracks[i];
        if (!track.enabled || track.num_steps == 0) {
            if (last_step_index_[i] != 255) {
                last_step_index_[i] = 255;
                state_changed = true;
            }
            continue;
        }
        any_track_enabled = true;

        TimelineStepPosition pos = evaluate_timeline_track(track, total_beats);

        if (last_step_index_[i] != pos.step_index) {
            last_step_index_[i] = pos.step_index;
            state_changed = true;
        }

        uint8_t preset_idx = pos.preset_index;
        SceneConfig p{};
        uint8_t ch = track.channel_id < 16 ? track.channel_id : i;
        if (config_.load_scene(preset_idx, p) == ESP_OK) {
            active_composite.channels[ch] = p.channels[ch];
            active_composite.global_brightness = std::min(
                active_composite.global_brightness,
                p.global_brightness
            );
        }
    }

    if (any_track_enabled) {
        engine_.acquire_control(ControlSource::Scheduler, 200);
        if (state_changed) {
            engine_.activate_scene_direct(active_composite, true);
        }
    } else {
        engine_.release_control(ControlSource::Scheduler);
    }
}

void Scheduler::start_local_timeline(uint16_t bpm, uint64_t anchor_ms) {
    portENTER_CRITICAL(&sync_transport_mutex_);
    active_sequence_ = persisted_sequence_;
    if (bpm > 0) {
        active_sequence_.bpm = bpm;
    }
    local_timeline_running_ = true;
    local_timeline_bpm_ = active_sequence_.bpm;
    timeline_anchor_ms_ = (anchor_ms > 0) ? anchor_ms : clock_.now_ms();
    transport_generation_++;
    for (uint8_t& step_index : last_step_index_) {
        step_index = 255;
    }
    portEXIT_CRITICAL(&sync_transport_mutex_);
}

void Scheduler::stop_local_timeline() {
    portENTER_CRITICAL(&sync_transport_mutex_);
    local_timeline_running_ = false;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    engine_.release_control(ControlSource::Scheduler);
}

bool Scheduler::local_timeline_active() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    bool active = local_timeline_running_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return active;
}

bool Scheduler::is_local_timeline_running() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    bool running = local_timeline_running_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return running;
}

bool Scheduler::is_remote_timeline_running() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    bool running = remote_timeline_running_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return running;
}

uint32_t Scheduler::transport_generation() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    uint32_t gen = transport_generation_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return gen;
}

uint64_t Scheduler::timeline_anchor_ms() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    uint64_t anchor = timeline_anchor_ms_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return anchor;
}

bool Scheduler::is_timeline_content_stale() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    bool stale = (active_sequence_.checksum != persisted_sequence_.checksum);
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return stale;
}

const Sequence& Scheduler::active_sequence() const {
    return active_sequence_;
}

const Sequence& Scheduler::persisted_sequence() const {
    return persisted_sequence_;
}

void Scheduler::reload_sequence() {
    portENTER_CRITICAL(&sync_transport_mutex_);
    config_.load_sequence(persisted_sequence_);
    portEXIT_CRITICAL(&sync_transport_mutex_);
}

void Scheduler::clear_remote_timeline() {
    portENTER_CRITICAL(&sync_transport_mutex_);
    remote_timeline_running_ = false;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    engine_.release_control(ControlSource::Scheduler);
}

void Scheduler::apply_remote_timeline(
    uint32_t generation,
    uint16_t bpm,
    uint64_t anchor_ms,
    uint32_t checksum
) {
    apply_remote_timeline(generation, anchor_ms, checksum, bpm, clock_.now_ms());
}

} // namespace lg
