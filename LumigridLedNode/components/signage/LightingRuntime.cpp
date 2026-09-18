// components/signage/LightingRuntime.cpp
#include "LightingRuntime.hpp"
#include "ChannelHardware.hpp"
#include "EffectRenderer.hpp"
#include "Compositor.hpp"
#include "FixtureMapper.hpp"
#include "OutputScheduler.hpp"
#include "ColorPipeline.hpp"
#include "SceneTransition.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

static const char* TAG = "LightingRuntime";

namespace lg {
namespace {
inline uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
}

LightingRuntime::LightingRuntime(ControlSourceManager& source_manager, OutputController& controller, ConfigStore& config)
    : source_manager_(source_manager), controller_(controller), config_(config), scene_manager_(config) {
    mutex_ = xSemaphoreCreateMutex();
    shutdown_sem_ = xSemaphoreCreateBinary();
    controller_.set_commit_observer([this](esp_err_t commit_result) {
        if (commit_result != ESP_OK) report_output_fault(commit_result);
        else if (output_state_.load(std::memory_order_acquire) == OutputState::Armed) {
            output_state_.store(OutputState::Healthy, std::memory_order_release);
            ESP_LOGI(TAG, "Armed commit succeeded: output state restored to HEALTHY");
        }
    });
}

LightingRuntime::~LightingRuntime() {
    controller_.set_commit_observer(nullptr);
    stop();
    // Destruction is terminal. If bounded stop timed out, wait for the cooperative
    // task to acknowledge exit before deleting the semaphore/mutex it can touch.
    if (task_handle_) {
        if (shutdown_sem_) xSemaphoreTake(shutdown_sem_, portMAX_DELAY);
        task_handle_ = nullptr;
        controller_.blackout();
        if (mutex_) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
        lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
    }
    if (shutdown_sem_) {
        vSemaphoreDelete(shutdown_sem_);
        shutdown_sem_ = nullptr;
    }
}

void LightingRuntime::stop() {
    LifecycleState prev = lifecycle_state_.exchange(LifecycleState::Stopping, std::memory_order_acq_rel);
    if (prev == LifecycleState::Stopping || prev == LifecycleState::Stopped) return;
    shutting_down_.store(true, std::memory_order_release);
    if (task_handle_) {
        xTaskAbortDelay(task_handle_);
        bool ack = false;
        if (shutdown_sem_) ack = (xSemaphoreTake(shutdown_sem_, pdMS_TO_TICKS(1000)) == pdTRUE);
        if (!ack) {
            ESP_LOGE(TAG, "LightingRuntime task did not exit within 1000ms timeout! Halting cleanup to prevent UAF");
            lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
            controller_.set_commit_observer(nullptr);
            return;
        }
        task_handle_ = nullptr;
    }
    controller_.set_commit_observer(nullptr);
    controller_.blackout();
    if (mutex_) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        live_pending_ = {};
        live_active_ = {};
        live_pending_dirty_ = true;
        last_live_stream_ms_ = 0;
        source_manager_.release(ControlSource::LiveDmx);
        source_manager_.release(ControlSource::ScenePlayback);
        source_manager_.release(ControlSource::Factory);
        // Do NOT give the mutex before deleting it. The task is confirmed dead
        // (shutdown_sem_ was acked). Giving first would allow any concurrent waiter
        // that slipped in to grab the semaphore and then touch the deleted object
        // (UAF). Delete directly while holding it — FreeRTOS allows this.
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
}

esp_err_t LightingRuntime::start() {
    LifecycleState current = lifecycle_state_.load(std::memory_order_acquire);
    if (current == LifecycleState::Running || current == LifecycleState::Starting || current == LifecycleState::Stopping) {
        ESP_LOGW(TAG, "LightingRuntime start() rejected: lifecycle transition already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (current == LifecycleState::Faulted && task_handle_ != nullptr) {
        ESP_LOGW(TAG, "LightingRuntime start() rejected: previous task has not terminated");
        return ESP_ERR_INVALID_STATE;
    }
    lifecycle_state_.store(LifecycleState::Starting, std::memory_order_release);
    shutting_down_.store(false, std::memory_order_release);
    controller_.set_commit_observer([this](esp_err_t commit_result) {
        if (commit_result != ESP_OK) report_output_fault(commit_result);
        else if (output_state_.load(std::memory_order_acquire) == OutputState::Armed) {
            output_state_.store(OutputState::Healthy, std::memory_order_release);
            ESP_LOGI(TAG, "Armed commit succeeded: output state restored to HEALTHY");
        }
    });
    config_.load_config(device_cfg_);
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
    ColorPipeline::set_gamma(device_cfg_.gamma_x10);
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) {
            ESP_LOGE(TAG, "Failed to create mutex");
            controller_.set_commit_observer(nullptr);
            lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
            return ESP_ERR_NO_MEM;
        }
    }
    if (!shutdown_sem_) shutdown_sem_ = xSemaphoreCreateBinary();
    scene_manager_.init();
    BaseType_t ret = xTaskCreatePinnedToCore(task_entry, "lighting_rt", 6144, this, 6, &task_handle_, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        controller_.set_commit_observer(nullptr);
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        return ESP_FAIL;
    }
    lifecycle_state_.store(LifecycleState::Running, std::memory_order_release);
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

void LightingRuntime::task_entry(void* arg) { static_cast<LightingRuntime*>(arg)->task(); }

void LightingRuntime::task() {
    TickType_t last_wake = xTaskGetTickCount();
    enable_ms_ = now_ms();
    power_budget_.on_enable();
    while (!shutting_down_.load(std::memory_order_acquire)) {
        uint32_t current_ms = now_ms();
        if (maintenance_cb_) maintenance_cb_(current_ms);
        render_frame(current_ms);
        frame_count_++;
        if (current_ms - last_fps_time_ms_ >= 1000) {
            current_fps_ = (frame_count_ * 1000.0f) / (current_ms - last_fps_time_ms_);
            frame_count_ = 0;
            last_fps_time_ms_ = current_ms;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kFramePeriodMs));
    }
    if (shutdown_sem_) xSemaphoreGive(shutdown_sem_);
    vTaskDelete(NULL);
}

