// components/signage/LightingRuntime.cpp
#include "LightingRuntime.hpp"
#include "ChannelHardware.hpp"
#include "EffectRenderer.hpp"
#include "Compositor.hpp"
#include "FixtureMapper.hpp"
#include "OutputScheduler.hpp"
#include "ColorPipeline.hpp"
#include "SceneTransition.hpp"
#include "ParallelRmtDriver.hpp"
#include "OutputCoreExecutor.hpp"
#include "BoardOutputSafety.hpp"
#include "ResetForensics.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>
#include <cinttypes>
#include <new>

static const char* TAG = "LightingRuntime";

namespace lg {
namespace {
inline uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
}

LightingRuntime::LightingRuntime(ControlSourceManager& source_manager, OutputController& controller, ConfigStore& config)
    : source_manager_(source_manager), controller_(controller), config_(config), scene_manager_(config) {
    live_pending_.reset(new (std::nothrow) LiveDmxFrame{});
    live_active_.reset(new (std::nothrow) LiveDmxFrame{});
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
    stop();
    // Destruction is terminal. If bounded stop timed out, wait for the cooperative
    // task to acknowledge exit before deleting the semaphore/mutex it can touch.
    if (task_handle_) {
        if (shutdown_sem_) xSemaphoreTake(shutdown_sem_, portMAX_DELAY);
        task_handle_ = nullptr;
        stop();
    }
    controller_.set_commit_observer(nullptr);
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (shutdown_sem_) {
        vSemaphoreDelete(shutdown_sem_);
        shutdown_sem_ = nullptr;
    }
}

esp_err_t LightingRuntime::stop() {
    LifecycleState current = lifecycle_state_.load(std::memory_order_acquire);
    for (;;) {
        if (current == LifecycleState::Stopped) return ESP_OK;
        if (current == LifecycleState::Starting || current == LifecycleState::Stopping) return ESP_ERR_INVALID_STATE;
        if (lifecycle_state_.compare_exchange_weak(current, LifecycleState::Stopping, std::memory_order_acq_rel)) break;
    }
    if (task_handle_ && xTaskGetCurrentTaskHandle() == task_handle_) {
        lifecycle_state_.store(current, std::memory_order_release);
        return ESP_ERR_INVALID_STATE;
    }
    control_epoch_.fetch_add(1, std::memory_order_acq_rel);
    shutting_down_.store(true, std::memory_order_release);
    if (task_handle_) {
        xTaskAbortDelay(task_handle_);
        bool ack = false;
        if (shutdown_sem_) ack = (xSemaphoreTake(shutdown_sem_, pdMS_TO_TICKS(1000)) == pdTRUE);
        if (!ack) {
            ESP_LOGE(TAG, "LightingRuntime task did not exit within 1000ms timeout! Halting cleanup to prevent UAF");
            lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
            return ESP_ERR_TIMEOUT;
        }
        task_handle_ = nullptr;
    }
    controller_.blackout();
    if (mutex_) {
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
            lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
            return ESP_ERR_TIMEOUT;
        }
        if (live_pending_) (*live_pending_) = {};
        if (live_active_) (*live_active_) = {};
        live_pending_dirty_ = true;
        last_live_stream_ms_ = 0;
        source_manager_.release(ControlSource::LiveDmx);
        source_manager_.release(ControlSource::ScenePlayback);
        source_manager_.release(ControlSource::Factory);
        source_manager_.release(ControlSource::ManualOverride);
        manual_active_ = {};
        // The render task has exited, but API/DMX callers can still be waiting
        // here. Keep the semaphore alive and let each waiter reject shutdown
        // after acquiring it. A subsequent start reuses the same semaphore.
        xSemaphoreGive(mutex_);
    }
    controller_.set_commit_observer(nullptr);
    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
    return ESP_OK;
}

