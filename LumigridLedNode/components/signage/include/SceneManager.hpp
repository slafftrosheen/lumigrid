// components/signage/include/SceneManager.hpp
#pragma once
#include "SignageTypes.hpp"
#include "ConfigStore.hpp"

namespace lg {

class SceneManager {
public:
    SceneManager(ConfigStore& config);

    void init();
    void update_device_config(const DeviceConfig& cfg);

    void activate_scene(uint8_t index, uint32_t current_ms);
    void activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate = false);
    void inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity, uint32_t current_ms);
    void feed_watchdog(uint32_t current_ms);

    // Returns the scene to render for the current frame
    SceneConfig process_frame(uint32_t current_ms);

    uint8_t active_scene_index() const { return active_index_; }
    bool is_transitioning() const { return transitioning_; }
    bool is_fallback_active() const { return fallback_active_; }
    uint32_t scene_started_ms() const { return scene_started_ms_; }

    bool is_live_stream_active() const { return live_stream_active_; }
    uint32_t last_live_stream_ms() const { return last_live_stream_ms_; }

private:
    ConfigStore& config_;
    DeviceConfig device_cfg_{};

    SceneConfig current_scene_{};
    SceneConfig target_scene_{};
    
    uint8_t active_index_ = 0;
    uint32_t scene_started_ms_ = 0;
    
    uint32_t transition_start_ms_ = 0;
    uint32_t transition_duration_ms_ = 0;
    bool transitioning_ = false;

    uint32_t last_command_ms_ = 0;
    bool fallback_active_ = false;
    
    uint32_t last_live_stream_ms_ = 0;
    bool live_stream_active_ = false;
};

} // namespace lg