void LightingRuntime::render_frame(uint32_t current_ms) {
    const EstopState es = estop_state_.load(std::memory_order_acquire);
    if (es == EstopState::Tripped) {
        controller_.blackout();
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        return;
    } else if (es == EstopState::PendingReset) {
        controller_.blackout();
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        estop_state_.store(EstopState::ReArmedBlackout, std::memory_order_release);
        return;
    } else if (es == EstopState::ReArmedBlackout) {
        controller_.clear_estop();
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        estop_state_.store(EstopState::Inactive, std::memory_order_release);
        ESP_LOGI(TAG, "EMERGENCY STOP RESET: Controller re-armed, resuming output on subsequent frame");
        return;
    }

    uint32_t render_scene_generation = 0;
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;
    if (live_pending_dirty_) { live_active_ = live_pending_; live_pending_dirty_ = false; }
    current_render_scene_ = scene_manager_.process_frame(current_ms);
    current_local_cfg_ = device_cfg_;
    render_scene_generation = requested_scene_generation_.load(std::memory_order_acquire);
    xSemaphoreGive(mutex_);

    const SceneConfig& render_scene = current_render_scene_;
    const DeviceConfig& local_cfg = current_local_cfg_;

    ControlSnapshot ctrl_snapshot = source_manager_.get_snapshot(current_ms);
    auto live_channel_is_valid = [current_ms](const LiveDmxChannel& ch) -> bool {
        if (!ch.active || ch.opacity == 0) return false;
        return current_ms - ch.timestamp_ms <= kLiveFrameFailsafeTimeoutMs;
    };
    RenderFrame& render_frame = current_render_frame_;
    for (auto& ch_frame : render_frame.channels) {
        ch_frame.pixels.fill(LightValue{0, 0, 0, 0});
        ch_frame.pwm = 0;
    }

    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& channel_cfg = local_cfg.channels[ch];
        if (!channel_cfg.enabled) continue;

        const auto hw = channel_hardware(ch);
        const bool is_addressable = (hw.kind == ChannelKind::Addressable);
        const uint16_t num_pixels = is_addressable ? std::min<uint16_t>(channel_cfg.pixel_count, MAX_PIXELS) : 1;

        ControlSource ch_owner = ctrl_snapshot.resolve_channel(ch, current_ms);
        if (ch_owner == ControlSource::LiveDmx && live_channel_is_valid(live_active_.channels[ch])) {
            ChannelFrame& live_src = current_live_src_;
            const uint16_t pcount = std::min<uint16_t>(live_active_.channels[ch].pixel_count, num_pixels);
            for (uint16_t p = 0; p < pcount; ++p) live_src.pixels[p] = live_active_.channels[ch].pixels[p];
            live_src.pwm = live_active_.channels[ch].pwm;

            if (is_addressable) {
                for (uint16_t px = 0; px < num_pixels; ++px) {
                    render_frame.channels[ch].pixels[px] = ColorPipeline::to_addressable(live_src.pixels[px], channel_cfg, render_scene.global_brightness);
                }
            } else {
                render_frame.channels[ch].pwm = ColorPipeline::to_pwm(live_src.pixels[0], live_src.pwm, channel_cfg, render_scene.global_brightness);
            }
        } else {
            const ChannelContent& content = render_scene.channels[ch];
            ChannelFrame& composited_channel = current_composited_zone_;
            composited_channel.pixels.fill(LightValue{0, 0, 0, 0});
            composited_channel.pwm = 0;

            const bool is_trans = scene_manager_.is_transitioning();
            const uint16_t t_q15 = scene_manager_.transition_progress_q15();
            const auto& from_content = scene_manager_.source_scene().channels[ch];
            const auto& to_content = scene_manager_.target_scene().channels[ch];

            for (uint8_t l = 0; l < 3; ++l) {
                ChannelFrame& layer_frame = current_layer_frame_;
                layer_frame.pixels.fill(LightValue{0, 0, 0, 0});
                layer_frame.pwm = 0;

                if (is_trans) {
                    const LayerConfig& from_l = from_content.layers[l];
                    const LayerConfig& to_l = to_content.layers[l];

                    if (can_parameter_morph(from_l, to_l)) {
                        LayerConfig morph = sample_layer_morph(from_l, to_l, t_q15);
                        if (morph.enabled && morph.opacity > 0) {
                            EffectRenderer::render_layer(layer_frame, morph, num_pixels, current_ms);
                            Compositor::blend(composited_channel, layer_frame, morph.blend_mode, morph.opacity, num_pixels);
                        }
                    } else {
                        ChannelFrame& from_frame = layer_frame;
                        ChannelFrame& to_frame = transition_layer_frame_;
                        from_frame.pixels.fill(LightValue{0, 0, 0, 0}); from_frame.pwm = 0;
                        to_frame.pixels.fill(LightValue{0, 0, 0, 0}); to_frame.pwm = 0;

                        if (from_l.enabled && from_l.opacity > 0) {
                            EffectRenderer::render_layer(from_frame, from_l, num_pixels, current_ms);
                        }
                        if (to_l.enabled && to_l.opacity > 0) {
                            EffectRenderer::render_layer(to_frame, to_l, num_pixels, current_ms);
                        }
                        blend_frames(from_frame, to_frame, t_q15, num_pixels);

                        BlendMode blend_m = (t_q15 < 16384) ? from_l.blend_mode : to_l.blend_mode;
                        uint8_t op = lerp_q15_u8(from_l.opacity, to_l.opacity, t_q15);
                        if (op > 0) {
                            Compositor::blend(composited_channel, from_frame, blend_m, op, num_pixels);
                        }
                    }
                } else {
                    if (!content.layers[l].enabled || content.layers[l].opacity == 0) continue;
                    EffectRenderer::render_layer(layer_frame, content.layers[l], num_pixels, current_ms);
                    Compositor::blend(composited_channel, layer_frame, content.layers[l].blend_mode, content.layers[l].opacity, num_pixels);
                }
            }

            if (is_addressable) {
                for (uint16_t px = 0; px < num_pixels; ++px) {
                    render_frame.channels[ch].pixels[px] = ColorPipeline::to_addressable(composited_channel.pixels[px], channel_cfg, render_scene.global_brightness);
                }
            } else {
                uint16_t pwm_val = ColorPipeline::to_pwm(composited_channel.pixels[0], composited_channel.pwm, channel_cfg, render_scene.global_brightness);
                render_frame.channels[ch].pwm = pwm_val;
                if (ch >= 8 && ch < 16) {
                    last_pwm_duty_[ch - 8] = pwm_val;
                }
            }
        }
    }
    power_budget_.enforce(render_frame, local_cfg, current_ms);
    OutputState state = output_state_.load(std::memory_order_acquire);
    if (state == OutputState::Faulted) {
        output_fault_frame_count_.fetch_add(1, std::memory_order_relaxed);
        controller_.blackout();
    } else {
        bool frame_committed = false;
        ControlSource dominant = source_manager_.current_owner();
        if (dominant != ControlSource::Factory && dominant != ControlSource::EStop) {
            const esp_err_t sched_err = OutputScheduler::schedule(render_frame, local_cfg, controller_);
            if (sched_err != ESP_OK) { report_output_fault(sched_err); return; }
            frame_committed = true;
        }
        if (state == OutputState::Armed) {
            output_state_.store(OutputState::Healthy, std::memory_order_release);
            ESP_LOGI(TAG, "Armed commit succeeded: output state restored to HEALTHY");
        }

        // Do not acknowledge merely loading a scene. Only advance the applied
        // generation after OutputScheduler has completed a successful hardware
        // transaction for a frame that observed that request generation.
        if (frame_committed && render_scene_generation != 0) {
            const uint32_t applied =
                applied_scene_generation_.load(std::memory_order_acquire);
            if (render_scene_generation > applied) {
                applied_scene_generation_.store(
                    render_scene_generation,
                    std::memory_order_release
                );
            }
        }
    }

    if (preview_cb_) {
        try {
            preview_cb_(render_frame, local_cfg);
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "preview_cb exception: %s", e.what());
        } catch (...) {
            ESP_LOGW(TAG, "preview_cb unknown exception");
        }
    }
}

