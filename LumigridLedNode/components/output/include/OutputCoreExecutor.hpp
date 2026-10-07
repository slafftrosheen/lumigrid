// components/output/include/OutputCoreExecutor.hpp
//
// LG-HW-001: Hardware lifecycle execution context pinned permanently to CPU1.
// Guarantees RMT channel/encoder allocation and PCA I2C driver installation
// execute strictly on CPU1, completely isolated from CPU0 networking (Wi-Fi).
#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "SignageTypes.hpp"

namespace lg {

class OutputController;

enum class OutputCoreCommandType : uint8_t {
    Init,
    Reconfigure,
};

struct OutputCoreCommand {
    OutputCoreCommandType type;
    OutputController* controller = nullptr;
    const DeviceConfig* config = nullptr;
    SemaphoreHandle_t completion_sem = nullptr;
    esp_err_t result = ESP_FAIL;
};

class OutputCoreExecutor {
public:
    static OutputCoreExecutor& instance();

    OutputCoreExecutor();
    ~OutputCoreExecutor();

    OutputCoreExecutor(const OutputCoreExecutor&) = delete;
    OutputCoreExecutor& operator=(const OutputCoreExecutor&) = delete;

    esp_err_t start();
    bool is_running() const { return task_handle_ != nullptr; }

    esp_err_t execute_init(OutputController* controller);
    esp_err_t execute_reconfigure(OutputController* controller, const DeviceConfig& cfg);

    int executor_core() const { return executor_core_; }

private:
    static void task_entry(void* arg);
    void run();

    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t lifecycle_mutex_ = nullptr;
    int executor_core_ = -1;
};

} // namespace lg
