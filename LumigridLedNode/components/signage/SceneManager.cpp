// components/signage/SceneManager.cpp
#include "SceneManager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>

static const char* TAG = "SceneManager";

namespace lg {

namespace {
uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (int16_t(b) - a) * t / 255);
}

void blend_scenes_into(SceneConfig& dest, const SceneConfig& from, const SceneConfig& to, uint8_t t) {
    dest = to; // Copy target's structure/metadata
    dest.global_brightness = lerp8(from.global_brightness, to.global_brightness, t);

    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            dest.zones[z].layers[l].primary_color.r = lerp8(from.zones[z].layers[l].primary_color.r, to.zones[z].layers[l].primary_color.r, t);
            dest.zones[z].layers[l].primary_color.g = lerp8(from.zones[z].layers[l].primary_color.g, to.zones[z].layers[l].primary_color.g, t);
            dest.zones[z].layers[l].primary_color.b = lerp8(from.zones[z].layers[l].primary_color.b, to.zones[z].layers[l].primary_color.b, t);
                
            dest.zones[z].layers[l].secondary_color.r = lerp8(from.zones[z].layers[l].secondary_color.r, to.zones[z].layers[l].secondary_color.r, t);
            dest.zones[z].layers[l].secondary_color.g = lerp8(from.zones[z].layers[l].secondary_color.g, to.zones[z].layers[l].secondary_color.g, t);
            dest.zones[z].layers[l].secondary_color.b = lerp8(from.zones[z].layers[l].secondary_color.b, to.zones[z].layers[l].secondary_color.b, t);
                
            dest.zones[z].layers[l].opacity = lerp8(from.zones[z].layers[l].opacity, to.zones[z].layers[l].opacity, t);
        }
    }
}
}

SceneManager::SceneManager(ConfigStore& config) : config_(config) {}

void SceneManager::init() {
    config_.load_config(device_cfg_);
    
    uint8_t active_idx = 0;
    config_.load_active_state(active_idx);

    SceneConfig scene;
    if (config_.load_scene(active_idx, scene) != ESP_OK) {
        active_idx = 0;
        if (config_.load_scene(0, scene) != ESP_OK) {
            ESP_LOGW(TAG, "No scenes available, starting with blackout");
            scene = SceneConfig{};
            strncpy(scene.name, "Default", sizeof(scene.name) - 1);
            scene.name[sizeof(scene.name) - 1] = '\0';
        }
    }

    current_scene_ = scene;
    render_scene_ = scene;
    active_index_ = active_idx;
    saved_active_index_ = active_idx;
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    scene_started_ms_ = now;
    last_command_ms_ = now;
    ESP_LOGI(TAG, "SceneManager starting with scene %u: \"%s\"", active_idx, scene.name);
}

void SceneManager::update_device_config(const DeviceConfig& cfg) {
    device_cfg_ = cfg;
}

bool SceneManager::activate_scene(uint8_t index, uint32_t current_ms) {
    SceneConfig scene;
    const esp_err_t load_result = config_.load_scene(index, scene);
    if (load_result != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u cannot be activated: load failed (%s)", index, esp_err_to_name(load_result));
        return false;
    }

    last_command_ms_ = current_ms;

    if (transitioning_) {
        const uint32_t elapsed = current_ms - transition_start_ms_;
        const uint8_t t = std::min(uint8_t(255), static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
        blend_scenes_into(current_scene_, current_scene_, target_scene_, t);
    }

    target_scene_ = scene;
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, scene.transition_ms);
    transitioning_ = true;
    active_index_ = index;
    fallback_active_ = false;

    const esp_err_t persist_result = config_.save_active_state(index);
    if (persist_result != ESP_OK) {
        // Runtime activation is still valid. Persistence failure is observable in
        // firmware logs and must not roll back a scene already accepted by runtime.
        ESP_LOGW(TAG, "Preset %u activated but active-state persistence failed (%s)", index, esp_err_to_name(persist_result));
    }
    ESP_LOGI(TAG, "Transitioning to preset %u: \"%s\" (%u ms fade)", index, scene.name, scene.transition_ms);
    return true;
}

void SceneManager::activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate) {
    last_command_ms_ = current_ms;
    if (immediate) {
        current_scene_ = scene;
        transitioning_ = false;
        scene_started_ms_ = current_ms;
    } else {
        target_scene_ = scene;
        transition_start_ms_ = current_ms;
        transition_duration_ms_ = std::max<uint32_t>(1, scene.transition_ms);
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
                        const uint32_t elapsed = current_ms - transition_start_ms_;
                        const uint8_t t = std::min(uint8_t(255), static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
                        blend_scenes_into(current_scene_, current_scene_, target_scene_, t);
                    }
                    target_scene_ = fallback;
                    transition_start_ms_ = current_ms;
                    transition_duration_ms_ = std::max<uint32_t>(1, fallback.transition_ms);
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
        const uint32_t elapsed = current_ms - transition_start_ms_;
        if (elapsed >= transition_duration_ms_) {
            current_scene_ = target_scene_;
            transitioning_ = false;
            scene_started_ms_ = current_ms;
            ESP_LOGI(TAG, "Transition complete -> \"%s\"", current_scene_.name);
            return current_scene_;
        } else {
            const uint8_t t = static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_);
            blend_scenes_into(render_scene_, current_scene_, target_scene_, t);
            return render_scene_;
        }
    }

    return current_scene_;
}

} // namespace lg
