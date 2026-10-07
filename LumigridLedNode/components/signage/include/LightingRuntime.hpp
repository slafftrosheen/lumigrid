// components/signage/include/LightingRuntime.hpp
#pragma once
#include <atomic>
#include <functional>
#include <array>
#include <memory>

#include "ControlSourceManager.hpp"
#include "OutputController.hpp"
#include "ConfigStore.hpp"
#include "SignageTypes.hpp"
#include "PowerBudget.hpp"
#include "SceneManager.hpp"
#include "RenderTypes.hpp"
#include "ManualControlTypes.hpp"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lg {

class LightingRuntime {
public:
    enum class LifecycleState : uint8_t {
        Constructed = 0,
        Starting = 1,
        Running = 2,
        Stopping = 3,
        Stopped = 4,
        Faulted = 5
    };

    LightingRuntime(ControlSourceManager& source_manager, OutputController& controller, ConfigStore& config);
    ~LightingRuntime();

    esp_err_t start();

    void activate_scene(uint8_t index);
    // Truthful control-plane activation boundary. Unlike the legacy void method,
    // this reports load/rejection failures and only accepts activation while the
    // runtime is atomically observed in the Running lifecycle state.
    bool activate_scene_checked(uint8_t index, uint32_t* generation = nullptr) {
        if (!mutex_ ||
            lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running ||
            shutting_down_.load(std::memory_order_acquire)) {
            return false;
        }

        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool accepted = scene_manager_.activate_scene(
            index,
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL)
        );

        if (accepted) {
            source_manager_.acquire(ControlSource::ScenePlayback, 0, 0, 0, 0xFFFF);
            const uint32_t next_generation =
                requested_scene_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            requested_scene_index_.store(index, std::memory_order_release);
            if (generation) {
                *generation = next_generation;
            }
        }

