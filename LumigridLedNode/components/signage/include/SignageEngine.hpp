// components/signage/include/SignageEngine.hpp
#pragma once
#include "OutputController.hpp"
#include "ConfigStore.hpp"
#include "SignageTypes.hpp"
#include "PowerBudget.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lg {

/// Core signage rendering task. Manages preset activation, smooth
/// cross-fade transitions, network-loss failsafe watchdog, and
/// power-loss recovery from NVS.
///
/// Replaces FactoryRunner's auto-loop for production signage use.
/// Both engines can coexist — FactoryRunner handles test commands,
/// SignageEngine handles persistent preset rendering.
class SignageEngine {
public:
    SignageEngine(OutputController& output, ConfigStore& config);

    esp_err_t start();

    /// Activate a preset by index with smooth cross-fade transition.
    void activate_preset(uint8_t index);

    /// Activate a preset directly (no NVS lookup, immediate or with transition).
    void activate_preset_direct(const BrandPreset& preset, bool immediate = false);

    /// Feed the network watchdog. Call on every external command (REST, Art-Net, MQTT).
    void feed_watchdog();

    /// Query the active state.
    uint8_t active_preset_index() const;
    bool is_fallback_active() const { return fallback_active_; }
    bool is_transitioning() const { return transitioning_; }

    /// Access power budget for configuration.
    PowerBudget& power_budget() { return power_budget_; }

    /// Set stream override mode (Art-Net takes over rendering).
    void set_stream_override(bool active);

    /// Set the beat period used by beat-synced preset modifiers.
    void set_beat_period(uint16_t period_ms);

private:
    static void task_entry(void* arg);
    void task();

    void render_frame(uint32_t now_ms);
    void check_watchdog(uint32_t now_ms);

    OutputController& output_;
    ConfigStore& config_;
    PowerBudget power_budget_;

    BrandPreset current_preset_{};
    BrandPreset target_preset_{};
    uint8_t active_index_ = 0;
    uint32_t preset_started_ms_ = 0;

    // Transition state
    uint32_t transition_start_ms_ = 0;
    uint16_t transition_duration_ms_ = 0;
    bool transitioning_ = false;

    // Watchdog state
    uint32_t last_command_ms_ = 0;
    bool fallback_active_ = false;
    bool watchdog_enabled_ = false;
    uint16_t watchdog_timeout_s_ = 60;

    // Stream override (Art-Net/DMX)
    bool stream_override_ = false;
    uint16_t beat_period_ms_ = 500;

    // Soft-start tracking
    uint32_t enable_ms_ = 0;
    bool first_frame_ = true;

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    static constexpr uint32_t kFramePeriodMs = 25; // 40 FPS
};

} // namespace lg
