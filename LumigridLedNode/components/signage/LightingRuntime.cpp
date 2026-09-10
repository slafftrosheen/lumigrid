// components/signage/LightingRuntime.cpp
#include "LightingRuntime.hpp"
#include "EffectRenderer.hpp"
#include "Compositor.hpp"
#include "FixtureMapper.hpp"
#include "OutputScheduler.hpp"
#include "ColorPipeline.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

static const char* TAG = "LightingRuntime";

namespace lg {
namespace {

inline uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace

LightingRuntime::LightingRuntime(ControlSourceManager& source_manager, OutputController& controller, ConfigStore& config)
    : source_manager_(source_manager),
      controller_(controller),
      config_(config),
      scene_manager_(config) {
    mutex_ = xSemaphoreCreateMutex();
}

LightingRuntime::~LightingRuntime() {
    stop();
}

void LightingRuntime::stop() {
    if (shutting_down_.exchange(true)) {
        return;
    }
    // 1. Stop task
    if (task_handle_) {
        TaskHandle_t th = task_handle_;
        task_handle_ = nullptr;
        vTaskDelete(th);
    }
    // 2. Force blackout
    controller_.blackout();
    // 3. Clear live state and release controls
    if (mutex_) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        live_pending_ = {};
        live_active_ = {};
        live_pending_dirty_ = true;
        last_live_stream_ms_ = 0;
        source_manager_.release(ControlSource::LiveDmx);
        source_manager_.release(ControlSource::ScenePlayback);
        source_manager_.release(ControlSource::Factory);
        xSemaphoreGive(mutex_);

        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t LightingRuntime::start() {
    config_.load_config(device_cfg_);
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
    ColorPipeline::set_gamma(device_cfg_.gamma_x10);

    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    scene_manager_.init();

    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "lighting_rt",
        8192,
        this,
        6,
        &task_handle_,
        1 // Core 1 (app core)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LightingRuntime started (gamma=%.1f, max_mw=%u)", device_cfg_.gamma_x10 / 10.0f, device_cfg_.max_power_mw);
    return ESP_OK;
}

void LightingRuntime::trigger_estop() {
    estop_state_.store(EstopState::Tripped, std::memory_order_release);
    controller_.latch_estop();
    source_manager_.release(ControlSource::Factory);
    clear_live_frame();
    controller_.blackout();
    ESP_LOGW(TAG, "EMERGENCY STOP TRIGGERED: Hardware output blacked out and latched");
}

void LightingRuntime::clear_live_frame() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    live_pending_ = {};
    live_active_ = {};
    live_pending_dirty_ = true;
    last_live_stream_ms_ = 0;
    source_manager_.release(ControlSource::LiveDmx);
    xSemaphoreGive(mutex_);
}

void LightingRuntime::clear_estop() {
    if (estop_state_.load(std::memory_order_relaxed) == EstopState::Tripped) {
        estop_state_.store(EstopState::PendingReset, std::memory_order_release);
        ESP_LOGI(TAG, "EMERGENCY STOP CLEAR REQUESTED: Forcing 1-frame blackout before scene resume");
    }
}

void LightingRuntime::task_entry(void* arg) {
    static_cast<LightingRuntime*>(arg)->task();
}

void LightingRuntime::task() {
    TickType_t last_wake = xTaskGetTickCount();
    enable_ms_ = now_ms();
    power_budget_.on_enable();

    while (true) {
        uint32_t current_ms = now_ms();
        render_frame(current_ms);

        // Calculate and report actual frame rate
        frame_count_++;
        if (current_ms - last_fps_time_ms_ >= 1000) {
            current_fps_ = (frame_count_ * 1000.0f) / (current_ms - last_fps_time_ms_);
            frame_count_ = 0;
            last_fps_time_ms_ = current_ms;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kFramePeriodMs));
    }
}

void LightingRuntime::render_frame(uint32_t current_ms) {
    // Check E-Stop state
    const EstopState es = estop_state_.load(std::memory_order_acquire);
    if (es == EstopState::Tripped) {
        controller_.blackout();
        if (preview_cb_) {
            RenderFrame blank{};
            DeviceConfig local_cfg = get_config_snapshot();
            preview_cb_(blank, local_cfg);
        }
        return;
    } else if (es == EstopState::PendingReset) {
        // Frame N: Controller remains latched/disabled; force blackout; advance to ReArmedBlackout
        controller_.blackout();
        if (preview_cb_) {
            RenderFrame blank{};
            DeviceConfig local_cfg = get_config_snapshot();
            preview_cb_(blank, local_cfg);
        }
        estop_state_.store(EstopState::ReArmedBlackout, std::memory_order_release);
        return;
    } else if (es == EstopState::ReArmedBlackout) {
        // Frame N+1: Re-arms controller hardware, but keeps output blacked out (do NOT render/commit)
        controller_.clear_estop();
        if (preview_cb_) {
            RenderFrame blank{};
            DeviceConfig local_cfg = get_config_snapshot();
            preview_cb_(blank, local_cfg);
        }
        estop_state_.store(EstopState::Inactive, std::memory_order_release);
        ESP_LOGI(TAG, "EMERGENCY STOP RESET: Controller re-armed, resuming output on subsequent frame");
        return;
    }

    // 1. Snapshot shared state under short critical section (< 10 µs, Item 10)
    SceneConfig render_scene;
    DeviceConfig local_cfg;

    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }
    // Promote pending -> active at frame boundary under router/runtime lock (Item 5)
    if (live_pending_dirty_) {
        live_active_ = live_pending_;
        live_pending_dirty_ = false;
    }
    render_scene = scene_manager_.process_frame(current_ms);
    local_cfg = device_cfg_;
    xSemaphoreGive(mutex_);

