// components/scheduler/include/Scheduler.hpp
#pragma once
#include "ClockDiscipline.hpp"
#include "ConfigStore.hpp"
#include "LightingRuntime.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <ctime>

#include "CueRunner.hpp"

namespace lg {

/// NTP time-sync, calendar trigger, and beat-sequencer evaluator.
class Scheduler {
public:
    Scheduler(
        ConfigStore& config,
        LightingRuntime& engine,
        ClockDiscipline& clock,
        CueRunner* cue_runner = nullptr
    );

    /// Initialize SNTP and start the background evaluation task.
    esp_err_t start(const char* timezone);
    esp_err_t stop();

    /// Returns true once SNTP or a trusted sync leader has supplied an epoch.
    bool has_time() const { return clock_.has_epoch(); }

    /// Authoritative timeline clock telemetry
    uint64_t current_clock_ms() const { return clock_.now_ms(); }
    int32_t estimated_drift_ppm() const { return 0; }
    bool is_time_synced() const { return time_synced_; }

    void set_cue_runner(CueRunner* runner) { cue_runner_ = runner; }
    CueRunner* cue_runner() { return cue_runner_; }

private:
    static void task_entry(void* arg);
    void task();
    void evaluate(const Sequence& seq, uint64_t clock_ms);
    void evaluate_calendar(const CalendarSchedule& schedule, uint64_t clock_ms);
    void refresh_config(uint64_t clock_ms);

    ConfigStore& config_;
    LightingRuntime& engine_;
    ClockDiscipline& clock_;
    CueRunner* cue_runner_ = nullptr;

    bool time_synced_ = false;
    uint8_t last_step_index_[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    Sequence sequence_{};
    CalendarSchedule calendar_{};
    uint64_t last_config_refresh_ms_ = 0;
    uint64_t last_calendar_minute_[8] = {
        UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
        UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX
    };
    bool calendar_sequence_active_ = false;
    uint64_t calendar_sequence_started_ms_ = 0;

    TaskHandle_t task_handle_ = nullptr;
    bool running_ = false;
    /// Signalled by the task just before it calls vTaskDelete.
    /// stop() waits on this to guarantee the task has exited before returning.
    SemaphoreHandle_t shutdown_sem_ = nullptr;
};

} // namespace lg
