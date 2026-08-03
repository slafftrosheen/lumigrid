// components/scheduler/Scheduler.cpp
#include "Scheduler.hpp"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <ctime>

static const char* TAG = "scheduler";

namespace lg {

Scheduler::Scheduler(ConfigStore& config, SignageEngine& engine)
    : config_(config), engine_(engine) {}

esp_err_t Scheduler::start(const char* timezone) {
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", timezone);

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized, waiting for sync...");

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "scheduler", 4096, this, 2, &task_handle_, 0
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void Scheduler::task_entry(void* arg) {
    static_cast<Scheduler*>(arg)->task();
    vTaskDelete(nullptr);
}

void Scheduler::task() {
    for (int attempt = 0; attempt < 30; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        time_t now_epoch;
        time(&now_epoch);
        struct tm now_tm;
        localtime_r(&now_epoch, &now_tm);

        if (now_tm.tm_year > (2024 - 1900)) {
            time_synced_ = true;
            break;
        }
    }

    if (!time_synced_) {
        ESP_LOGW(TAG, "SNTP sync failed, sequencer will use local uptime");
    }

    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50)); // Evaluate 20x a second

        Sequence seq;
        if (config_.load_sequence(seq) != ESP_OK || !seq.enabled || seq.bpm == 0) {
            continue;
        }

        uint64_t clock_ms = esp_timer_get_time() / 1000ULL;
        if (time_synced_) {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            clock_ms = (uint64_t(tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
        }

        evaluate(seq, clock_ms);
    }
}

void Scheduler::evaluate(const Sequence& seq, uint64_t clock_ms) {
    const uint32_t ms_per_beat = 60000 / seq.bpm;
    const uint64_t total_beats = clock_ms / ms_per_beat;

    BrandPreset active_composite;
    // Just use a base preset name for reference
    active_composite.set_name("Sequenced");
    active_composite.brightness = 255;
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
        BrandPreset p;
        if (config_.load_preset(preset_idx, p) == ESP_OK) {
            active_composite.channels[track.channel_id] = p.channels[track.channel_id];
        }
    }

    if (any_track_enabled && state_changed) {
        engine_.activate_preset_direct(active_composite, true);
    }
}

} // namespace lg
