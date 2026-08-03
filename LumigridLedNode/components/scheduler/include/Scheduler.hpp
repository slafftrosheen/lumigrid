// components/scheduler/include/Scheduler.hpp
#pragma once
#include "ConfigStore.hpp"
#include "SignageEngine.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>

namespace lg {

/// NTP time-sync + day-night schedule evaluator.
///
/// Starts an SNTP client, then periodically checks stored schedule entries
/// against the current local time. When a matching entry is found, it
/// activates the corresponding preset via SignageEngine.
class Scheduler {
public:
    Scheduler(ConfigStore& config, SignageEngine& engine);

    /// Initialize SNTP and start the background evaluation task.
    esp_err_t start(const char* timezone);

    /// Returns true if SNTP has obtained at least one valid time.
    bool has_time() const { return time_synced_; }

private:
    static void task_entry(void* arg);
    void task();
    void evaluate(const ::tm& now);

    ConfigStore& config_;
    SignageEngine& engine_;

    bool time_synced_ = false;
    int8_t last_activated_ = -1; // Prevent re-triggering the same entry

    TaskHandle_t task_handle_ = nullptr;
};

} // namespace lg
