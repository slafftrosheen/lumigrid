// components/signage/SignageEngine.cpp
#include "SignageEngine.hpp"
#include "SignageEffects.hpp"
#include "GammaLut.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "signage";

namespace lg {
namespace {

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (int16_t(b) - a) * t / 255);
}

SceneConfig blend_scenes(const SceneConfig& from, const SceneConfig& to, uint8_t t) {
    SceneConfig result = to; // Copy target's structure/metadata
    result.global_brightness = lerp8(from.global_brightness, to.global_brightness, t);

    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            result.zones[z].layers[l].primary_color.r = lerp8(
                from.zones[z].layers[l].primary_color.r, to.zones[z].layers[l].primary_color.r, t);
            result.zones[z].layers[l].primary_color.g = lerp8(
                from.zones[z].layers[l].primary_color.g, to.zones[z].layers[l].primary_color.g, t);
            result.zones[z].layers[l].primary_color.b = lerp8(
                from.zones[z].layers[l].primary_color.b, to.zones[z].layers[l].primary_color.b, t);
                
            result.zones[z].layers[l].secondary_color.r = lerp8(
                from.zones[z].layers[l].secondary_color.r, to.zones[z].layers[l].secondary_color.r, t);
            result.zones[z].layers[l].secondary_color.g = lerp8(
                from.zones[z].layers[l].secondary_color.g, to.zones[z].layers[l].secondary_color.g, t);
            result.zones[z].layers[l].secondary_color.b = lerp8(
                from.zones[z].layers[l].secondary_color.b, to.zones[z].layers[l].secondary_color.b, t);
                
            result.zones[z].layers[l].opacity = lerp8(
                from.zones[z].layers[l].opacity, to.zones[z].layers[l].opacity, t);
        }
    }

    return result;
}

} // namespace

SignageEngine::SignageEngine(OutputArbitrator& output, ConfigStore& config)
    : output_(output), config_(config) {}

