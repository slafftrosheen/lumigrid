// components/signage/include/SignageEngine.hpp
#pragma once
#include "OutputArbitrator.hpp"
#include "ConfigStore.hpp"
#include "SignageTypes.hpp"
#include "PowerBudget.hpp"
#include <functional>
#include <vector>

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
    SignageEngine(OutputArbitrator& output, ConfigStore& config);

    esp_err_t start();

    /// Activate a scene by index with smooth cross-fade transition.
    void activate_scene(uint8_t index);

    /// Activate a scene directly (no NVS lookup, immediate or with transition).
    void activate_scene_direct(const SceneConfig& scene, bool immediate = false);

    /// Inject live streaming data (Art-Net/sACN) directly into the current scene.
    void inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity);

    /// Feed the system watchdog (REST/MQTT/etc).
    void feed_watchdog();
    uint8_t active_scene_index() const;
    bool is_fallback_active() const { return fallback_active_; }
    bool is_transitioning() const { return transitioning_; }

    /// Access power budget for configuration.
    PowerBudget& power_budget() { return power_budget_; }

    float get_fps() const { return current_fps_; }

    /// Set the beat period used by beat-synced preset modifiers.
    void set_beat_period(uint16_t period_ms);

    /// Call when DeviceConfig changes to update mapping
    void reload_device_config();

    using PreviewCallback = std::function<void(const AddressableLeds& leds)>;
    void set_preview_callback(PreviewCallback cb) { preview_cb_ = std::move(cb); }

private:
    static void task_entry(void* arg);

    void task();

    void render_frame(uint32_t now_ms);
    void check_watchdog(uint32_t now_ms);

    OutputArbitrator& output_;
    ConfigStore& config_;
    PowerBudget power_budget_;

    DeviceConfig device_cfg_{};
    SceneConfig current_scene_{};
    SceneConfig target_scene_{};
    uint8_t active_index_ = 0;
    uint32_t scene_started_ms_ = 0;

    // Transition state
    uint32_t transition_start_ms_ = 0;
    uint16_t transition_duration_ms_ = 0;
    bool transitioning_ = false;

    // Watchdog state
    uint32_t last_command_ms_ = 0;
    bool fallback_active_ = false;
    bool watchdog_enabled_ = false;
    uint16_t watchdog_timeout_s_ = 60;
    
    // Live stream watchdog state
    uint32_t last_live_stream_ms_ = 0;
    bool live_stream_active_ = false;

    uint16_t beat_period_ms_ = 500;

    // Soft-start tracking
    uint32_t enable_ms_ = 0;
    bool first_frame_ = true;

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    uint32_t frame_count_ = 0;
    uint32_t last_fps_time_ms_ = 0;
    float current_fps_ = 0.0f;

    PreviewCallback preview_cb_;

    static constexpr uint32_t kFramePeriodMs = 25; // 40 FPS
};


} // namespace lg
