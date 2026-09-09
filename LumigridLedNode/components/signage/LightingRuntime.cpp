// components/signage/LightingRuntime.cpp
#include "LightingRuntime.hpp"
#include "EffectRenderer.hpp"
#include "Compositor.hpp"
#include "FixtureMapper.hpp"
#include "OutputScheduler.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "LightingRuntime";

namespace lg {
namespace {
uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}
} // namespace

LightingRuntime::LightingRuntime(ControlSourceManager& source_manager, ConfigStore& config)
    : source_manager_(source_manager), config_(config), scene_manager_(config) {}

esp_err_t LightingRuntime::start() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;

    config_.load_config(device_cfg_);
    
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);

    scene_manager_.init();
    scene_manager_.update_device_config(device_cfg_);

    enable_ms_ = now_ms();

    source_manager_.acquire(ControlSource::Fallback);

    const BaseType_t created = xTaskCreatePinnedToCore(
        task_entry, "lighting", 8192, this, 4, &task_handle_, 1
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void LightingRuntime::task_entry(void* arg) {
    static_cast<LightingRuntime*>(arg)->task();
    vTaskDelete(nullptr);
}

void LightingRuntime::task() {
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
        
        uint32_t delay_ms = kFramePeriodMs;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (scene_manager_.is_live_stream_active() && (current_ms - scene_manager_.last_live_stream_ms() < 1000)) {
            delay_ms = 10; // 100 FPS for live streaming
        }
        xSemaphoreGive(mutex_);
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
    }
}

void LightingRuntime::render_frame(uint32_t current_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    SceneConfig render_scene = scene_manager_.process_frame(current_ms);
    uint32_t effect_elapsed = current_ms - scene_manager_.scene_started_ms();
    xSemaphoreGive(mutex_);

    // 1. Calculate global brightness and power budget soft start
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
    
    // Evaluate power budget based on previous frame output
    power_budget_.enforce(source_manager_.pixels(), source_manager_.pwm(), render_scene.global_brightness);

    // 2. Render Pipeline
    RenderFrame& render_frame = current_render_frame_;
    // Clear previous render frame
    for (auto& fixture : render_frame.fixtures) {
        fixture.pixels.fill(Rgb{0,0,0});
        fixture.pwm = 0;
    }

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg_.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single || fixture.profile == FixtureProfile::PWM_RGB || fixture.profile == FixtureProfile::PWM_RGBW);
        if (fixture.num_pixels == 0 && !is_pwm) continue;

        // Find which zone this fixture is in
        uint8_t target_zone = 255;
        for (uint8_t z = 0; z < 8; ++z) {
            if (device_cfg_.zones[z].fixture_mask & (1 << f)) {
                target_zone = z;
                break;
            }
        }
        if (target_zone == 255) continue;

        const ZoneContent& zone = render_scene.zones[target_zone];
        FixtureFrame& composited_zone = current_composited_zone_; 
        composited_zone.pixels.fill(Rgb{0,0,0});
        composited_zone.pwm = 0;

        // Generate and composite layers
        for (uint8_t l = 0; l < 3; ++l) {
            if (!zone.layers[l].enabled || zone.layers[l].opacity == 0) continue;
            
            FixtureFrame& layer_frame = current_layer_frame_;
            layer_frame.pixels.fill(Rgb{0,0,0});
            layer_frame.pwm = 0;
            
            EffectRenderer::render_layer(layer_frame, zone.layers[l], fixture.num_pixels, effect_elapsed);
            Compositor::blend(composited_zone, layer_frame, zone.layers[l].blend_mode, zone.layers[l].opacity, fixture.num_pixels);
        }

        // Map to fixture (apply gamma, white balance, brightness)
        FixtureMapper::map(render_frame.fixtures[f], composited_zone, fixture, render_scene.global_brightness);
    }

    ControlSource source = scene_manager_.is_fallback_active() ? ControlSource::Fallback : ControlSource::ScenePlayback;
    if (source == ControlSource::ScenePlayback) {
        source_manager_.release(ControlSource::Fallback);
        source_manager_.acquire(ControlSource::ScenePlayback);
    } else {
        source_manager_.release(ControlSource::ScenePlayback);
        source_manager_.acquire(ControlSource::Fallback);
    }

    // 3. Output Scheduling
    OutputScheduler::schedule(render_frame, device_cfg_, source_manager_, source);
    
    if (preview_cb_) {
        preview_cb_(source_manager_.pixels());
    }
}

void LightingRuntime::activate_scene(uint8_t index) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    scene_manager_.activate_scene(index, now_ms());
    xSemaphoreGive(mutex_);
}

void LightingRuntime::activate_scene_direct(const SceneConfig& scene, bool immediate) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    scene_manager_.activate_scene_direct(scene, now_ms(), immediate);
    xSemaphoreGive(mutex_);
}

void LightingRuntime::inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    scene_manager_.inject_live_data(zone_idx, layer_idx, color, opacity, now_ms());
    xSemaphoreGive(mutex_);
}

void LightingRuntime::update_dmx_data(const uint8_t* data, size_t length) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (length > 512) length = 512;
    memcpy(dmx_data_.data(), data, length);
    if (length < 512) {
        memset(dmx_data_.data() + length, 0, 512 - length);
    }
    xSemaphoreGive(mutex_);
}

void LightingRuntime::feed_watchdog() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    scene_manager_.feed_watchdog(now_ms());
    xSemaphoreGive(mutex_);
}

uint8_t LightingRuntime::active_scene_index() const {
    return scene_manager_.active_scene_index();
}

bool LightingRuntime::is_fallback_active() const {
    return scene_manager_.is_fallback_active();
}

bool LightingRuntime::is_transitioning() const {
    return scene_manager_.is_transitioning();
}

void LightingRuntime::set_beat_period(uint16_t period_ms) {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    beat_period_ms_ = std::max<uint16_t>(10, std::min<uint16_t>(period_ms, 60000));
    xSemaphoreGive(mutex_);
}

void LightingRuntime::reload_device_config() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    config_.load_config(device_cfg_);
    scene_manager_.update_device_config(device_cfg_);
    xSemaphoreGive(mutex_);
}

} // namespace lg