void LightingRuntime::activate_scene(uint8_t index) { xSemaphoreTake(mutex_, portMAX_DELAY); scene_manager_.activate_scene(index, now_ms()); xSemaphoreGive(mutex_); }
void LightingRuntime::activate_scene_direct(const SceneConfig& scene, bool immediate) { xSemaphoreTake(mutex_, portMAX_DELAY); scene_manager_.activate_scene_direct(scene, now_ms(), immediate); xSemaphoreGive(mutex_); }

bool LightingRuntime::submit_live_frame(const LiveDmxFrame& frame) {
    if (shutting_down_.load(std::memory_order_acquire) || !mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    const uint32_t now = now_ms();
    live_pending_ = frame; live_pending_.timestamp_ms = now; live_pending_dirty_ = true; last_live_stream_ms_ = now;
    if (!task_handle_) { live_active_ = live_pending_; live_pending_dirty_ = false; }
    uint8_t live_zone_mask = 0, live_layer_mask = 0; uint16_t live_channel_mask = 0;
    for (uint8_t z = 0; z < 8; ++z) for (uint8_t l = 0; l < 3; ++l) if (live_pending_.layers[z][l].active) { live_zone_mask |= (1 << z); live_layer_mask |= (1 << l); }
    for (uint8_t ch = 0; ch < 16; ++ch) if (live_pending_.channels[ch].active) live_channel_mask |= (1 << ch);
    if (live_zone_mask == 0 && live_channel_mask == 0) source_manager_.release(ControlSource::LiveDmx);
    else source_manager_.acquire(ControlSource::LiveDmx, 0, live_zone_mask, live_layer_mask, live_channel_mask);
    xSemaphoreGive(mutex_); return true;
}

esp_err_t LightingRuntime::clear_output_fault() {
    if (controller_.is_estop_latched() || is_estop_active()) return ESP_ERR_INVALID_STATE;
    controller_.blackout(); output_state_.store(OutputState::Armed, std::memory_order_release);
    ESP_LOGI(TAG, "Output fault cleared: output state is ARMED awaiting next rendered frame commit");
    return ESP_OK;
}

DeviceConfig LightingRuntime::get_config_snapshot() const {
    DeviceConfig cfg{};
    if (!mutex_) {
        return cfg;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    cfg = device_cfg_;
    xSemaphoreGive(mutex_);
    return cfg;
}

void LightingRuntime::inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity) {
    if (zone_idx >= 8 || layer_idx >= 3 || !mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY); const uint32_t now = now_ms();
    live_pending_.layers[zone_idx][layer_idx].color = color; live_pending_.layers[zone_idx][layer_idx].opacity = opacity;
    live_pending_.layers[zone_idx][layer_idx].timestamp_ms = now; live_pending_.layers[zone_idx][layer_idx].active = true;
    live_pending_dirty_ = true; last_live_stream_ms_ = now;
    source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, (1 << zone_idx), (1 << layer_idx), 0);
    xSemaphoreGive(mutex_);
}

