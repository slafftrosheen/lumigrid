// components/scheduler/Scheduler.cpp
#include "Scheduler.hpp"
#include "esp_log.h"
#include "esp_sntp.h"
#include <ctime>

static const char* TAG = "scheduler";

namespace lg {

Scheduler::Scheduler(ConfigStore& config, SignageEngine& engine)
    : config_(config), engine_(engine) {}

esp_err_t Scheduler::start(const char* timezone) {
    // Set timezone
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", timezone);

    // Initialize SNTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized, waiting for sync...");

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "scheduler", 3072, this, 2, &task_handle_, 0
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void Scheduler::task_entry(void* arg) {
    static_cast<Scheduler*>(arg)->task();
    vTaskDelete(nullptr);
}

void Scheduler::task() {
    // Wait for first SNTP sync (check every 2 seconds, timeout after 60s)
    for (int attempt = 0; attempt < 30; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        time_t now_epoch;
        time(&now_epoch);
        struct tm now_tm;
        localtime_r(&now_epoch, &now_tm);

        // Year > 2024 means we have a valid time
        if (now_tm.tm_year > (2024 - 1900)) {
            time_synced_ = true;
            ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                     now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
                     now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);
            break;
        }
    }

    if (!time_synced_) {
        ESP_LOGW(TAG, "SNTP sync failed after 60s, schedule disabled");
    }

    // Evaluation loop: check every 30 seconds
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        if (!time_synced_) {
            // Retry sync check
            time_t now_epoch;
            time(&now_epoch);
            struct tm now_tm;
            localtime_r(&now_epoch, &now_tm);
            if (now_tm.tm_year > (2024 - 1900)) {
                time_synced_ = true;
            } else {
                continue;
            }
        }

        time_t now_epoch;
        time(&now_epoch);
        struct tm now_tm;
        localtime_r(&now_epoch, &now_tm);
        evaluate(now_tm);
    }
}

void Scheduler::evaluate(const ::tm& now) {
    const uint8_t dow_bit = 1 << now.tm_wday; // tm_wday: 0=Sun
    const uint16_t current_minutes = now.tm_hour * 60 + now.tm_min;

    for (uint8_t i = 0; i < kMaxSchedules; ++i) {
        ScheduleEntry entry;
        if (config_.load_schedule(i, entry) != ESP_OK) continue;
        if (!entry.enabled) continue;

        // Check day of week
        if (!(entry.days_of_week & dow_bit)) continue;

        // Check time window
        const uint16_t start_min = entry.hour_start * 60 + entry.minute_start;
        const uint16_t end_min   = entry.hour_end * 60 + entry.minute_end;

        bool in_window;
        if (start_min <= end_min) {
            // Normal window (e.g., 08:00 – 20:00)
            in_window = (current_minutes >= start_min && current_minutes <= end_min);
        } else {
            // Overnight window (e.g., 22:00 – 06:00)
            in_window = (current_minutes >= start_min || current_minutes <= end_min);
        }

        if (in_window) {
            if (last_activated_ != static_cast<int8_t>(i)) {
                ESP_LOGI(TAG, "Schedule %u matched (%02u:%02u), activating preset %u",
                         i, now.tm_hour, now.tm_min, entry.preset_index);
                engine_.activate_preset(entry.preset_index);
                last_activated_ = static_cast<int8_t>(i);
            }
            return; // First match wins
        }
    }

    // No match: reset tracking so re-entry works
    if (last_activated_ >= 0) {
        last_activated_ = -1;
    }
}

} // namespace lg
