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
#include "ShowTransport.hpp"

namespace lg {

enum class SyncTransportMode : uint8_t {
    Standalone = 0,
    Scene      = 1,
    Timeline   = 2,
    Cue        = 3,
};

struct SyncTransportState {
    SyncTransportMode mode = SyncTransportMode::Standalone;
    uint16_t bpm = 120;
    uint32_t updated_ms = 0;
};

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

    enum class RemoteShowMode : uint8_t {
        None = 0,
        Scene,
        Timeline,
        Cue,
    };

    struct SyncTimelineTransport {
        bool active = false;
        RemoteShowMode mode = RemoteShowMode::None;
        bool timeline_enabled = false;
        uint16_t bpm = 120;
        uint64_t updated_ms = 0;
    };

    void apply_remote_show_mode(RemoteShowMode mode, uint16_t bpm = 120, uint64_t updated_ms = 0);
    void clear_remote_show_mode();
    RemoteShowMode remote_show_mode() const;

    // Canonical transport v2 API (Phase 0.2)
    void apply_sync_transport(SyncTransportMode mode, uint16_t bpm, uint32_t now_ms);
    void apply_sync_transport(bool timeline_enabled, uint16_t bpm, uint32_t now_ms);
    void clear_sync_transport();
    SyncTransportState sync_transport_state() const;
    SyncTimelineTransport sync_transport() const;

    // Snapshot of current effective transport (Phase 4.2)
    ShowTransportSnapshot transport_snapshot(uint64_t clock_ms) const;

    void start_local_timeline(uint16_t bpm = 0);
    void stop_local_timeline();
    bool local_timeline_active() const;

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
    uint8_t last_step_index_[16] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255
    };
    Sequence sequence_{};
    CalendarSchedule calendar_{};
    uint64_t last_config_refresh_ms_ = 0;
    uint64_t last_calendar_minute_[8] = {
        UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
        UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX
    };
    bool calendar_sequence_active_ = false;
    uint64_t calendar_sequence_started_ms_ = 0;
    bool local_timeline_running_ = false;
    uint16_t local_timeline_bpm_ = 120;

    TaskHandle_t task_handle_ = nullptr;
    bool running_ = false;
    SemaphoreHandle_t shutdown_sem_ = nullptr;

    mutable portMUX_TYPE sync_transport_mutex_ = portMUX_INITIALIZER_UNLOCKED;
    SyncTransportState transport_state_{};
    SyncTimelineTransport sync_transport_{};
};

} // namespace lg
