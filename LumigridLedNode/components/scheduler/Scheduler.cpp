#include "Scheduler.hpp"
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
    } else if (mode == SyncTransportMode::Timeline) {
        sync_transport_.mode = RemoteShowMode::Timeline;
        sync_transport_.timeline_enabled = true;
    } else if (mode == SyncTransportMode::Cue) {
        sync_transport_.mode = RemoteShowMode::Cue;
        sync_transport_.timeline_enabled = false;
    } else {
        sync_transport_.mode = RemoteShowMode::None;
        sync_transport_.timeline_enabled = false;
    }
    sync_transport_.bpm = bpm;
    sync_transport_.updated_ms = now_ms;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    // Phase 0.3: Stop local CueRunner when switching to remote transport if it was local-driven
    if (mode != SyncTransportMode::Standalone && cue_runner_) {
        if (mode != SyncTransportMode::Cue && cue_runner_->is_running() && !cue_runner_->is_remote_sync_active()) {
            cue_runner_->stop(clock_.now_ms());
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

ShowTransportSnapshot Scheduler::transport_snapshot(uint64_t clock_ms) const {
    ShowTransportSnapshot snap{};

    portENTER_CRITICAL(&sync_transport_mutex_);
    const SyncTransportState remote = transport_state_;
    portEXIT_CRITICAL(&sync_transport_mutex_);

    // Phase 4.1: Deterministic Show transport precedence:
    // Cue active -> Cue; else Timeline active -> Timeline; else Scene
    if (cue_runner_ && cue_runner_->is_running()) {
        snap.mode = ShowTransportMode::Cue;
        snap.cue_list = cue_runner_->active_list_index();
        snap.cue_index = cue_runner_->current_cue_index();
        snap.cue_next = cue_runner_->next_cue_index();
        snap.cue_state = cue_runner_->state();
        snap.cue_generation = cue_runner_->transport_generation();
        snap.cue_elapsed_ms = cue_runner_->cue_elapsed_ms(clock_ms);
        snap.manual_fade_q15 = cue_runner_->manual_fade_q15();
        snap.bpm = sequence_.bpm > 0 ? sequence_.bpm : 120;
        snap.active_scene = engine_.active_scene_index();

        snap.cue.active = true;
        snap.cue.list_index = snap.cue_list;
        snap.cue.state = static_cast<uint8_t>(snap.cue_state);
        snap.cue.current_cue = snap.cue_index;
        snap.cue.next_cue = snap.cue_next;
        snap.cue.generation = snap.cue_generation;
        snap.cue.paused_elapsed_ms = snap.cue_elapsed_ms;
        snap.cue.manual_fade_q15 = snap.manual_fade_q15;
    } else if ((sequence_.enabled || local_timeline_running_ || calendar_sequence_active_) &&
               (remote.mode == SyncTransportMode::Standalone || remote.mode == SyncTransportMode::Timeline)) {
        snap.mode = ShowTransportMode::Timeline;
        snap.bpm = local_timeline_running_
            ? (local_timeline_bpm_ > 0 ? local_timeline_bpm_ : 120)
            : (remote.mode == SyncTransportMode::Timeline ? remote.bpm : (sequence_.bpm > 0 ? sequence_.bpm : 120));
        snap.active_scene = engine_.active_scene_index();
        snap.cue_state = CueRunnerState::Stopped;
    } else {
        snap.mode = ShowTransportMode::Scene;
        snap.active_scene = engine_.active_scene_index();
        snap.bpm = sequence_.bpm > 0 ? sequence_.bpm : 120;
        snap.cue_state = CueRunnerState::Stopped;
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
        if (cue_runner_ && cue_runner_->is_running()) {
            engine_.release_control(ControlSource::Scheduler);
            continue;
        }

        bool seq_enabled = sequence_.enabled;
        uint16_t seq_bpm = sequence_.bpm;
        bool suppress_timeline = false;

        portENTER_CRITICAL(&sync_transport_mutex_);
        if (transport_state_.mode != SyncTransportMode::Standalone) {
            if (transport_state_.mode == SyncTransportMode::Scene ||
                transport_state_.mode == SyncTransportMode::Cue) {
                // Phase 0.1 & 0.2: Remote Scene and Cue explicitly suppress follower Timeline
                suppress_timeline = true;
                seq_enabled = false;
            } else if (transport_state_.mode == SyncTransportMode::Timeline) {
                seq_enabled = true;
                seq_bpm = transport_state_.bpm;
            }
        } else if (local_timeline_running_) {
            seq_enabled = true;
            seq_bpm = local_timeline_bpm_ > 0 ? local_timeline_bpm_ : 120;
        }
        portEXIT_CRITICAL(&sync_transport_mutex_);

        if (suppress_timeline || seq_bpm == 0 ||
            (!seq_enabled && !calendar_sequence_active_)) {
            engine_.release_control(ControlSource::Scheduler);
            continue;
        }

        Sequence effective_seq = sequence_;
        effective_seq.enabled = seq_enabled;
        effective_seq.bpm = seq_bpm;

        const uint64_t sequence_clock_ms =
            calendar_sequence_active_ && clock_ms >= calendar_sequence_started_ms_
                ? clock_ms - calendar_sequence_started_ms_
                : calendar_sequence_active_ ? 0 : clock_ms;
        evaluate(effective_seq, sequence_clock_ms);
    }
}

void Scheduler::refresh_config(uint64_t clock_ms) {
    if (last_config_refresh_ms_ != 0 &&
        clock_ms - last_config_refresh_ms_ < 1000) {
        return;
    }

    Sequence sequence;
    if (config_.load_sequence(sequence) == ESP_OK) {
        sequence_ = sequence;
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
            calendar_sequence_active_ = true;
            calendar_sequence_started_ms_ = clock_ms;
            for (uint8_t& step_index : last_step_index_) {
                step_index = 255;
            }
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

void Scheduler::evaluate(const Sequence& seq, uint64_t clock_ms) {
    const uint32_t ms_per_beat = 60000 / seq.bpm;
    const uint64_t total_beats = clock_ms / ms_per_beat;
    engine_.set_beat_period(static_cast<uint16_t>(
        std::min<uint32_t>(ms_per_beat, UINT16_MAX)
    ));

    SceneConfig active_composite;
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

        uint32_t track_total_length = 0;
        for (uint8_t s = 0; s < track.num_steps; ++s) {
            track_total_length += track.steps[s].length_beats;
        }
        if (track_total_length == 0) track_total_length = 1;

        const uint32_t beat_in_track = total_beats % track_total_length;
        
        uint32_t current_beat = 0;
        uint8_t current_step = 0;
        for (uint8_t s = 0; s < track.num_steps; ++s) {
            if (beat_in_track >= current_beat && beat_in_track < current_beat + track.steps[s].length_beats) {
                current_step = s;
                break;
            }
            current_beat += track.steps[s].length_beats;
        }

        if (last_step_index_[i] != current_step) {
            last_step_index_[i] = current_step;
            state_changed = true;
        }

        uint8_t preset_idx = track.steps[current_step].preset_index;
        SceneConfig p;
        if (config_.load_scene(preset_idx, p) == ESP_OK && track.channel_id < 16) {
            active_composite.channels[track.channel_id] = p.channels[track.channel_id];
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

void Scheduler::start_local_timeline(uint16_t bpm) {
    portENTER_CRITICAL(&sync_transport_mutex_);
    local_timeline_running_ = true;
    local_timeline_bpm_ = bpm > 0 ? bpm : 120;
    portEXIT_CRITICAL(&sync_transport_mutex_);
}

void Scheduler::stop_local_timeline() {
    portENTER_CRITICAL(&sync_transport_mutex_);
    local_timeline_running_ = false;
    portEXIT_CRITICAL(&sync_transport_mutex_);
}

bool Scheduler::local_timeline_active() const {
    portENTER_CRITICAL(&sync_transport_mutex_);
    bool active = local_timeline_running_;
    portEXIT_CRITICAL(&sync_transport_mutex_);
    return active;
}

} // namespace lg