esp_err_t SignageEngine::start() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;

    // Load device config for watchdog, power settings, and logical mapping
    config_.load_config(device_cfg_);

    watchdog_timeout_s_ = device_cfg_.watchdog_timeout_s;
    watchdog_enabled_ = (watchdog_timeout_s_ > 0);

    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);

    // Load last active scene (power-loss recovery)
    uint8_t active_idx = 0;
    config_.load_active_state(active_idx);

    SceneConfig scene;
    if (config_.load_scene(active_idx, scene) != ESP_OK) {
        // Fallback to scene 0
        active_idx = 0;
        if (config_.load_scene(0, scene) != ESP_OK) {
            ESP_LOGW(TAG, "No scenes available, starting with blackout");
            scene = SceneConfig{};
            strncpy(scene.name, "Default", sizeof(scene.name));
        }
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    current_scene_ = scene;
    active_index_ = active_idx;
    enable_ms_ = now_ms();
    scene_started_ms_ = enable_ms_;
    last_command_ms_ = enable_ms_;
    xSemaphoreGive(mutex_);

    ESP_LOGI(TAG, "Starting with scene %u: \"%s\"", active_idx, scene.name);

    output_.acquire(OutputPriority::Signage); // Acquire indefinite lease

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "signage", 4096, this, 4, &task_handle_, 1
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void SignageEngine::task_entry(void* arg) {
    static_cast<SignageEngine*>(arg)->task();
    vTaskDelete(nullptr);
}

void SignageEngine::task() {
    power_budget_.on_enable();
    last_fps_time_ms_ = now_ms();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    for (;;) {
        const uint32_t current_ms = now_ms();
        
        frame_count_++;
        if (current_ms - last_fps_time_ms_ >= 1000) {
            current_fps_ = (frame_count_ * 1000.0f) / (current_ms - last_fps_time_ms_);
            frame_count_ = 0;
            last_fps_time_ms_ = current_ms;
        }

        render_frame(current_ms);
        check_watchdog(current_ms);
        
        uint32_t delay_ms = kFramePeriodMs;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (live_stream_active_ && (current_ms - last_live_stream_ms_ < 1000)) {
            delay_ms = 10; // 100 FPS for smooth Art-Net/sACN
        }
        xSemaphoreGive(mutex_);
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
    }
}

void SignageEngine::render_frame(uint32_t current_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);

    SceneConfig render_scene;
    uint16_t beat_period_ms = 500;

    if (transitioning_) {
        const uint32_t elapsed = current_ms - transition_start_ms_;
        if (elapsed >= transition_duration_ms_) {
            // Transition complete
            current_scene_ = target_scene_;
            transitioning_ = false;
            scene_started_ms_ = current_ms;
            render_scene = current_scene_;
            ESP_LOGI(TAG, "Transition complete → \"%s\"", current_scene_.name);
        } else {
            // Mid-transition: blend
            const uint8_t t = static_cast<uint8_t>(
                (uint32_t(elapsed) * 255) / transition_duration_ms_
            );
            render_scene = blend_scenes(current_scene_, target_scene_, t);
        }
    } else {
        render_scene = current_scene_;
    }

    const uint32_t effect_elapsed = current_ms - scene_started_ms_;
    beat_period_ms = beat_period_ms_;
    xSemaphoreGive(mutex_);

    // Apply soft-start factor
    if (first_frame_) {
        first_frame_ = false;
    }
    const uint32_t ms_since_enable = current_ms - enable_ms_;
    const uint8_t ss_factor = power_budget_.soft_start_factor(ms_since_enable);
    if (ss_factor < 255) {
        render_scene.global_brightness = static_cast<uint8_t>(
            (uint16_t(render_scene.global_brightness) * ss_factor) / 255
        );
    }

    // Apply power budget
    power_budget_.enforce(output_.pixels(), output_.pwm(), render_scene.global_brightness);

    // Render the effects per fixture mapped from zones
    if (output_.begin_frame(OutputPriority::Signage)) {
        output_.pixels().clear();
        output_.pwm().set_all(0);
        if (scene_is_visible(render_scene, effect_elapsed, beat_period_ms)) {
            for (uint8_t z = 0; z < 8; ++z) {
                if (device_cfg_.zones[z].fixture_mask == 0) continue;
                for (uint8_t f = 0; f < 16; ++f) {
                    if (device_cfg_.zones[z].fixture_mask & (1 << f)) {
                        const FixtureConfig& fixture = device_cfg_.fixtures[f];
                        render_fixture_zone(output_.controller(), fixture, device_cfg_.physical_outputs, render_scene.zones[z], render_scene.global_brightness, effect_elapsed);
                    }
                }
            }
        }
        output_.commit_frame(OutputPriority::Signage);
        
        if (preview_cb_) {
            preview_cb_(output_.pixels());
        }
    }
}

void SignageEngine::check_watchdog(uint32_t current_ms) {
    if (!watchdog_enabled_ || fallback_active_) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t elapsed_s = (current_ms - last_command_ms_) / 1000;
    const uint32_t live_elapsed_ms = (current_ms - last_live_stream_ms_);
    const bool was_live = live_stream_active_;
    xSemaphoreGive(mutex_);

    bool trigger_fallback = false;
    const char* reason = "";

    // 1. System Watchdog
    if (elapsed_s > watchdog_timeout_s_) {
        trigger_fallback = true;
        reason = "System idle";
    }

    // 2. Live Stream Watchdog
    if (!trigger_fallback && was_live) {
        DeviceConfig cfg;
        config_.load_config(cfg);
        if (live_elapsed_ms > cfg.protocol.stream_timeout_ms) {
            trigger_fallback = true;
            reason = "Stream timeout";
            
            xSemaphoreTake(mutex_, portMAX_DELAY);
            live_stream_active_ = false;
            xSemaphoreGive(mutex_);
        }
    }

    if (trigger_fallback) {
        DeviceConfig cfg;
        config_.load_config(cfg);
        
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const uint8_t current_idx = active_index_;
        xSemaphoreGive(mutex_);

        if (current_idx == cfg.fallback_preset_index) {
            // Already on fallback, don't trigger watchdog. Reset timer.
            xSemaphoreTake(mutex_, portMAX_DELAY);
            last_command_ms_ = current_ms;
            xSemaphoreGive(mutex_);
            return;
        }

        ESP_LOGW(TAG, "Watchdog (%s): activating fallback %u", reason, cfg.fallback_preset_index);

        SceneConfig fallback;
        if (config_.load_scene(cfg.fallback_preset_index, fallback) == ESP_OK) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (transitioning_) {
                const uint32_t elapsed = current_ms - transition_start_ms_;
                const uint8_t t = std::min(uint8_t(255),
                    static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
                current_scene_ = blend_scenes(current_scene_, target_scene_, t);
            }
            target_scene_ = fallback;
            transition_start_ms_ = current_ms;
            transition_duration_ms_ = std::max<uint32_t>(1, fallback.transition_ms);
            transitioning_ = true;
            fallback_active_ = true;
            active_index_ = cfg.fallback_preset_index;
            xSemaphoreGive(mutex_);
        } else {
            ESP_LOGE(TAG, "Watchdog fallback preset %u not found", cfg.fallback_preset_index);
            xSemaphoreTake(mutex_, portMAX_DELAY);
            fallback_active_ = true; // Prevent retry loop
            xSemaphoreGive(mutex_);
        }
    }
}