esp_err_t LightingRuntime::start() {
    LifecycleState current = lifecycle_state_.load(std::memory_order_acquire);
    if (current == LifecycleState::Running || current == LifecycleState::Starting || current == LifecycleState::Stopping) {
        ESP_LOGW(TAG, "LightingRuntime start() rejected: lifecycle transition already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (!lifecycle_state_.compare_exchange_strong(current, LifecycleState::Starting, std::memory_order_acq_rel)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current == LifecycleState::Faulted && (task_handle_ != nullptr || shutting_down_.load(std::memory_order_acquire))) {
        ESP_LOGW(TAG, "LightingRuntime start() rejected: previous task has not terminated");
        lifecycle_state_.store(current, std::memory_order_release);
        return ESP_ERR_INVALID_STATE;
    }
    control_epoch_.fetch_add(1, std::memory_order_acq_rel);
    if (!live_pending_ || !live_active_) {
        ESP_LOGE(TAG, "Live DMX frame allocation failed; refusing lighting startup");
        controller_.blackout();
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    shutting_down_.store(false, std::memory_order_release);
    controller_.set_commit_observer([this](esp_err_t commit_result) {
        if (commit_result != ESP_OK) report_output_fault(commit_result);
        else if (output_state_.load(std::memory_order_acquire) == OutputState::Armed) {
            output_state_.store(OutputState::Healthy, std::memory_order_release);
            ESP_LOGI(TAG, "Armed commit succeeded: output state restored to HEALTHY");
        }
    });
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
    if (!shutdown_sem_) {
        controller_.set_commit_observer(nullptr);
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    // The recovery API remains available across stop/start. Publish the new
    // configuration and scene state under the same stable mutex as its readers.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_err_t init_err = config_.load_config(device_cfg_);
    if (init_err == ESP_OK) {
        power_budget_.set_limit_mw(device_cfg_.max_power_mw);
        power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
        ColorPipeline::set_gamma(device_cfg_.gamma_x10);
        init_err = scene_manager_.init();
    }
    if (init_err == ESP_OK) {
        last_observed_topology_generation_ = controller_.topology_generation();
        restart_soft_start_locked();
    }
    xSemaphoreGive(mutex_);
    if (init_err != ESP_OK) {
        controller_.set_commit_observer(nullptr);
        controller_.blackout();
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        return init_err;
    }
    // LG-HW-001: Pinned to CPU1 (core 1).
    // Architecture:
    //   CPU0 = network/system side
    //   CPU1 = deterministic lighting/output side
    // This is intentional system architecture, not a random optimization.
    BaseType_t ret = xTaskCreatePinnedToCore(task_entry, "lighting_rt", 10240, this, 6, &task_handle_, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        controller_.set_commit_observer(nullptr);
        lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
        return ESP_FAIL;
    }
    lifecycle_state_.store(LifecycleState::Running, std::memory_order_release);
    ESP_LOGI(TAG, "LightingRuntime started (gamma=%.1f, max_mw=%" PRIu32 ")", device_cfg_.gamma_x10 / 10.0f, static_cast<uint32_t>(device_cfg_.max_power_mw));
    return ESP_OK;
}

void LightingRuntime::trigger_estop() {
    controller_.latch_estop();
    portENTER_CRITICAL(&estop_state_mux_);
    estop_state_.store(EstopState::Tripped, std::memory_order_release);
    portEXIT_CRITICAL(&estop_state_mux_);
    source_manager_.release(ControlSource::Factory);
    clear_live_frame();
    clear_manual_override();
    controller_.blackout();
    ESP_LOGW(TAG, "EMERGENCY STOP TRIGGERED: Hardware output blacked out and latched");
}

void LightingRuntime::clear_live_frame() {
    if (!mutex_ || !live_pending_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    (*live_pending_) = {};
    live_pending_dirty_ = true;
    last_live_stream_ms_ = 0;
    source_manager_.release(ControlSource::LiveDmx);
    xSemaphoreGive(mutex_);
}

void LightingRuntime::clear_manual_override() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    manual_active_ = {};
    source_manager_.release(ControlSource::ManualOverride);
    xSemaphoreGive(mutex_);
}

bool LightingRuntime::submit_manual_override(const ManualOverrideFrame& frame, uint32_t lease_ms) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (shutting_down_.load(std::memory_order_acquire) || !mutex_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return false; }

    // Allowed lease range: 1,000–60,000 ms (default 15,000 ms)
    if (lease_ms < 1000) lease_ms = 1000;
    if (lease_ms > 60000) lease_ms = 60000;

    const uint32_t now = now_ms();
    manual_active_ = frame;
    manual_active_.timestamp_ms = now;
    manual_active_.lease_ms = lease_ms;

    uint16_t mask = 0;
    for (uint8_t ch = 0; ch < 16; ++ch) {
        if (manual_active_.channels[ch].active) {
            mask |= (1U << ch);
        }
    }
    manual_active_.channel_mask = mask;

    if (mask == 0) {
        source_manager_.release(ControlSource::ManualOverride);
    } else {
        source_manager_.acquire(ControlSource::ManualOverride, lease_ms, 0, 0, mask);
    }

    xSemaphoreGive(mutex_);
    return true;
}

ManualControlStatus LightingRuntime::manual_control_status() const {
    ManualControlStatus status{};
    if (!mutex_) return status;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t now = now_ms();
    ControlSnapshot snap = const_cast<ControlSourceManager&>(source_manager_).get_snapshot(now);
    const uint8_t idx = static_cast<uint8_t>(ControlSource::ManualOverride);
    const auto& src_state = snap.states[idx];

    if (src_state.active && (src_state.expires_ms == 0 || now < src_state.expires_ms) && src_state.channel_mask != 0) {
        status.active = true;
        status.channel_mask = src_state.channel_mask;
        status.lease_remaining_ms = (src_state.expires_ms > now) ? (src_state.expires_ms - now) : 0;
    }
    xSemaphoreGive(mutex_);
    return status;
}

void LightingRuntime::clear_estop() {
    portENTER_CRITICAL(&estop_state_mux_);
    if (estop_state_.load(std::memory_order_relaxed) == EstopState::Tripped) {
        estop_reset_generation_ = controller_.estop_generation();
        estop_state_.store(EstopState::PendingReset, std::memory_order_release);
    }
    portEXIT_CRITICAL(&estop_state_mux_);
}

void LightingRuntime::task_entry(void* arg) { static_cast<LightingRuntime*>(arg)->task(); }

void LightingRuntime::task() {
    TickType_t next_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(kFramePeriodMs);

    uint32_t loop_count = 0;
    uint32_t commit_count = 0;
    uint32_t last_fps_calc_ms = now_ms();

    while (!shutting_down_.load(std::memory_order_acquire)) {
        const int64_t t_frame_start = esp_timer_get_time();
        const uint32_t current_ms = static_cast<uint32_t>(t_frame_start / 1000);

        if (maintenance_cb_) maintenance_cb_(current_ms);

        FrameResult result = render_frame(current_ms);
        loop_count++;

        if (result == FrameResult::Committed) {
            commit_count++;
        } else if (result == FrameResult::Failed) {
            frame_metrics_.failed_frames++;
        }

        const int64_t t_frame_end = esp_timer_get_time();
        const uint32_t frame_us = static_cast<uint32_t>(t_frame_end - t_frame_start);
        frame_metrics_.last_frame_us = frame_us;
        if (frame_us > frame_metrics_.max_frame_us) {
            frame_metrics_.max_frame_us = frame_us;
        }

        if (current_ms - last_fps_calc_ms >= 1000) {
            const float dt = (current_ms - last_fps_calc_ms) / 1000.0f;
            frame_metrics_.loop_fps = loop_count / dt;
            frame_metrics_.committed_fps = commit_count / dt;
            current_fps_ = frame_metrics_.committed_fps;
            loop_count = 0;
            commit_count = 0;
            last_fps_calc_ms = current_ms;
        }

        TickType_t now_ticks = xTaskGetTickCount();
        if (now_ticks >= next_deadline) {
            frame_metrics_.deadline_misses++;
            TickType_t overrun_ticks = now_ticks - next_deadline;
            uint32_t dropped = static_cast<uint32_t>(overrun_ticks / pdMS_TO_TICKS(kFramePeriodMs)) + 1;
            frame_metrics_.dropped_periods += dropped;
            next_deadline = now_ticks + pdMS_TO_TICKS(kFramePeriodMs);
        } else {
            vTaskDelay(next_deadline - now_ticks);
            next_deadline += pdMS_TO_TICKS(kFramePeriodMs);
        }
    }
    if (shutdown_sem_) xSemaphoreGive(shutdown_sem_);
    vTaskDelete(NULL);
}

FrameResult LightingRuntime::render_frame(uint32_t current_ms) {
    // LG-HW-004 §27: Abnormal reset output quarantine (physical outputs held dark for 1000ms)
    if (board::is_abnormal_reset_quarantine_active(current_ms)) {
        in_abnormal_reset_quarantine_ = true;
        controller_.blackout();
        if (activation_timing_.first_zero_black_ms == 0) {
            activation_timing_.first_zero_black_ms = current_ms;
            if (activation_timing_.arm_ms > 0) {
                activation_timing_.last_arm_to_zero_ms = (current_ms >= activation_timing_.arm_ms) ? (current_ms - activation_timing_.arm_ms) : 0;
            }
        }
        return FrameResult::NoOutput;
    } else if (in_abnormal_reset_quarantine_) {
        in_abnormal_reset_quarantine_ = false;
        restart_soft_start();
        ESP_LOGI(TAG, "Abnormal reset output quarantine expired: re-arming output with soft start");
    }

    // LG-HW-004 §20: Trigger power ramp on successful topology generation change
    const uint32_t cur_top_gen = controller_.topology_generation();
    if (cur_top_gen != last_observed_topology_generation_) {
        last_observed_topology_generation_ = cur_top_gen;
        restart_soft_start();
    }

    portENTER_CRITICAL(&estop_state_mux_);
    const EstopState es = estop_state_.load(std::memory_order_acquire);
    const uint32_t reset_generation = estop_reset_generation_;
    portEXIT_CRITICAL(&estop_state_mux_);
    if (es == EstopState::Tripped) {
        controller_.blackout();
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        return FrameResult::EstopBlackout;
    } else if (es == EstopState::PendingReset) {
        controller_.blackout();
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        portENTER_CRITICAL(&estop_state_mux_);
        if (estop_state_.load(std::memory_order_relaxed) == EstopState::PendingReset &&
            estop_reset_generation_ == reset_generation) {
            estop_state_.store(EstopState::ReArmedBlackout, std::memory_order_release);
        }
        portEXIT_CRITICAL(&estop_state_mux_);
        return FrameResult::EstopBlackout;
    } else if (es == EstopState::ReArmedBlackout) {
        const esp_err_t err = controller_.clear_estop(reset_generation);
        if (preview_cb_) {
            for (auto& ch : current_render_frame_.channels) {
                ch.pixels.fill(LightValue{0, 0, 0, 0});
                ch.pwm = 0;
            }
            preview_cb_(current_render_frame_, current_local_cfg_);
        }
        bool reset_finished = false, reset_failed = false;
        portENTER_CRITICAL(&estop_state_mux_);
        if (estop_state_.load(std::memory_order_relaxed) == EstopState::ReArmedBlackout &&
            estop_reset_generation_ == reset_generation) {
            estop_state_.store(err == ESP_OK ? EstopState::Inactive : EstopState::Tripped, std::memory_order_release);
            reset_finished = err == ESP_OK;
            reset_failed = err != ESP_OK;
        }
        portEXIT_CRITICAL(&estop_state_mux_);
        if (reset_finished) {
            restart_soft_start();
            ESP_LOGI(TAG, "EMERGENCY STOP RESET: Controller re-armed, resuming output on subsequent frame");
        } else if (reset_failed) {
            report_output_fault(err);
            ESP_LOGE(TAG, "EMERGENCY STOP RESET FAILED: Hardware clear error %s; re-tripping E-stop", esp_err_to_name(err));
        }
        return FrameResult::EstopBlackout;
    }

    uint32_t render_scene_generation = 0;
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return FrameResult::NoOutput;
    if (shutting_down_.load(std::memory_order_acquire)) { xSemaphoreGive(mutex_); return FrameResult::NoOutput; }
    if (live_pending_dirty_) { (*live_active_) = (*live_pending_); live_pending_dirty_ = false; }
    current_render_scene_ = scene_manager_.process_frame(current_ms);
    current_local_cfg_ = device_cfg_;
    render_scene_generation = requested_scene_generation_.load(std::memory_order_acquire);
    // Keep scene transitions, manual data, gamma and power state consistent for
    // CPU composition. Release before output I/O or application callbacks.

    const SceneConfig& render_scene = current_render_scene_;
    const DeviceConfig& local_cfg = current_local_cfg_;

    ControlSnapshot ctrl_snapshot = source_manager_.get_snapshot(current_ms);
    auto live_channel_is_valid = [current_ms](const LiveDmxChannel& ch) -> bool {
        if (!ch.active || ch.opacity == 0) return false;
        return current_ms - ch.timestamp_ms <= kLiveFrameFailsafeTimeoutMs;
    };
    auto manual_channel_is_valid = [current_ms, this](uint8_t ch) -> bool {
        if (ch >= 16 || !manual_active_.channels[ch].active) return false;
        if (manual_active_.lease_ms == 0) return false;
        return (current_ms - manual_active_.timestamp_ms <= manual_active_.lease_ms);
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
        if (ch_owner == ControlSource::LiveDmx && live_channel_is_valid(live_active_->channels[ch])) {
            ChannelFrame& live_src = current_live_src_;
            live_src.pixels.fill(LightValue{0, 0, 0, 0});
            const uint16_t pcount = std::min<uint16_t>(live_active_->channels[ch].pixel_count, num_pixels);
            for (uint16_t p = 0; p < pcount; ++p) live_src.pixels[p] = live_active_->channels[ch].pixels[p];
            live_src.pwm = live_active_->channels[ch].pwm;

            if (is_addressable) {
                for (uint16_t px = 0; px < num_pixels; ++px) {
                    render_frame.channels[ch].pixels[px] = ColorPipeline::to_addressable(live_src.pixels[px], channel_cfg, render_scene.global_brightness);
                }
            } else {
                render_frame.channels[ch].pwm = ColorPipeline::to_pwm(live_src.pixels[0], live_src.pwm, channel_cfg, render_scene.global_brightness);
            }
        } else if (ch_owner == ControlSource::ManualOverride && manual_channel_is_valid(ch)) {
            const auto& man_ch = manual_active_.channels[ch];
            if (is_addressable) {
                const LightValue pixel_val = ColorPipeline::to_addressable(man_ch.color, channel_cfg, man_ch.brightness);
                for (uint16_t px = 0; px < num_pixels; ++px) {
                    render_frame.channels[ch].pixels[px] = pixel_val;
                }
            } else {
                const uint16_t pwm_val = ColorPipeline::to_pwm(LightValue{0, 0, 0, 0}, man_ch.pwm, channel_cfg, 255);
                render_frame.channels[ch].pwm = pwm_val;
                if (ch >= 8 && ch < 16) {
                    last_pwm_duty_[ch - 8] = pwm_val;
                }
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
    xSemaphoreGive(mutex_);
    OutputState state = output_state_.load(std::memory_order_acquire);
    FrameResult frame_result = FrameResult::NoOutput;
    if (state == OutputState::Faulted) {
        output_fault_frame_count_.fetch_add(1, std::memory_order_relaxed);
        controller_.blackout();
        frame_result = FrameResult::Failed;
    } else {
        bool frame_committed = false;
        ControlSource dominant = source_manager_.current_owner();
        if (dominant != ControlSource::Factory && dominant != ControlSource::EStop) {
            const esp_err_t sched_err = OutputScheduler::schedule(render_frame, local_cfg, controller_);
            if (sched_err != ESP_OK) {
                report_output_fault(sched_err);
                if (preview_cb_) {
                    try { preview_cb_(render_frame, local_cfg); } catch (...) {}
                }
                return FrameResult::Failed;
            }
            frame_committed = true;
            frame_result = FrameResult::Committed;

            // LG-HW-004 §34: Track activation timing milestones
            if (activation_timing_.first_nonzero_ms == 0) {
                activation_timing_.first_nonzero_ms = current_ms;
                if (activation_timing_.first_zero_black_ms > 0) {
                    activation_timing_.last_zero_to_nonzero_ms = (current_ms >= activation_timing_.first_zero_black_ms) ?
                        (current_ms - activation_timing_.first_zero_black_ms) : 0;
                }
                board::record_first_output_commit(current_ms);
            }
            if (activation_timing_.ramp_complete_ms == 0 && current_ms >= enable_ms_ + power_budget_.ramp_ms()) {
                activation_timing_.ramp_complete_ms = current_ms;
                if (activation_timing_.arm_ms > 0) {
                    activation_timing_.last_ramp_duration_ms = (current_ms >= activation_timing_.arm_ms) ?
                        (current_ms - activation_timing_.arm_ms) : 0;
                }
            }
        } else {
            frame_result = FrameResult::NoOutput;
        }
        if (frame_committed && state == OutputState::Armed &&
            output_state_.compare_exchange_strong(state, OutputState::Healthy, std::memory_order_acq_rel)) {
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
                if (generation_applied_cb_) {
                    generation_applied_cb_(render_scene_generation);
                }
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
    return frame_result;
}

FrameBudgetEstimate LightingRuntime::preflight_frame_budget(const DeviceConfig& cfg) {
    FrameBudgetEstimate est;
    uint32_t max_wire_us = 0;
    bool any_pwm = false;

    for (uint8_t ch = 0; ch < 8; ++ch) {
        if (cfg.channels[ch].enabled && cfg.channels[ch].pixel_count > 0) {
            const bool is_rgbw = (cfg.channels[ch].addressable_type == AddressableType::SK6812);
            const uint16_t px = std::min<uint16_t>(cfg.channels[ch].pixel_count, MAX_PIXELS);
            const uint32_t wire_us = addressable_wire_time_us(px, is_rgbw);
            if (wire_us > max_wire_us) max_wire_us = wire_us;
        }
    }
    for (uint8_t ch = 8; ch < 16; ++ch) {
        if (cfg.channels[ch].enabled) {
            any_pwm = true;
            break;
        }
    }

    est.longest_addr_us = max_wire_us;
    est.estimated_pwm_us = any_pwm ? 3500 : 0;
    est.total_estimated_us = est.longest_addr_us + est.estimated_pwm_us;

    if (est.total_estimated_us <= 18000) {
        est.status = FrameBudgetStatus::Healthy;
    } else if (est.total_estimated_us <= 24500) {
        est.status = FrameBudgetStatus::NearLimit;
    } else {
        est.status = FrameBudgetStatus::Impossible;
    }
    return est;
}

void LightingRuntime::activate_scene(uint8_t index) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (!mutex_ || !control_updates_allowed()) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    if (scene_manager_.activate_scene(index, now_ms())) {
        source_manager_.acquire(ControlSource::ScenePlayback, 0, 0, 0, 0xFFFF);
    }
    xSemaphoreGive(mutex_);
}
void LightingRuntime::activate_scene_direct(const SceneConfig& scene, bool immediate) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (!mutex_ || !control_updates_allowed()) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    source_manager_.acquire(ControlSource::ScenePlayback, 0, 0, 0, 0xFFFF);
    scene_manager_.activate_scene_direct(scene, now_ms(), immediate);
    xSemaphoreGive(mutex_);
}

bool LightingRuntime::submit_live_frame(const LiveDmxFrame& frame) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (shutting_down_.load(std::memory_order_acquire) || !mutex_ || !live_pending_ || !live_active_) return false;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return false; }
    const uint32_t now = now_ms();
    (*live_pending_) = frame; live_pending_->timestamp_ms = now; live_pending_dirty_ = true; last_live_stream_ms_ = now;
    uint8_t live_zone_mask = 0, live_layer_mask = 0; uint16_t live_channel_mask = 0;
    for (uint8_t z = 0; z < 8; ++z) for (uint8_t l = 0; l < 3; ++l) if (live_pending_->layers[z][l].active) { live_zone_mask |= (1 << z); live_layer_mask |= (1 << l); }
    for (uint8_t ch = 0; ch < 16; ++ch) if (live_pending_->channels[ch].active) live_channel_mask |= (1 << ch);
    if (live_zone_mask == 0 && live_channel_mask == 0) source_manager_.release(ControlSource::LiveDmx);
    else source_manager_.acquire(ControlSource::LiveDmx, 0, live_zone_mask, live_layer_mask, live_channel_mask);
    xSemaphoreGive(mutex_); return true;
}

esp_err_t LightingRuntime::clear_output_fault() {
    if (controller_.is_estop_latched() || is_estop_active()) return ESP_ERR_INVALID_STATE;
    controller_.blackout();

    // LG-HW-003 §12: When fault domain is PcaI2c, perform PCA recovery on OutputCoreExecutor
    if (controller_.last_fault_domain() == OutputFaultDomain::PcaI2c) {
        esp_err_t rec_err = OutputCoreExecutor::instance().execute_pca_recovery(&controller_);
        if (rec_err != ESP_OK || !controller_.pwm().zero_confirmed()) {
            ESP_LOGE(TAG, "PCA recovery failed during fault clear: %s", esp_err_to_name(rec_err));
            return (rec_err != ESP_OK) ? rec_err : ESP_FAIL;
        }
    }

    controller_.clear_fault_domain();
    output_state_.store(OutputState::Armed, std::memory_order_release);
    restart_soft_start();
    ESP_LOGI(TAG, "Output fault cleared: output state is ARMED awaiting next rendered frame commit");
    return ESP_OK;
}

void LightingRuntime::restart_soft_start() {
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    restart_soft_start_locked();
    xSemaphoreGive(mutex_);
}

void LightingRuntime::restart_soft_start_locked() {
    enable_ms_ = now_ms();
    activation_timing_.arm_ms = enable_ms_;
    activation_timing_.ramp_complete_ms = 0;
    power_budget_.on_enable();
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
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (zone_idx >= 8 || layer_idx >= 3 || !mutex_ || !live_pending_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    const uint32_t now = now_ms();
    live_pending_->layers[zone_idx][layer_idx].color = color; live_pending_->layers[zone_idx][layer_idx].opacity = opacity;
    live_pending_->layers[zone_idx][layer_idx].timestamp_ms = now; live_pending_->layers[zone_idx][layer_idx].active = true;
    live_pending_dirty_ = true; last_live_stream_ms_ = now;
    source_manager_.acquire(ControlSource::LiveDmx, kLiveFrameFailsafeTimeoutMs, (1 << zone_idx), (1 << layer_idx), 0);
    xSemaphoreGive(mutex_);
}

void LightingRuntime::inject_live_fixture(uint8_t fixture_idx, const LightValue* pixels, uint16_t count, const uint16_t* pwm_duties, uint8_t opacity) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (fixture_idx >= 16 || !mutex_ || !live_pending_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    const uint32_t now = now_ms(); auto& lf = live_pending_->channels[fixture_idx];
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
    if (!mutex_ || !live_active_) {
        return {};
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto dmx = live_active_->dmx_telemetry;
    xSemaphoreGive(mutex_);
    return dmx;
}

uint8_t LightingRuntime::active_live_objects() const {
    if (!mutex_ || !live_active_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return 0;
    uint8_t count = 0; const uint32_t now = now_ms();
    for (uint8_t z = 0; z < 8; ++z) for (uint8_t l = 0; l < 3; ++l) { const auto& layer = live_active_->layers[z][l]; if (layer.active && layer.opacity > 0 && now - layer.timestamp_ms <= kLiveFrameFailsafeTimeoutMs) count++; }
    for (uint8_t ch = 0; ch < 16; ++ch) { const auto& ch_obj = live_active_->channels[ch]; if (ch_obj.active && ch_obj.opacity > 0 && now - ch_obj.timestamp_ms <= kLiveFrameFailsafeTimeoutMs) count++; }
    xSemaphoreGive(mutex_);
    return count;
}

void LightingRuntime::feed_watchdog() {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (!mutex_) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    scene_manager_.feed_watchdog(now_ms());
    xSemaphoreGive(mutex_);
}
uint8_t LightingRuntime::active_scene_index() const { return scene_manager_.active_scene_index(); }
bool LightingRuntime::is_fallback_active() const { return scene_manager_.is_fallback_active(); }
bool LightingRuntime::is_transitioning() const { return scene_manager_.is_transitioning(); }
void LightingRuntime::set_beat_period(uint16_t period_ms) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (!mutex_) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    beat_period_ms_ = std::max<uint16_t>(10, std::min<uint16_t>(period_ms, 60000));
    xSemaphoreGive(mutex_);
}

void LightingRuntime::reload_device_config() {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
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
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    device_cfg_ = candidate;
    power_budget_.set_limit_mw(device_cfg_.max_power_mw);
    power_budget_.set_soft_start(device_cfg_.soft_start_enabled, device_cfg_.soft_start_ms);
    ColorPipeline::set_gamma(device_cfg_.gamma_x10);
    scene_manager_.update_device_config(device_cfg_);
    restart_soft_start_locked();
    xSemaphoreGive(mutex_);
    // The runtime callback updates DmxRouter, which clears the live frame when
    // its patch changes. It must be able to reacquire this nonrecursive mutex.
    if (config_reload_cb_) {
        config_reload_cb_(candidate);
    }
    ESP_LOGI(TAG, "Device config transactional reload applied successfully");
}

bool LightingRuntime::acquire_control(ControlSource source, uint32_t lease_ms, uint8_t zone_mask, uint8_t layer_mask, uint16_t channel_mask, bool takeover) {
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (source == ControlSource::EStop) {
        trigger_estop();
    }
    if (!mutex_) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return false; }
    bool won = source_manager_.acquire(source, lease_ms, zone_mask, layer_mask, channel_mask, takeover);
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
    if (source == ControlSource::Factory) {
        restart_soft_start_locked();
    }
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
    const uint32_t epoch = control_epoch_.load(std::memory_order_acquire);
    if (!mutex_) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!control_updates_allowed(epoch)) { xSemaphoreGive(mutex_); return; }
    scene_manager_.set_transition_curve(curve);
    xSemaphoreGive(mutex_);
}

} // namespace lg
