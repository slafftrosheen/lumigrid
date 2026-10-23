// components/signage/SceneManager.cpp
#include "SceneManager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>
#include <new>

static const char* TAG = "SceneManager";

namespace lg {

SceneManager::SceneManager(ConfigStore& config) : config_(config) {}

esp_err_t SceneManager::init() {
    esp_err_t err = config_.load_config(device_cfg_);
    if (err != ESP_OK) return err;
    
    uint8_t active_idx = 0;
    config_.load_active_state(active_idx);

    // Startup owns these member buffers; avoid two scene-sized stack values.
    SceneConfig &scene = current_scene_;
    err = config_.load_scene(active_idx, scene);
    if (err == ESP_ERR_NO_MEM) return err;
    if (err != ESP_OK) {
        active_idx = 0;
        err = config_.load_scene(0, scene);
        if (err == ESP_ERR_NO_MEM) return err;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No scenes available, starting with blackout");
            scene.~SceneConfig();
            new (&scene) SceneConfig{};
            strncpy(scene.name, "Default", sizeof(scene.name) - 1);
            scene.name[sizeof(scene.name) - 1] = '\0';
        }
    }

    render_scene_ = scene;
    target_scene_ = scene;
    active_index_ = active_idx;
    saved_active_index_ = active_idx;
    // A stopped runtime may be started again. Its previous transition belongs
    // to the old session, including any manual progress or frozen target.
    transitioning_ = false;
    manual_transition_ = false;
    transition_frozen_ = false;
    transition_start_ms_ = 0;
    transition_duration_ms_ = 0;
    transition_progress_q15_ = 32768;
    manual_target_index_ = active_idx;
    fallback_active_ = false;
    if (config_.load_scene_motion(active_idx, current_motion_) != ESP_OK) {
        current_motion_.reset();
    }
    target_motion_ = current_motion_;
    transition_curve_ = current_motion_.transition_curve;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    scene_started_ms_ = now;
    last_command_ms_ = now;
    ESP_LOGI(TAG, "SceneManager starting with scene %u: \"%s\"", active_idx, scene.name);
    return ESP_OK;
}

void SceneManager::update_device_config(const DeviceConfig& cfg) {
    device_cfg_ = cfg;
}

bool SceneManager::activate_scene_custom(uint8_t index, uint16_t fade_ms, TransitionCurve curve, uint32_t current_ms) {
    SceneConfig scene;
    const esp_err_t load_result = config_.load_scene(index, scene);
    if (load_result != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u cannot be activated: load failed (%s)", index, esp_err_to_name(load_result));
        return false;
    }

    last_command_ms_ = current_ms;

    if (transitioning_) {
        SceneConfig sampled{};
        sample_scene_morph(sampled, current_scene_, target_scene_, transition_progress_q15_);
        current_scene_ = sampled;
        current_motion_ = target_motion_;
    }

    target_scene_ = scene;
    if (config_.load_scene_motion(index, target_motion_) != ESP_OK) {
        target_motion_.reset();
    }
    transition_curve_ = curve;
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, fade_ms);
    transition_progress_q15_ = 0;
    transitioning_ = true;
    manual_transition_ = false;
    active_index_ = index;
    fallback_active_ = false;

    const esp_err_t persist_result = config_.save_active_state(index);
    if (persist_result != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u activated but active-state persistence failed (%s)", index, esp_err_to_name(persist_result));
    }
    ESP_LOGI(TAG, "Transitioning to preset %u: \"%s\" (%u ms fade)", index, scene.name, fade_ms);
    return true;
}

bool SceneManager::activate_scene(uint8_t index, uint32_t current_ms) {
    SceneConfig scene;
    if (config_.load_scene(index, scene) != ESP_OK) return false;
    SceneMotionConfig motion;
    if (config_.load_scene_motion(index, motion) != ESP_OK) motion.reset();
    return activate_scene_custom(index, scene.transition_ms, motion.transition_curve, current_ms);
}

bool SceneManager::prepare_manual_transition(uint8_t target_index) {
    SceneConfig scene;
    if (config_.load_scene(target_index, scene) != ESP_OK) return false;
    if (transitioning_ && !manual_transition_) {
        SceneConfig sampled{};
        sample_scene_morph(sampled, current_scene_, target_scene_, transition_progress_q15_);
        current_scene_ = sampled;
        current_motion_ = target_motion_;
    }
    target_scene_ = scene;
    manual_target_index_ = target_index;
    if (config_.load_scene_motion(target_index, target_motion_) != ESP_OK) {
        target_motion_.reset();
    }
    transitioning_ = true;
    manual_transition_ = true;
    transition_progress_q15_ = 0;
    return true;
}

void SceneManager::set_manual_transition_progress(uint16_t progress_q15) {
    if (!manual_transition_) return;
    transition_progress_q15_ = std::min<uint16_t>(32768, progress_q15);
    if (transition_progress_q15_ >= 32768) {
        current_scene_ = target_scene_;
        current_motion_ = target_motion_;
        active_index_ = manual_target_index_;
        transitioning_ = false;
        manual_transition_ = false;
        transition_frozen_ = false;
        (void)config_.save_active_state(active_index_);
    }
}

bool SceneManager::freeze_transition() {
    // LG-034: only an active automatic transition can be frozen. Manual (T-Bar)
    // transitions are already externally progressed; freezing is a no-op that
    // reports success because the visual state is already operator-controlled.
    if (!transitioning_ || transition_frozen_) {
        return transitioning_ || transition_frozen_;
    }
    if (manual_transition_) {
        transition_frozen_ = true;
        return true;
    }
    // Retain source (current_scene_) and target as-is. Switch this transition to
    // manual progress driven by resume_frozen_transition(); physical output stops
    // advancing because process_frame() samples at the frozen progress value.
    transition_frozen_ = true;
    manual_transition_ = true;
    return true;
}

