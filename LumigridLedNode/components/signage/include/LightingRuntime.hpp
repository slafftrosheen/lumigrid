// components/signage/include/LightingRuntime.hpp
#pragma once
#include <atomic>
#include <functional>
#include <array>

#include "ControlSourceManager.hpp"
#include "OutputController.hpp"
#include "ConfigStore.hpp"
#include "SignageTypes.hpp"
#include "PowerBudget.hpp"
#include "SceneManager.hpp"
#include "RenderTypes.hpp"

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
    bool activate_scene_checked(uint8_t index) {
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
        xSemaphoreGive(mutex_);
        return accepted;
    }
    void activate_scene_direct(const SceneConfig& scene, bool immediate = false);

    /// Atomically submit a full live DMX frame from DmxRouter (returns false if rejected or unavailable)
    bool submit_live_frame(const LiveDmxFrame& frame);

    /// Authoritative clear of complete live frame state and ownership
    void clear_live_frame();

    /// Safe ordered runtime task shutdown
    void stop();

    void inject_live_data(uint8_t zone_idx, uint8_t layer_idx, const BrandColor& color, uint8_t opacity);
    void inject_live_fixture(uint8_t fixture_idx, const LightValue* pixels, uint16_t count, const uint16_t* pwm_duties, uint8_t opacity);
    void feed_watchdog();
    
    uint8_t active_scene_index() const;
    bool is_fallback_active() const;
    bool is_transitioning() const;

    PowerBudget& power_budget() { return power_budget_; }
    const PowerBudget& power_budget() const { return power_budget_; }
    float get_fps() const { return current_fps_; }
    void set_beat_period(uint16_t period_ms);
    void reload_device_config();

    bool acquire_control(ControlSource source, uint32_t lease_ms = 0, uint8_t zone_mask = 0xFF, uint8_t layer_mask = 0x07);
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
    uint8_t active_live_objects() const;

    DeviceConfig get_config_snapshot() const;

    std::array<uint8_t, 512> last_dmx_data() const;

    using PreviewCallback = std::function<void(const RenderFrame& frame, const DeviceConfig& cfg)>;
    void set_preview_callback(PreviewCallback cb) { preview_cb_ = std::move(cb); }

    using ConfigReloadCallback = std::function<void(const DeviceConfig& cfg)>;
    void set_config_reload_callback(ConfigReloadCallback cb) { config_reload_cb_ = std::move(cb); }

    using MaintenanceCallback = std::function<void(uint32_t now_ms)>;
    void set_maintenance_callback(MaintenanceCallback cb) { maintenance_cb_ = std::move(cb); }

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
    static void task_entry(void* arg);
    void task();
    void render_frame(uint32_t current_ms);

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
    std::atomic<bool> shutting_down_{false};
    SemaphoreHandle_t shutdown_sem_ = nullptr;

    DeviceConfig device_cfg_{};

    // Double buffering for LiveDmxFrame
    LiveDmxFrame live_pending_{};
    LiveDmxFrame live_active_{};
    bool live_pending_dirty_{false};
    uint32_t last_live_stream_ms_ = 0;

    RenderFrame current_render_frame_;
    FixtureFrame current_composited_zone_;
    FixtureFrame current_layer_frame_;
    FixtureFrame current_live_src_;
    SceneConfig current_render_scene_{};
    DeviceConfig current_local_cfg_{};

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