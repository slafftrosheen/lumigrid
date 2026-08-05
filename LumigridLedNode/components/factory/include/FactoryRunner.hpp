// components/factory/include/FactoryRunner.hpp
#pragma once
#include "FactoryEffects.hpp"
#include "FactoryTypes.hpp"
#include <functional>
#include "OutputArbitrator.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace lg {

class FactoryRunner {
public:
    explicit FactoryRunner(OutputArbitrator& output);

    esp_err_t start();
    bool enqueue(const FactoryCommand& command);
    void set_stream_frame(const StreamFramePayload& payload, uint8_t brightness, uint32_t duration_ms);
    FactoryStatus status() const;

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

    OutputArbitrator& output_;

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
