// components/signage/LightingRuntime.cpp
#include "LightingRuntime.hpp"
#include "EffectRenderer.hpp"
#include "Compositor.hpp"
#include "FixtureMapper.hpp"
#include "OutputScheduler.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

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
        if (last_live_stream_ms_ > 0 && (current_ms - last_live_stream_ms_ < 1000)) {
            delay_ms = 10; // 100 FPS for live streaming
        }
        xSemaphoreGive(mutex_);
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(delay_ms));
    }
}

void LightingRuntime::render_frame(uint32_t current_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const SceneConfig& render_scene = scene_manager_.process_frame(current_ms);
    const uint32_t effect_elapsed = current_ms - scene_manager_.scene_started_ms();

    // Check if live layers or live fixtures timed out
    const uint32_t stream_timeout = device_cfg_.protocol.stream_timeout_ms > 0 ? device_cfg_.protocol.stream_timeout_ms : 2000;
    bool any_live_active = false;
    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            if (live_layers_[z][l].active) {
                if (current_ms - live_layers_[z][l].timestamp_ms > stream_timeout) {
                    live_layers_[z][l].active = false;
                } else {
                    any_live_active = true;
                }
            }
        }
    }
    for (uint8_t f = 0; f < 16; ++f) {
        if (live_fixtures_[f].active) {
            if (current_ms - live_fixtures_[f].timestamp_ms > stream_timeout) {
                live_fixtures_[f].active = false;
            } else {
                any_live_active = true;
            }
        }
    }

    if (!any_live_active && source_manager_.current_owner() == ControlSource::LiveDmx) {
        source_manager_.release(ControlSource::LiveDmx);
    }

    // 1. Render Pipeline
    RenderFrame& render_frame = current_render_frame_;
    for (auto& fixture : render_frame.fixtures) {
        fixture.pixels.fill(LightValue{0,0,0,0});
        fixture.pwm.fill(0);
    }

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg_.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                       fixture.profile == FixtureProfile::PWM_RGB ||
                       fixture.profile == FixtureProfile::PWM_RGBW);
        if (fixture.num_pixels == 0 && !is_pwm) continue;

        const uint8_t target_zone = fixture.zone_id;
        if (target_zone >= 8) continue;

        const ZoneContent& zone = render_scene.zones[target_zone];
        FixtureFrame& composited_zone = current_composited_zone_; 
        composited_zone.pixels.fill(LightValue{0,0,0,0});
        composited_zone.pwm.fill(0);

        // Composite 3 layers
        for (uint8_t l = 0; l < 3; ++l) {
            ControlSource owner = source_manager_.resolve(target_zone, l, current_ms);
            FixtureFrame& layer_frame = current_layer_frame_;
            layer_frame.pixels.fill(LightValue{0,0,0,0});
            layer_frame.pwm.fill(0);

            if (owner == ControlSource::LiveDmx && live_layers_[target_zone][l].active) {
                const auto& live = live_layers_[target_zone][l];
                if (live.opacity == 0) continue;
                
                uint16_t pcount = std::min<uint16_t>(fixture.num_pixels, MAX_PIXELS);
                for (uint16_t px = 0; px < pcount; ++px) {
                    layer_frame.pixels[px] = LightValue{live.color.r, live.color.g, live.color.b, 0};
                }
                layer_frame.pwm[0] = (static_cast<uint32_t>(live.color.r) * 4095) / 255;
                
                Compositor::blend(composited_zone, layer_frame, BlendMode::Normal, live.opacity, fixture.num_pixels);
            } else {
                if (!zone.layers[l].enabled || zone.layers[l].opacity == 0) continue;
                EffectRenderer::render_layer(layer_frame, zone.layers[l], fixture.num_pixels, effect_elapsed);
                Compositor::blend(composited_zone, layer_frame, zone.layers[l].blend_mode, zone.layers[l].opacity, fixture.num_pixels);
            }
        }

        // Map to fixture (applies effective brightness, white balance, SK6812 RGBW, and gamma)
        FixtureMapper::map(render_frame.fixtures[f], composited_zone, fixture, render_scene.global_brightness);

        // If direct fixture live DMX is active, blend into rendered fixture
        if (live_fixtures_[f].active && live_fixtures_[f].opacity > 0) {
            FixtureFrame live_mapped;
            FixtureFrame live_src;
            const uint16_t pcount = std::min<uint16_t>(static_cast<uint16_t>(live_fixtures_[f].pixels.size()), fixture.num_pixels);
            for (uint16_t p = 0; p < pcount; ++p) {
                live_src.pixels[p] = live_fixtures_[f].pixels[p];
            }
            live_src.pwm = live_fixtures_[f].pwm;
            FixtureMapper::map(live_mapped, live_src, fixture, render_scene.global_brightness);
            Compositor::blend(render_frame.fixtures[f], live_mapped, BlendMode::Normal, live_fixtures_[f].opacity, fixture.num_pixels);
        }
    }

    // 2. Power Limiting (enforce predicted power & soft-start directly on newly rendered RenderFrame)
    power_budget_.enforce(render_frame, device_cfg_, current_ms);

    // 3. Resolve active control source for OutputScheduler
    ControlSource active_source = source_manager_.current_owner();
    if (active_source == ControlSource::Idle || active_source == ControlSource::Fallback || active_source == ControlSource::ScenePlayback) {
        active_source = scene_manager_.is_fallback_active() ? ControlSource::Fallback : ControlSource::ScenePlayback;
        if (active_source == ControlSource::ScenePlayback) {
            source_manager_.release(ControlSource::Fallback);
            source_manager_.acquire(ControlSource::ScenePlayback);
        } else {
            source_manager_.release(ControlSource::ScenePlayback);
            source_manager_.acquire(ControlSource::Fallback);
        }
    }

    // 4. Output Scheduling
    OutputScheduler::schedule(render_frame, device_cfg_, source_manager_, active_source);
    
    if (preview_cb_) {
        preview_cb_(source_manager_.pixels());
    }
    xSemaphoreGive(mutex_);
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
    if (zone_idx >= 8 || layer_idx >= 3) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t now = now_ms();
    live_layers_[zone_idx][layer_idx].color = color;
    live_layers_[zone_idx][layer_idx].opacity = opacity;
    live_layers_[zone_idx][layer_idx].timestamp_ms = now;
    live_layers_[zone_idx][layer_idx].active = true;
    last_live_stream_ms_ = now;

    const uint16_t timeout_ms = device_cfg_.protocol.stream_timeout_ms > 0 ? device_cfg_.protocol.stream_timeout_ms : 2000;
    source_manager_.acquire(ControlSource::LiveDmx, timeout_ms, (1 << zone_idx), (1 << layer_idx));
    xSemaphoreGive(mutex_);
}