        xSemaphoreGive(mutex_);
        return accepted;
    }

    bool activate_scene_custom(uint8_t index, uint16_t fade_ms, TransitionCurve curve, uint32_t* generation = nullptr) {
        if (!mutex_ ||
            lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running ||
            shutting_down_.load(std::memory_order_acquire)) {
            return false;
        }

        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool accepted = scene_manager_.activate_scene_custom(
            index,
            fade_ms,
            curve,
            static_cast<uint32_t>(esp_timer_get_time() / 1000ULL)
        );

        if (accepted) {
            source_manager_.acquire(ControlSource::ScenePlayback, 0, 0, 0, 0xFFFF);
            const uint32_t next_generation =
                requested_scene_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            requested_scene_index_.store(index, std::memory_order_release);
            if (generation) {
                *generation = next_generation;
            }
        }

        xSemaphoreGive(mutex_);
        return accepted;
    }

    bool prepare_manual_transition(uint8_t target_index) {
        if (!mutex_ || lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running) return false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        bool res = scene_manager_.prepare_manual_transition(target_index);
        xSemaphoreGive(mutex_);
        return res;
    }

    void set_manual_transition_progress(uint16_t progress_q15) {
        if (!mutex_) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        scene_manager_.set_manual_transition_progress(progress_q15);
        xSemaphoreGive(mutex_);
    }

    // ── LG-034: transition freeze (Cue pause) ──
    bool freeze_transition() {
        if (!mutex_ || lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running) return false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool res = scene_manager_.freeze_transition();
        xSemaphoreGive(mutex_);
        return res;
    }

    bool resume_frozen_transition(uint32_t remaining_ms, uint32_t current_ms) {
        if (!mutex_ || lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Running) return false;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool res = scene_manager_.resume_frozen_transition(remaining_ms, current_ms);
        xSemaphoreGive(mutex_);
        return res;
    }

    void activate_scene_direct(const SceneConfig& scene, bool immediate = false);

    /// Atomically submit a full live DMX frame from DmxRouter (returns false if rejected or unavailable)
    /// Atomically submit a full live DMX frame from DmxRouter (returns false if rejected or unavailable)
    bool submit_live_frame(const LiveDmxFrame& frame);

    /// Authoritative clear of complete live frame state and ownership
    void clear_live_frame();

    /// Submit a transient channel-scoped manual override frame with lease
    bool submit_manual_override(const ManualOverrideFrame& frame, uint32_t lease_ms = 15000);

    /// Clear transient manual override state and release ownership
    void clear_manual_override();

    /// Query current manual override status (active, channelMask, leaseRemainingMs)
    ManualControlStatus manual_control_status() const;

    /// Safe ordered runtime task shutdown
    void stop();

    void inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity);
    void inject_live_fixture(uint8_t fixture_idx, const LightValue* pixels, uint16_t count, const uint16_t* pwm_duties, uint8_t opacity);
    void feed_watchdog();
    
    uint8_t active_scene_index() const;
    bool is_fallback_active() const;
    bool is_transitioning() const;
    uint16_t transition_progress_q15() const;
    TransitionCurve transition_curve() const;
    void set_transition_curve(TransitionCurve curve);
    SceneManager& scene_manager() { return scene_manager_; }
    const SceneManager& scene_manager() const { return scene_manager_; }

    uint8_t requested_scene_index() const {
        return requested_scene_index_.load(std::memory_order_acquire);
    }
    uint32_t requested_scene_generation() const {
        return requested_scene_generation_.load(std::memory_order_acquire);
    }
    uint32_t applied_scene_generation() const {
        return applied_scene_generation_.load(std::memory_order_acquire);
    }
    bool scene_apply_pending() const {
        return requested_scene_generation() > applied_scene_generation();
    }

    PowerBudget& power_budget() { return power_budget_; }
    const PowerBudget& power_budget() const { return power_budget_; }
    float get_fps() const { return frame_metrics_.committed_fps; }
    FrameTimingMetrics frame_timing_metrics() const { return frame_metrics_; }
    static FrameBudgetEstimate preflight_frame_budget(const DeviceConfig& cfg);
    void set_beat_period(uint16_t period_ms);
    void reload_device_config();
    void restart_soft_start();

    bool acquire_control(ControlSource source, uint32_t lease_ms = 0, uint8_t zone_mask = 0xFF, uint8_t layer_mask = 0x07, uint16_t channel_mask = 0, bool takeover = false);
    void release_control(ControlSource source);
    ControlSource current_control_source() const;

    // Absolute E-Stop safety state with latched recovery semantics
    void trigger_estop();

    LifecycleState lifecycle_state() const {
        return lifecycle_state_.load(std::memory_order_acquire);
    }

    uint32_t commit_failure_count() const {
        return output_commit_failure_count_.load(std::memory_order_relaxed);
    }
    uint32_t output_commit_failure_count() const {
        return output_commit_failure_count_.load(std::memory_order_relaxed);
    }

    void clear_estop();
    bool is_estop_active() const {
        return estop_state_.load(std::memory_order_relaxed) != EstopState::Inactive;
    }
    bool is_output_controller_estop_latched() const {
        return controller_.is_estop_latched();
    }
    const OutputController& controller() const { return controller_; }

    enum class OutputState : uint8_t {
        Healthy = 0,
        Faulted = 1,
        Armed = 2
    };

    // Hardware output fault tracking (3-state recovery: Healthy, Faulted, Armed)
    void report_output_fault(esp_err_t err);
    esp_err_t clear_output_fault();
    OutputState output_state() const {
        return output_state_.load(std::memory_order_acquire);
    }
    bool is_output_fault() const {
        return output_state_.load(std::memory_order_acquire) == OutputState::Faulted;
    }
    bool output_fault() const {
        return is_output_fault();
    }
    uint32_t fault_event_count() const {
        return output_fault_count_.load(std::memory_order_relaxed);
    }
    uint32_t output_fault_count() const {
        return fault_event_count();
    }
    uint32_t output_fault_frame_count() const {
        return output_fault_frame_count_.load(std::memory_order_relaxed);
    }
    uint32_t frame_count() const {
        return frame_count_;
    }
    uint8_t current_brightness() const {
        return current_render_scene_.global_brightness;
    }
    uint16_t last_pwm_level(uint8_t channel) const {
        if (channel >= 8 && channel < 16) {
            return last_pwm_duty_[channel - 8];
        }
        return 0;
    }
    uint8_t active_live_objects() const;

    DeviceConfig get_config_snapshot() const;

    std::array<uint8_t, 512> last_dmx_data() const;

    using PreviewCallback = std::function<void(const RenderFrame& frame, const DeviceConfig& cfg)>;
    void set_preview_callback(PreviewCallback cb) { preview_cb_ = std::move(cb); }

    using ConfigReloadCallback = std::function<void(const DeviceConfig& cfg)>;
    void set_config_reload_callback(ConfigReloadCallback cb) { config_reload_cb_ = std::move(cb); }

    using MaintenanceCallback = std::function<void(uint32_t now_ms)>;
    void set_maintenance_callback(MaintenanceCallback cb) { maintenance_cb_ = std::move(cb); }

    using GenerationAppliedCallback = std::function<void(uint32_t applied_generation)>;
    void set_generation_applied_callback(GenerationAppliedCallback cb) { generation_applied_cb_ = std::move(cb); }

