// components/signage/SceneManager.cpp
#include "SceneManager.hpp"
#include "esp_log.h"
#include <algorithm>

static const char* TAG = "SceneManager";

namespace lg {

namespace {
uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (int16_t(b) - a) * t / 255);
}

SceneConfig blend_scenes(const SceneConfig& from, const SceneConfig& to, uint8_t t) {
    SceneConfig result = to; // Copy target's structure/metadata
    result.global_brightness = lerp8(from.global_brightness, to.global_brightness, t);

    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            result.zones[z].layers[l].primary_color.r = lerp8(from.zones[z].layers[l].primary_color.r, to.zones[z].layers[l].primary_color.r, t);
            result.zones[z].layers[l].primary_color.g = lerp8(from.zones[z].layers[l].primary_color.g, to.zones[z].layers[l].primary_color.g, t);
            result.zones[z].layers[l].primary_color.b = lerp8(from.zones[z].layers[l].primary_color.b, to.zones[z].layers[l].primary_color.b, t);
                
            result.zones[z].layers[l].secondary_color.r = lerp8(from.zones[z].layers[l].secondary_color.r, to.zones[z].layers[l].secondary_color.r, t);
            result.zones[z].layers[l].secondary_color.g = lerp8(from.zones[z].layers[l].secondary_color.g, to.zones[z].layers[l].secondary_color.g, t);
            result.zones[z].layers[l].secondary_color.b = lerp8(from.zones[z].layers[l].secondary_color.b, to.zones[z].layers[l].secondary_color.b, t);
                
            result.zones[z].layers[l].opacity = lerp8(from.zones[z].layers[l].opacity, to.zones[z].layers[l].opacity, t);
        }
    }
    return result;
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
            strncpy(scene.name, "Default", sizeof(scene.name));
        }
    }

    current_scene_ = scene;
    active_index_ = active_idx;
    scene_started_ms_ = 0;
    last_command_ms_ = 0;
    ESP_LOGI(TAG, "SceneManager starting with scene %u: \"%s\"", active_idx, scene.name);
}

void SceneManager::update_device_config(const DeviceConfig& cfg) {
    device_cfg_ = cfg;
}

void SceneManager::activate_scene(uint8_t index, uint32_t current_ms) {
    SceneConfig scene;
    if (config_.load_scene(index, scene) != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u not found", index);
        return;
    }

    if (transitioning_) {
        const uint32_t elapsed = current_ms - transition_start_ms_;
        const uint8_t t = std::min(uint8_t(255), static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
        current_scene_ = blend_scenes(current_scene_, target_scene_, t);
    }

    target_scene_ = scene;
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, scene.transition_ms);
    transitioning_ = true;
    active_index_ = index;
    fallback_active_ = false;

    config_.save_active_state(index);
    ESP_LOGI(TAG, "Transitioning to preset %u: \"%s\" (%u ms fade)", index, scene.name, scene.transition_ms);
}

void SceneManager::activate_scene_direct(const SceneConfig& scene, uint32_t current_ms, bool immediate) {
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

void SceneManager::inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity, uint32_t current_ms) {
    if (zone_idx >= 8 || layer_idx >= 3) return;
    
    if (!live_stream_active_ || !current_scene_.zones[zone_idx].layers[layer_idx].enabled) {
        current_scene_.zones[zone_idx].layers[layer_idx].enabled = true;
        if (current_scene_.zones[zone_idx].layers[layer_idx].effect != 0 /* Solid */) {
            current_scene_.zones[zone_idx].layers[layer_idx].effect = 0;
            current_scene_.zones[zone_idx].layers[layer_idx].blend_mode = BlendMode::Add;
        }
    }

    current_scene_.zones[zone_idx].layers[layer_idx].primary_color = color;
    current_scene_.zones[zone_idx].layers[layer_idx].opacity = opacity;
    
    fallback_active_ = false;
    live_stream_active_ = true;
    last_live_stream_ms_ = current_ms;
    last_command_ms_ = current_ms;
}

void SceneManager::feed_watchdog(uint32_t current_ms) {
    last_command_ms_ = current_ms;
    if (fallback_active_) {
        fallback_active_ = false;
        ESP_LOGI(TAG, "Watchdog fed, exiting fallback mode");
    }
}

SceneConfig SceneManager::process_frame(uint32_t current_ms) {
    // 1. Process Watchdog
    if (device_cfg_.watchdog_timeout_s > 0 && !fallback_active_) {
        const uint32_t elapsed_s = (current_ms - last_command_ms_) / 1000;
        const uint32_t live_elapsed_ms = (current_ms - last_live_stream_ms_);
        
        bool trigger_fallback = false;
        if (elapsed_s > device_cfg_.watchdog_timeout_s) {
            trigger_fallback = true;
        } else if (live_stream_active_ && live_elapsed_ms > device_cfg_.protocol.stream_timeout_ms) {
            trigger_fallback = true;
            live_stream_active_ = false;
        }

        if (trigger_fallback) {
            if (active_index_ != device_cfg_.fallback_preset_index) {
                ESP_LOGW(TAG, "Watchdog timeout: activating fallback %u", device_cfg_.fallback_preset_index);
                SceneConfig fallback;
                if (config_.load_scene(device_cfg_.fallback_preset_index, fallback) == ESP_OK) {
                    if (transitioning_) {
                        const uint32_t elapsed = current_ms - transition_start_ms_;
                        const uint8_t t = std::min(uint8_t(255), static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
                        current_scene_ = blend_scenes(current_scene_, target_scene_, t);
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
    SceneConfig render_scene;
    if (transitioning_) {
        const uint32_t elapsed = current_ms - transition_start_ms_;
        if (elapsed >= transition_duration_ms_) {
            current_scene_ = target_scene_;
            transitioning_ = false;
            scene_started_ms_ = current_ms;
            render_scene = current_scene_;
            ESP_LOGI(TAG, "Transition complete -> \"%s\"", current_scene_.name);
        } else {
            const uint8_t t = static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_);
            render_scene = blend_scenes(current_scene_, target_scene_, t);
        }
    } else {
        render_scene = current_scene_;
    }

    return render_scene;
}

} // namespace lg