void LightingRuntime::inject_live_fixture(
    uint8_t fixture_idx,
    const LightValue* pixels,
    uint16_t count,
    const uint16_t* pwm_duties,
    uint8_t opacity
) {
    if (fixture_idx >= 16) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t now = now_ms();
    auto& lf = live_fixtures_[fixture_idx];
    uint16_t pcount = std::min<uint16_t>(count, MAX_PIXELS);
    if (pixels && pcount > 0) {
        if (lf.pixels.size() != pcount) {
            lf.pixels.resize(pcount);
        }
        std::memcpy(lf.pixels.data(), pixels, pcount * sizeof(LightValue));
    }
    if (pwm_duties) {
        for (uint8_t ch = 0; ch < 4; ++ch) lf.pwm[ch] = pwm_duties[ch];
    }
    lf.opacity = opacity;
    lf.timestamp_ms = now;
    lf.active = true;
    last_live_stream_ms_ = now;

    const uint16_t timeout_ms = device_cfg_.protocol.stream_timeout_ms > 0 ? device_cfg_.protocol.stream_timeout_ms : 2000;
    const uint8_t zone_id = device_cfg_.fixtures[fixture_idx].zone_id;
    source_manager_.acquire(ControlSource::LiveDmx, timeout_ms, (1 << zone_id), 0xFF);
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
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
    scene_manager_.update_device_config(device_cfg_);
    if (config_reload_cb_) {
        config_reload_cb_(device_cfg_);
    }
    xSemaphoreGive(mutex_);
}

bool LightingRuntime::acquire_control(ControlSource source, uint32_t lease_ms, uint8_t zone_mask, uint8_t layer_mask) {
    if (!mutex_) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool won = source_manager_.acquire(source, lease_ms, zone_mask, layer_mask);
    xSemaphoreGive(mutex_);
    return won;
}

void LightingRuntime::release_control(ControlSource source) {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    source_manager_.release(source);
    xSemaphoreGive(mutex_);
}

ControlSource LightingRuntime::current_control_source() const {
    return source_manager_.current_owner();
}

} // namespace lg