    // 2. Snapshot control arbitration states (lock-free evaluation)
    ControlSnapshot ctrl_snapshot = source_manager_.get_snapshot(current_ms);

    // 3. Defensive rendering fail-safe timeout (Batch 5 Item 3: DmxRouter owns protocol/source timeout authority)
    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            auto& layer = live_active_.layers[z][l];
            if (layer.active) {
                if (current_ms - layer.timestamp_ms > kLiveFrameFailsafeTimeoutMs) {
                    layer.active = false;
                    layer.opacity = 0;
                }
            }
        }
    }

    for (uint8_t f = 0; f < 16; ++f) {
        auto& fix = live_active_.fixtures[f];
        if (fix.active) {
            if (current_ms - fix.timestamp_ms > kLiveFrameFailsafeTimeoutMs) {
                fix.active = false;
                fix.opacity = 0;
            }
        }
    }

    // 4. Render Pipeline (Completely lock-free!)
    RenderFrame& render_frame = current_render_frame_;
    for (auto& fixture : render_frame.fixtures) {
        fixture.pixels.fill(LightValue{0,0,0,0});
        fixture.pwm.fill(0);
    }

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = local_cfg.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                       fixture.profile == FixtureProfile::PWM_RGB ||
                       fixture.profile == FixtureProfile::PWM_RGBW);
        if (fixture.num_pixels == 0 && !is_pwm) continue;

        const uint8_t target_zone = fixture.zone_id;
        if (target_zone >= 8) continue;

        ControlSource fx_owner = ctrl_snapshot.resolve_fixture(f, target_zone, current_ms);

        if (fx_owner == ControlSource::LiveDmx && live_active_.fixtures[f].active && live_active_.fixtures[f].opacity > 0) {
            // Direct fixture DMX override takes precedence (Item 7)
            FixtureFrame live_src;
            const uint16_t pcount = std::min<uint16_t>(live_active_.fixtures[f].pixel_count, fixture.num_pixels);
            for (uint16_t p = 0; p < pcount; ++p) {
                live_src.pixels[p] = live_active_.fixtures[f].pixels[p];
            }
            live_src.pwm = live_active_.fixtures[f].pwm;
            FixtureMapper::map(render_frame.fixtures[f], live_src, fixture, render_scene.global_brightness);
        } else {
            // Zone compositing
            const ZoneContent& zone = render_scene.zones[target_zone];
            FixtureFrame& composited_zone = current_composited_zone_; 
            composited_zone.pixels.fill(LightValue{0,0,0,0});
            composited_zone.pwm.fill(0);

            for (uint8_t l = 0; l < 3; ++l) {
                ControlSource owner = ctrl_snapshot.resolve(target_zone, l, current_ms);
                FixtureFrame& layer_frame = current_layer_frame_;
                layer_frame.pixels.fill(LightValue{0,0,0,0});
                layer_frame.pwm.fill(0);

                if (owner == ControlSource::LiveDmx && live_active_.layers[target_zone][l].active) {
                    const auto& live = live_active_.layers[target_zone][l];
                    if (live.opacity == 0) continue;
                    
                    uint16_t pcount = std::min<uint16_t>(fixture.num_pixels, MAX_PIXELS);
                    for (uint16_t px = 0; px < pcount; ++px) {
                        layer_frame.pixels[px] = LightValue{live.color.r, live.color.g, live.color.b, 0};
                    }
                    layer_frame.pwm[0] = (static_cast<uint32_t>(live.color.r) * 4095) / 255;
                    
                    Compositor::blend(composited_zone, layer_frame, BlendMode::Normal, live.opacity, fixture.num_pixels);
                } else {
                    if (!zone.layers[l].enabled || zone.layers[l].opacity == 0) continue;
                    EffectRenderer::render_layer(layer_frame, zone.layers[l], fixture.num_pixels, current_ms);
                    Compositor::blend(composited_zone, layer_frame, zone.layers[l].blend_mode, zone.layers[l].opacity, fixture.num_pixels);
                }
            }

            FixtureMapper::map(render_frame.fixtures[f], composited_zone, fixture, render_scene.global_brightness);
        }
    }

    // 5. Power Limiting
    power_budget_.enforce(render_frame, local_cfg, current_ms);

    // 6. Physical Output Scheduling & Error Handling (Two-stage recovery, Batch 5 Items 1 & 2)
    if (output_fault_.load(std::memory_order_acquire)) {
        // While output_fault_ is true: runtime continues rendering internally,
        // BUT MUST NOT restore physical output automatically.
        controller_.blackout();
    } else {
        if (output_armed_for_next_frame_.load(std::memory_order_acquire)) {
            // First frame after fault clear: consume arm flag and allow commit
            output_armed_for_next_frame_.store(false, std::memory_order_release);
        }
        const esp_err_t sched_err = OutputScheduler::schedule(render_frame, local_cfg, controller_);
        if (sched_err != ESP_OK) {
            report_output_fault(sched_err);
            return;
        }
    }
    
    // 7. Logical RenderFrame Preview
    if (preview_cb_) {
        preview_cb_(render_frame, local_cfg);
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

bool LightingRuntime::submit_live_frame(const LiveDmxFrame& frame) {
    if (shutting_down_.load(std::memory_order_acquire)) return false;
    if (!mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;

    const uint32_t now = now_ms();
    live_pending_ = frame;
    live_pending_.timestamp_ms = now;
    live_pending_dirty_ = true;
    last_live_stream_ms_ = now;

    // Aggregate ownership calculation
    uint8_t live_zone_mask = 0;
    uint8_t live_layer_mask = 0;
    uint16_t live_fixture_mask = 0;

    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            if (live_pending_.layers[z][l].active) {
                live_zone_mask |= (1 << z);
                live_layer_mask |= (1 << l);
            }
        }
    }
    for (uint8_t f = 0; f < 16; ++f) {
        if (live_pending_.fixtures[f].active) {
            live_fixture_mask |= (1 << f);
        }
    }

    if (live_zone_mask == 0 && live_fixture_mask == 0) {
        source_manager_.release(ControlSource::LiveDmx);
    } else {
        source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, live_zone_mask, live_layer_mask, live_fixture_mask);
    }
    xSemaphoreGive(mutex_);
    return true;
}