void LightingRuntime::inject_live_fixture(uint8_t fixture_idx, const LightValue* pixels, uint16_t count, const uint16_t* pwm_duties, uint8_t opacity) {
    if (fixture_idx >= 16 || !mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY); const uint32_t now = now_ms(); auto& lf = live_pending_.channels[fixture_idx];
    uint16_t pcount = std::min<uint16_t>(count, MAX_PIXELS); lf.pixel_count = pcount;
    if (pixels && pcount > 0) std::memcpy(lf.pixels.data(), pixels, pcount * sizeof(LightValue));
    if (pwm_duties) {
        lf.pwm = pwm_duties[0];
    }
    lf.opacity = opacity; lf.timestamp_ms = now; lf.active = true; live_pending_dirty_ = true; last_live_stream_ms_ = now;
    source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, 0, 0, (1 << fixture_idx));
    xSemaphoreGive(mutex_);
}

std::array<uint8_t, 512> LightingRuntime::last_dmx_data() const {
    if (!mutex_) {
        return {};
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto dmx = live_active_.dmx_telemetry;
    xSemaphoreGive(mutex_);
    return dmx;
}

uint8_t LightingRuntime::active_live_objects() const {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return 0;
    uint8_t count = 0; const uint32_t now = now_ms();
    for (uint8_t z = 0; z < 8; ++z) for (uint8_t l = 0; l < 3; ++l) { const auto& layer = live_active_.layers[z][l]; if (layer.active && layer.opacity > 0 && now - layer.timestamp_ms <= kLiveFrameFailsafeTimeoutMs) count++; }
    for (uint8_t ch = 0; ch < 16; ++ch) { const auto& ch_obj = live_active_.channels[ch]; if (ch_obj.active && ch_obj.opacity > 0 && now - ch_obj.timestamp_ms <= kLiveFrameFailsafeTimeoutMs) count++; }
    xSemaphoreGive(mutex_);
    return count;
}

void LightingRuntime::feed_watchdog() {
    if (!mutex_) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    scene_manager_.feed_watchdog(now_ms());
    xSemaphoreGive(mutex_);
}
uint8_t LightingRuntime::active_scene_index() const { return scene_manager_.active_scene_index(); }
bool LightingRuntime::is_fallback_active() const { return scene_manager_.is_fallback_active(); }
bool LightingRuntime::is_transitioning() const { return scene_manager_.is_transitioning(); }
void LightingRuntime::set_beat_period(uint16_t period_ms) {
    if (!mutex_) {
        return;
    }
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
        ESP_LOGE(TAG, "Config reload aborted: candidate validation failed (%u errors): %s", (unsigned)val_res.error_count, val_res.first_error);
        return;
    }
    if (!mutex_) {
        return;
    }
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
    if (!mutex_) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool won = source_manager_.acquire(source, lease_ms, zone_mask, layer_mask);
    xSemaphoreGive(mutex_);
    return won;
}

void LightingRuntime::release_control(ControlSource source) {
    if (source == ControlSource::EStop) {
        clear_estop();
    }
    if (!mutex_) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    source_manager_.release(source);
    xSemaphoreGive(mutex_);
}

ControlSource LightingRuntime::current_control_source() const { return source_manager_.current_owner(); }

void LightingRuntime::report_output_fault(esp_err_t err) {
    output_commit_failure_count_.fetch_add(1, std::memory_order_relaxed);
    OutputState prev = output_state_.exchange(OutputState::Faulted, std::memory_order_acq_rel);
    if (prev != OutputState::Faulted) { output_fault_count_.fetch_add(1, std::memory_order_relaxed); ESP_LOGE(TAG, "Hardware output transitioned to FAULTED (err=%s)", esp_err_to_name(err)); }
    controller_.blackout();
}

uint16_t LightingRuntime::transition_progress_q15() const {
    return scene_manager_.transition_progress_q15();
}

TransitionCurve LightingRuntime::transition_curve() const {
    return scene_manager_.transition_curve();
}

void LightingRuntime::set_transition_curve(TransitionCurve curve) {
    scene_manager_.set_transition_curve(curve);
}

} // namespace lg