private:
    /// State machine for deterministic E-stop reset:
    /// Tripped -> PendingReset -> ReArmedBlackout -> Inactive
    /// Guarantees 2 full frame boundaries of blacked-out hardware before visible output resumes.
    enum class EstopState : uint8_t {
        Inactive = 0,
        Tripped = 1,
        PendingReset = 2,
        ReArmedBlackout = 3
    };

    ConfigReloadCallback config_reload_cb_;
    GenerationAppliedCallback generation_applied_cb_;
    static void task_entry(void* arg);
    void task();
    FrameResult render_frame(uint32_t current_ms);

    FrameTimingMetrics frame_metrics_{};

    ControlSourceManager& source_manager_;
    OutputController& controller_;
    ConfigStore& config_;
    PowerBudget power_budget_;
    SceneManager scene_manager_;

    std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Constructed};
    std::atomic<EstopState> estop_state_{EstopState::Inactive};
    std::atomic<OutputState> output_state_{OutputState::Healthy};
    std::atomic<uint32_t> output_fault_count_{0};
    std::atomic<uint32_t> output_commit_failure_count_{0};
    std::atomic<uint32_t> output_fault_frame_count_{0};

    // Scene request/application handshake. A generation becomes "applied" only
    // after a frame rendered from that request successfully commits to output.
    std::atomic<uint8_t> requested_scene_index_{0};
    std::atomic<uint32_t> requested_scene_generation_{0};
    std::atomic<uint32_t> applied_scene_generation_{0};

    std::atomic<bool> shutting_down_{false};
    SemaphoreHandle_t shutdown_sem_ = nullptr;

    DeviceConfig device_cfg_{};

    // Double buffering for LiveDmxFrame
    // LG-038.1: Allocate after app_main, leaving DRAM available for scheduler startup.
    // Owned for the runtime lifetime; stop/start preserves stable buffer addresses.
    std::unique_ptr<LiveDmxFrame> live_pending_;
    std::unique_ptr<LiveDmxFrame> live_active_;
    bool live_pending_dirty_{false};
    uint32_t last_live_stream_ms_ = 0;

    // Transient Manual Override state
    ManualOverrideFrame manual_active_{};

    RenderFrame current_render_frame_;
    FixtureFrame current_composited_zone_;
    FixtureFrame current_layer_frame_;
    FixtureFrame transition_layer_frame_;
    FixtureFrame current_live_src_;
    SceneConfig current_render_scene_{};
    DeviceConfig current_local_cfg_{};
    std::array<uint16_t, 8> last_pwm_duty_{};

    uint16_t beat_period_ms_ = 500;
    uint32_t enable_ms_ = 0;
    bool first_frame_ = true;

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    uint32_t frame_count_ = 0;
    uint32_t last_fps_time_ms_ = 0;
    float current_fps_ = 0.0f;

    PreviewCallback preview_cb_;
    MaintenanceCallback maintenance_cb_;

    static constexpr uint32_t kFramePeriodMs = 25; // 40 FPS
    /// DmxRouter decides protocol source lifetime.
    /// LightingRuntime fail-safe only prevents indefinite stale rendering
    /// if the router path itself stops delivering updates.
    /// The failsafe MUST NOT release ControlSource::LiveDmx by itself.
    /// The renderer should simply treat expired data as visually inactive.
    /// Control arbitration remains controlled by submitted live-frame state.
    static constexpr uint32_t kLiveFrameFailsafeTimeoutMs = 5000;
};

} // namespace lg