esp_err_t LightingRuntime::clear_output_fault() {
    if (controller_.is_estop_latched() || is_estop_active()) {
        ESP_LOGW(TAG, "Cannot clear output fault: E-stop is currently active/latched");
        return ESP_ERR_INVALID_STATE;
    }
    // Stage 1: Reset fault latch, force blackout, and arm output awaiting next clean frame (Batch 5 Item 2)
    output_fault_.store(false, std::memory_order_release);
    controller_.blackout();
    output_armed_for_next_frame_.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "Output fault latch cleared: output armed awaiting next clean render frame");
    return ESP_OK;
}

DeviceConfig LightingRuntime::get_config_snapshot() const {
    DeviceConfig cfg{};
    if (!mutex_) return cfg;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    cfg = device_cfg_;
    xSemaphoreGive(mutex_);
    return cfg;
}

void LightingRuntime::inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity) {
    if (zone_idx >= 8 || layer_idx >= 3) return;
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t now = now_ms();
    live_pending_.layers[zone_idx][layer_idx].color = color;
    live_pending_.layers[zone_idx][layer_idx].opacity = opacity;
    live_pending_.layers[zone_idx][layer_idx].timestamp_ms = now;
    live_pending_.layers[zone_idx][layer_idx].active = true;
    live_pending_dirty_ = true;
    last_live_stream_ms_ = now;

    source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, (1 << zone_idx), (1 << layer_idx), 0);
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
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t now = now_ms();
    auto& lf = live_pending_.fixtures[fixture_idx];
    uint16_t pcount = std::min<uint16_t>(count, MAX_PIXELS);
    lf.pixel_count = pcount;
    if (pixels && pcount > 0) {
        std::memcpy(lf.pixels.data(), pixels, pcount * sizeof(LightValue));
    }
    if (pwm_duties) {
        for (uint8_t ch = 0; ch < 4; ++ch) lf.pwm[ch] = pwm_duties[ch];
    }
    lf.opacity = opacity;
    lf.timestamp_ms = now;
    lf.active = true;
    live_pending_dirty_ = true;
    last_live_stream_ms_ = now;

    source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, 0, 0, (1 << fixture_idx));
    xSemaphoreGive(mutex_);
}

