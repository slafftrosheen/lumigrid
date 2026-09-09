// components/signage/include/LightingRuntime.hpp
#pragma once
#include "ControlSourceManager.hpp"
#include "ConfigStore.hpp"
#include "SignageTypes.hpp"
#include "PowerBudget.hpp"
#include "SceneManager.hpp"
#include "RenderTypes.hpp"
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lg {

class LightingRuntime {
public:
    LightingRuntime(ControlSourceManager& source_manager, ConfigStore& config);

    esp_err_t start();

    void activate_scene(uint8_t index);
    void activate_scene_direct(const SceneConfig& scene, bool immediate = false);
    void inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity);
    void update_dmx_data(const uint8_t* data, size_t length);
    void feed_watchdog();
    
    uint8_t active_scene_index() const;
    bool is_fallback_active() const;
    bool is_transitioning() const;

    PowerBudget& power_budget() { return power_budget_; }
    float get_fps() const { return current_fps_; }
    void set_beat_period(uint16_t period_ms);
    void reload_device_config();

    const std::array<uint8_t, 512>& last_dmx_data() const { return dmx_data_; }

    using PreviewCallback = std::function<void(const AddressableDriver& leds)>;
    void set_preview_callback(PreviewCallback cb) { preview_cb_ = std::move(cb); }

private:
    static void task_entry(void* arg);
    void task();
    void render_frame(uint32_t current_ms);

    ControlSourceManager& source_manager_;
    ConfigStore& config_;
    PowerBudget power_budget_;
    SceneManager scene_manager_;

    DeviceConfig device_cfg_{};
    std::array<uint8_t, 512> dmx_data_{};

    RenderFrame current_render_frame_;
    FixtureFrame current_composited_zone_;
    FixtureFrame current_layer_frame_;

    uint16_t beat_period_ms_ = 500;
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
