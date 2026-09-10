// components/factory/include/FactoryRunner.hpp
#pragma once
#include "FactoryEffects.hpp"
#include "FactoryTypes.hpp"
#include <functional>
#include "ControlSourceManager.hpp"
#include "OutputController.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace lg {

/// @brief Factory hardware test runner.
///
/// ARCHITECTURAL EXCEPTION NOTE (Batch 2, Item 23):
/// FactoryRunner directly commands OutputController hardware for factory bring-up,
/// manufacturing lane testing, PWM diagnostics, and board burn-in. This direct hardware
/// access is an INTENTIONAL EXCEPTION to the standard rendering pipeline.
///
/// DO NOT copy this direct-hardware pattern for any application features, user effects,
/// or protocol handlers. All operational lighting features MUST render into RenderFrame
/// via LightingRuntime and be scheduled through OutputScheduler and Compositor.
class FactoryRunner {
public:
    FactoryRunner(ControlSourceManager& source_manager, OutputController& controller);

    esp_err_t start();
    bool enqueue(const FactoryCommand& command);
    void set_stream_frame(const StreamFramePayload& payload, uint8_t brightness, uint32_t duration_ms);
    FactoryStatus status() const;

    ControlSourceManager& output() { return source_manager_; }

    using StatusCallback = std::function<void()>;
    void set_status_callback(StatusCallback cb) { status_callback_ = std::move(cb); }

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
};

} // namespace lg