std::array<uint8_t, 512> LightingRuntime::last_dmx_data() const {
    if (!mutex_) return {};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    std::array<uint8_t, 512> dmx = live_active_.dmx_telemetry;
    xSemaphoreGive(mutex_);
    return dmx;
}

uint8_t LightingRuntime::active_live_objects() const {
    uint8_t count = 0;
    for (uint8_t z = 0; z < 8; ++z) {
        for (uint8_t l = 0; l < 3; ++l) {
            if (live_active_.layers[z][l].active) count++;
        }
    }
    for (uint8_t f = 0; f < 16; ++f) {
        if (live_active_.fixtures[f].active) count++;
    }
    return count;
}

void LightingRuntime::feed_watchdog() {
    if (!mutex_) return;
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
    DeviceConfig candidate{};
    esp_err_t err = config_.load_config(candidate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config reload aborted: failed to load candidate from storage (%s)", esp_err_to_name(err));
        return;
    }

    ConfigStore::ConfigValidationResult val_res;
    err = ConfigStore::validate_config(candidate, &val_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config reload aborted: candidate validation failed (%u errors): %s",
                 (unsigned)val_res.error_count, val_res.first_error);
        return;
    }

    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    device_cfg_ = candidate;
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
    ColorPipeline::set_gamma(device_cfg_.gamma_x10);
    scene_manager_.update_device_config(device_cfg_);
    if (config_reload_cb_) {
        config_reload_cb_(device_cfg_);
    }
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Device config transactional reload applied successfully");
}

bool LightingRuntime::acquire_control(ControlSource source, uint32_t lease_ms, uint8_t zone_mask, uint8_t layer_mask) {
    if (source == ControlSource::EStop) {
        trigger_estop();
    }
    if (!mutex_) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool won = source_manager_.acquire(source, lease_ms, zone_mask, layer_mask);
    xSemaphoreGive(mutex_);
    return won;
}

void LightingRuntime::release_control(ControlSource source) {
    if (source == ControlSource::EStop) {
        clear_estop();
    }
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    source_manager_.release(source);
    xSemaphoreGive(mutex_);
}

ControlSource LightingRuntime::current_control_source() const {
    return source_manager_.current_owner();
}

void LightingRuntime::report_output_fault(esp_err_t err) {
    output_armed_for_next_frame_.store(false, std::memory_order_release);
    output_fault_.store(true, std::memory_order_release);
    output_fault_count_.fetch_add(1, std::memory_order_relaxed);
    controller_.blackout();
}

} // namespace lg
