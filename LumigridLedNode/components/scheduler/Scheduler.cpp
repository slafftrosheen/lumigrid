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
    ClockDiscipline& clock
) : config_(config), engine_(engine), clock_(clock) {}

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

esp_err_t Scheduler::stop() {
    if (!running_) return ESP_OK;
    running_ = false;
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
        evaluate_calendar(calendar_, clock_ms);

        if (sequence_.bpm == 0 ||
            (!sequence_.enabled && !calendar_sequence_active_)) {
            engine_.release_control(ControlSource::Scheduler);
            continue;
        }

        const uint64_t sequence_clock_ms =
            calendar_sequence_active_ && clock_ms >= calendar_sequence_started_ms_
                ? clock_ms - calendar_sequence_started_ms_
                : calendar_sequence_active_ ? 0 : clock_ms;
        evaluate(sequence_, sequence_clock_ms);
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

    for (uint8_t i = 0; i < 8; ++i) {
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
        if (config_.load_scene(preset_idx, p) == ESP_OK) {
            active_composite.zones[track.channel_id] = p.zones[track.channel_id];
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

} // namespace lg
