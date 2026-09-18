// components/factory/include/FactoryRunner.hpp
#pragma once
#include "FactoryEffects.hpp"
#include "FactoryTypes.hpp"
#include <atomic>
#include <functional>
#include "ControlSourceManager.hpp"
#include "OutputController.hpp"
#include "CommandReceipt.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace lg {

/// @brief Factory hardware test runner.
///
/// ARCHITECTURAL EXCEPTION NOTE:
/// FactoryRunner directly commands OutputController hardware for factory bring-up,
/// manufacturing lane testing, PWM diagnostics, and board burn-in. This direct hardware
/// access is an INTENTIONAL EXCEPTION to the standard rendering pipeline.
/// However, it MUST STILL PASS the same final hardware safety gate (OutputController::try_commit()).
/// FactoryRunner can NEVER physically output while the controller is estop-latched or output-disabled.
class FactoryRunner {
public:
    enum class LifecycleState : uint8_t {
        Constructed = 0,
        Starting = 1,
        Running = 2,
        Stopping = 3,
        Stopped = 4,
        Faulted = 5
    };

    FactoryRunner(ControlSourceManager& source_manager, OutputController& controller);
    ~FactoryRunner();

    esp_err_t start();
    void stop();
    LifecycleState lifecycle_state() const {
        return lifecycle_state_.load(std::memory_order_acquire);
    }

    bool enqueue(const FactoryCommand& command);
    bool abort() {
        FactoryCommand cmd;
        cmd.type = FactoryCommandType::Abort;
        return enqueue(cmd);
    }
    bool select_effect(FactoryEffect effect, uint8_t brightness = 32, uint32_t duration_ms = 10000) {
        FactoryCommand cmd;
        cmd.type = FactoryCommandType::SelectEffect;
        cmd.effect = effect;
        cmd.brightness = brightness;
        cmd.duration_ms = duration_ms;
        return enqueue(cmd);
    }
    bool set_auto_loop(bool enabled) {
        FactoryCommand cmd;
        cmd.type = FactoryCommandType::SetAutoLoop;
        cmd.enabled = enabled;
        return enqueue(cmd);
    }
    bool test_channel(uint8_t channel, const char* mode = "identify", uint32_t duration_ms = 3000);
    void set_stream_frame(const StreamFramePayload& payload, uint8_t brightness, uint32_t duration_ms);
    FactoryStatus status() const;

    ControlSourceManager& control_sources() { return source_manager_; }
    // Deprecated alias for backwards compatibility
    ControlSourceManager& output() { return control_sources(); }

    using StatusCallback = std::function<void()>;
    void set_status_callback(StatusCallback cb) { status_callback_ = std::move(cb); }
    void set_command_journal(CommandJournal* journal) { journal_ = journal; }

private:
    static void task_entry(void* argument);
    void task();

    void apply_command(const FactoryCommand& command, uint32_t now_ms);
    void start_effect(
        FactoryEffect effect,
        uint32_t duration_ms,
        uint8_t brightness,
        uint32_t now_ms
    );
    void blackout();

    ControlSourceManager& source_manager_;
    OutputController& controller_;
    CommandJournal* journal_ = nullptr;
    uint32_t pending_command_id_ = 0;

    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t status_mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;

    FactoryStatus status_{};
    FactoryCommand direct_test_{};
    bool direct_test_active_ = false;

    FactoryCommand stream_frame_{};
    bool stream_active_ = false;
    uint32_t stream_last_ms_ = 0;

    StatusCallback status_callback_{};
    std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Constructed};
    std::atomic<bool> shutting_down_{false};
    SemaphoreHandle_t shutdown_sem_ = nullptr;
};

} // namespace lg