void SignageEngine::inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity) {
    if (zone_idx >= 8 || layer_idx >= 3) return;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    // Auto-enable the layer on first injection
    if (!live_stream_active_ || !current_scene_.zones[zone_idx].layers[layer_idx].enabled) {
        current_scene_.zones[zone_idx].layers[layer_idx].enabled = true;
        // Optionally set effect to Solid if not set
        if (current_scene_.zones[zone_idx].layers[layer_idx].effect != static_cast<uint8_t>(SignageEffect::Solid)) {
            current_scene_.zones[zone_idx].layers[layer_idx].effect = static_cast<uint8_t>(SignageEffect::Solid);
            current_scene_.zones[zone_idx].layers[layer_idx].blend_mode = BlendMode::Add; // Good default for live streaming
        }
    }

    current_scene_.zones[zone_idx].layers[layer_idx].primary_color = color;
    current_scene_.zones[zone_idx].layers[layer_idx].opacity = opacity;
    
    // Clear any fallback state since we have active control
    fallback_active_ = false;
    live_stream_active_ = true;
    last_live_stream_ms_ = now_ms();
    last_command_ms_ = last_live_stream_ms_;
    
    xSemaphoreGive(mutex_);
}

void SignageEngine::activate_scene(uint8_t index) {
    SceneConfig scene;
    if (config_.load_scene(index, scene) != ESP_OK) {
        ESP_LOGW(TAG, "Preset %u not found", index);
        return;
    }

    const uint32_t current_ms = now_ms();

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (transitioning_) {
        // If already transitioning, snap current to wherever we are
        const uint32_t elapsed = current_ms - transition_start_ms_;
        const uint8_t t = std::min(uint8_t(255),
            static_cast<uint8_t>((uint32_t(elapsed) * 255) / transition_duration_ms_));
        current_scene_ = blend_scenes(current_scene_, target_scene_, t);
    }

    target_scene_ = scene;
    transition_start_ms_ = current_ms;
    transition_duration_ms_ = std::max<uint32_t>(1, scene.transition_ms);
    transitioning_ = true;
    active_index_ = index;
    fallback_active_ = false;
    xSemaphoreGive(mutex_);

    // Persist active state for power-loss recovery
    config_.save_active_state(index);

    ESP_LOGI(TAG, "Transitioning to preset %u: \"%s\" (%u ms fade)",
             index, scene.name, scene.transition_ms);
}

void SignageEngine::activate_scene_direct(const SceneConfig& scene, bool immediate) {
    const uint32_t current_ms = now_ms();

    xSemaphoreTake(mutex_, portMAX_DELAY);
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
    xSemaphoreGive(mutex_);
}

void SignageEngine::feed_watchdog() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    last_command_ms_ = now_ms();
    if (fallback_active_) {
        fallback_active_ = false;
        ESP_LOGI(TAG, "Watchdog fed, exiting fallback mode");
    }
    xSemaphoreGive(mutex_);
}

uint8_t SignageEngine::active_scene_index() const {
    return active_index_;
}

void SignageEngine::set_beat_period(uint16_t period_ms) {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    beat_period_ms_ = std::max<uint16_t>(
        10,
        std::min<uint16_t>(period_ms, 60000)
    );
    xSemaphoreGive(mutex_);
}


void SignageEngine::reload_device_config() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    config_.load_config(device_cfg_);
    xSemaphoreGive(mutex_);
}



} // namespace lg