bool SceneManager::resume_frozen_transition(uint32_t remaining_ms, uint32_t current_ms) {
    if (!transition_frozen_ || !transitioning_) return false;
    transition_frozen_ = false;
    if (manual_transition_) {
        // Resume the automatic portion from the frozen visual position: convert
        // the retained progress into a timed transition over remaining_ms.
        // sample_scene_morph() keeps the already-blended visual as the source.
        SceneConfig sampled{};
        sample_scene_morph(sampled, current_scene_, target_scene_, transition_progress_q15_);
        current_scene_ = sampled;
        current_motion_ = target_motion_;
        manual_transition_ = false;
        transition_start_ms_ = current_ms;
        transition_duration_ms_ = std::max<uint32_t>(1, remaining_ms);
        transition_progress_q15_ = 0;
        return true;
    }
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, remaining_ms);
    return true;
}

void SceneManager::activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate) {
    last_command_ms_ = current_ms;
    if (immediate) {
        current_scene_ = scene;
        target_scene_ = scene;
        current_motion_.reset();
        target_motion_.reset();
        transition_curve_ = TransitionCurve::EaseInOut;
        transitioning_ = false;
        transition_progress_q15_ = 32768;
        scene_started_ms_ = current_ms;
    } else {
        if (transitioning_) {
            SceneConfig sampled{};
            sample_scene_morph(sampled, current_scene_, target_scene_, transition_progress_q15_);
            current_scene_ = sampled;
            current_motion_ = target_motion_;
        }
        target_scene_ = scene;
        target_motion_.reset();
        transition_curve_ = TransitionCurve::EaseInOut;
        transition_start_ms_ = current_ms;
        transition_duration_ms_ = std::max<uint32_t>(1, scene.transition_ms);
        transition_progress_q15_ = 0;
        transitioning_ = true;
    }
    fallback_active_ = false;
}

void SceneManager::feed_watchdog(uint32_t current_ms) {
    last_command_ms_ = current_ms;
    if (fallback_active_) {
        ESP_LOGI(TAG, "Watchdog fed, recovering from fallback to preset %u", saved_active_index_);
        activate_scene(saved_active_index_, current_ms);
    }
}

const SceneConfig& SceneManager::process_frame(uint32_t current_ms) {
    // 1. Process Watchdog
    if (device_cfg_.watchdog_timeout_s > 0 && !fallback_active_) {
        const uint32_t elapsed_s = (current_ms - last_command_ms_) / 1000;
        
        if (elapsed_s > device_cfg_.watchdog_timeout_s) {
            if (active_index_ != device_cfg_.fallback_preset_index) {
                ESP_LOGW(TAG, "Watchdog timeout: activating fallback %u", device_cfg_.fallback_preset_index);
                SceneConfig fallback;
                if (config_.load_scene(device_cfg_.fallback_preset_index, fallback) == ESP_OK) {
                    saved_active_index_ = active_index_;
                    if (transitioning_) {
                        SceneConfig sampled{};
                        sample_scene_morph(sampled, current_scene_, target_scene_, transition_progress_q15_);
                        current_scene_ = sampled;
                        current_motion_ = target_motion_;
                    }
                    target_scene_ = fallback;
                    if (config_.load_scene_motion(device_cfg_.fallback_preset_index, target_motion_) != ESP_OK) {
                        target_motion_.reset();
                    }
                    transition_curve_ = target_motion_.transition_curve;
                    transition_start_ms_ = current_ms;
                    transition_duration_ms_ = std::max<uint32_t>(1, fallback.transition_ms);
                    transition_progress_q15_ = 0;
                    transitioning_ = true;
                    fallback_active_ = true;
                    active_index_ = device_cfg_.fallback_preset_index;
                } else {
                    fallback_active_ = true; // prevent infinite loop
                }
            } else {
                last_command_ms_ = current_ms; // already on fallback
            }
        }
    }

    // 2. Process Transition
    if (transitioning_) {
        if (manual_transition_) {
            sample_scene_morph(render_scene_, current_scene_, target_scene_, transition_progress_q15_);
            AutomationEngine::apply(render_scene_, target_motion_, 0);
            return render_scene_;
        }
        const uint32_t elapsed = current_ms - transition_start_ms_;
        if (elapsed >= transition_duration_ms_) {
            current_scene_ = target_scene_;
            current_motion_ = target_motion_;
            transitioning_ = false;
            transition_progress_q15_ = 32768;
            scene_started_ms_ = current_ms;
            ESP_LOGI(TAG, "Transition complete -> \"%s\"", current_scene_.name);
            render_scene_ = current_scene_;
            AutomationEngine::apply(render_scene_, current_motion_, 0);
            return render_scene_;
        } else {
            const uint16_t linear_t = static_cast<uint16_t>(
                std::min<uint32_t>(32768, (uint64_t(elapsed) * 32768) / transition_duration_ms_)
            );
            transition_progress_q15_ = apply_curve_q15(transition_curve_, linear_t);
            sample_scene_morph(render_scene_, current_scene_, target_scene_, transition_progress_q15_);
            AutomationEngine::apply(render_scene_, target_motion_, elapsed);
            return render_scene_;
        }
    }

    render_scene_ = current_scene_;
    AutomationEngine::apply(render_scene_, current_motion_, current_ms - scene_started_ms_);
    return render_scene_;
}

} // namespace lg
