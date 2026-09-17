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

    // Returns false when the requested preset cannot be loaded. Callers must not
    // report an activation as accepted unless this returns true.
    bool activate_scene(uint8_t index, uint32_t current_ms);
    void activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate = false);
    void feed_watchdog(uint32_t current_ms);

    // Returns a const reference to the scene to render for the current frame
    const SceneConfig& process_frame(uint32_t current_ms);

    uint8_t active_scene_index() const { return active_index_; }
    bool is_transitioning() const { return transitioning_; }
    bool is_fallback_active() const { return fallback_active_; }
    uint32_t scene_started_ms() const { return scene_started_ms_; }
    const SceneConfig& current_scene() const { return current_scene_; }

private:
    ConfigStore& config_;
    DeviceConfig device_cfg_{};

    SceneConfig current_scene_{};
    SceneConfig target_scene_{};
    SceneConfig render_scene_{};
    
    uint8_t active_index_ = 0;
    uint32_t scene_started_ms_ = 0;
    
    uint32_t transition_start_ms_ = 0;
    uint32_t transition_duration_ms_ = 0;
    bool transitioning_ = false;

    uint32_t last_command_ms_ = 0;
    bool fallback_active_ = false;
    uint8_t saved_active_index_ = 0;
};

} // namespace lg
