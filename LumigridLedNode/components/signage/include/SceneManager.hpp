// components/signage/include/SceneManager.hpp
#pragma once
#include "SignageTypes.hpp"
#include "ConfigStore.hpp"
#include "SceneTransition.hpp"

#include "AutomationEngine.hpp"

namespace lg {

class SceneManager {
public:
    SceneManager(ConfigStore& config);

    void init();
    void update_device_config(const DeviceConfig& cfg);

    // Returns false when the requested preset cannot be loaded. Callers must not
    // report an activation as accepted unless this returns true.
    bool activate_scene(uint8_t index, uint32_t current_ms);
    bool activate_scene_custom(uint8_t index, uint16_t fade_ms, TransitionCurve curve, uint32_t current_ms);
    bool prepare_manual_transition(uint8_t target_index);
    void set_manual_transition_progress(uint16_t progress_q15);
    bool is_manual_transition() const { return manual_transition_; }
    void activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate = false);
    void feed_watchdog(uint32_t current_ms);

    // Returns a const reference to the scene to render for the current frame
    const SceneConfig& process_frame(uint32_t current_ms);

    uint8_t active_scene_index() const { return active_index_; }
    bool is_transitioning() const { return transitioning_; }
    bool is_fallback_active() const { return fallback_active_; }
    uint32_t scene_started_ms() const { return scene_started_ms_; }
    const SceneConfig& current_scene() const { return current_scene_; }
    const SceneConfig& source_scene() const { return current_scene_; }
    const SceneConfig& target_scene() const { return target_scene_; }
    uint16_t transition_progress_q15() const { return transition_progress_q15_; }
    TransitionCurve transition_curve() const { return transition_curve_; }
    void set_transition_curve(TransitionCurve curve) { transition_curve_ = curve; }

    const SceneMotionConfig& current_motion() const { return current_motion_; }
    const SceneMotionConfig& target_motion() const { return target_motion_; }
    void set_motion_direct(const SceneMotionConfig& motion) {
        current_motion_ = motion;
        target_motion_ = motion;
        transition_curve_ = motion.transition_curve;
    }
    void reload_motion(uint8_t index) {
        if (index == active_index_) {
            if (config_.load_scene_motion(index, current_motion_) != ESP_OK) {
                current_motion_.reset();
            }
            target_motion_ = current_motion_;
            transition_curve_ = current_motion_.transition_curve;
        }
    }

private:
    ConfigStore& config_;
    DeviceConfig device_cfg_{};

    SceneConfig current_scene_{};
    SceneConfig target_scene_{};
    SceneConfig render_scene_{};
    
    SceneMotionConfig current_motion_{};
    SceneMotionConfig target_motion_{};
    
    uint8_t active_index_ = 0;
    uint32_t scene_started_ms_ = 0;
    
    uint32_t transition_start_ms_ = 0;
    uint32_t transition_duration_ms_ = 0;
    uint16_t transition_progress_q15_ = 32768;
    TransitionCurve transition_curve_ = TransitionCurve::EaseInOut;
    bool transitioning_ = false;
    bool manual_transition_ = false;
    uint8_t manual_target_index_ = 0;

    uint32_t last_command_ms_ = 0;
    bool fallback_active_ = false;
    uint8_t saved_active_index_ = 0;
};

} // namespace lg
