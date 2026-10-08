// components/output/OutputCoreExecutor.cpp
//
// LG-HW-001: Hardware lifecycle execution context pinned permanently to CPU1.
#include "OutputCoreExecutor.hpp"
#include "OutputController.hpp"
#include "esp_log.h"

namespace lg {

static const char* TAG = "output_exec";

OutputCoreExecutor& OutputCoreExecutor::instance() {
    static OutputCoreExecutor s_instance;
    return s_instance;
}

OutputCoreExecutor::OutputCoreExecutor() {
    lifecycle_mutex_ = xSemaphoreCreateMutex();
    queue_ = xQueueCreate(4, sizeof(OutputCoreCommand*));
}

OutputCoreExecutor::~OutputCoreExecutor() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
    if (lifecycle_mutex_ != nullptr) {
        vSemaphoreDelete(lifecycle_mutex_);
        lifecycle_mutex_ = nullptr;
    }
}

esp_err_t OutputCoreExecutor::start() {
    if (task_handle_ != nullptr) {
        return ESP_OK;
    }

    if (queue_ == nullptr) {
        queue_ = xQueueCreate(4, sizeof(OutputCoreCommand*));
    }
    if (lifecycle_mutex_ == nullptr) {
        lifecycle_mutex_ = xSemaphoreCreateMutex();
    }
    if (queue_ == nullptr || lifecycle_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // Pin hardware executor permanently to CPU1 (core 1)
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_entry,
        "output_hw",
        6144,
        this,
        5, // Priority: dedicated hardware lifecycle worker
        &task_handle_,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create output_hw executor task on CPU1");
        return ESP_FAIL;
    }

    // Yield briefly so worker initializes executor_core_
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

esp_err_t OutputCoreExecutor::execute_init(OutputController* controller) {
    if (controller == nullptr) return ESP_ERR_INVALID_ARG;

    // If caller is already executing on CPU1, invoke directly to prevent deadlocks
    if (xPortGetCoreID() == 1) {
        return controller->init_on_output_core();
    }

    esp_err_t err = start();
    if (err != ESP_OK) return err;

    OutputCoreCommand cmd{};
    cmd.type = OutputCoreCommandType::Init;
    cmd.controller = controller;
    cmd.completion_sem = xSemaphoreCreateBinary();
    if (cmd.completion_sem == nullptr) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_ERR_TIMEOUT;
    }

    OutputCoreCommand* p_cmd = &cmd;
    if (xQueueSend(queue_, &p_cmd, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(lifecycle_mutex_);
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_FAIL;
    }

    xSemaphoreTake(cmd.completion_sem, portMAX_DELAY);
    xSemaphoreGive(lifecycle_mutex_);

    vSemaphoreDelete(cmd.completion_sem);
    return cmd.result;
}

esp_err_t OutputCoreExecutor::execute_reconfigure(OutputController* controller, const DeviceConfig& cfg) {
    if (controller == nullptr) return ESP_ERR_INVALID_ARG;

    // If caller is already executing on CPU1, invoke directly to prevent deadlocks
    if (xPortGetCoreID() == 1) {
        return controller->reconfigure_on_output_core(cfg);
    }

    esp_err_t err = start();
    if (err != ESP_OK) return err;

    OutputCoreCommand cmd{};
    cmd.type = OutputCoreCommandType::Reconfigure;
    cmd.controller = controller;
    cmd.config = &cfg;
    cmd.completion_sem = xSemaphoreCreateBinary();
    if (cmd.completion_sem == nullptr) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_ERR_TIMEOUT;
    }

    OutputCoreCommand* p_cmd = &cmd;
    if (xQueueSend(queue_, &p_cmd, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(lifecycle_mutex_);
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_FAIL;
    }

    xSemaphoreTake(cmd.completion_sem, portMAX_DELAY);
    xSemaphoreGive(lifecycle_mutex_);

    vSemaphoreDelete(cmd.completion_sem);
    return cmd.result;
}

esp_err_t OutputCoreExecutor::execute_pca_recovery(OutputController* controller) {
    if (controller == nullptr) return ESP_ERR_INVALID_ARG;

    // If caller is already executing on CPU1, invoke directly to prevent deadlocks
    if (xPortGetCoreID() == 1) {
        return controller->recover_pca_on_output_core();
    }

    esp_err_t err = start();
    if (err != ESP_OK) return err;

    OutputCoreCommand cmd{};
    cmd.type = OutputCoreCommandType::RecoverPca;
    cmd.controller = controller;
    cmd.completion_sem = xSemaphoreCreateBinary();
    if (cmd.completion_sem == nullptr) return ESP_ERR_NO_MEM;

    if (xSemaphoreTake(lifecycle_mutex_, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_ERR_TIMEOUT;
    }

    OutputCoreCommand* p_cmd = &cmd;
    if (xQueueSend(queue_, &p_cmd, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(lifecycle_mutex_);
        vSemaphoreDelete(cmd.completion_sem);
        return ESP_FAIL;
    }

    xSemaphoreTake(cmd.completion_sem, portMAX_DELAY);
    xSemaphoreGive(lifecycle_mutex_);

    vSemaphoreDelete(cmd.completion_sem);
    return cmd.result;
}

void OutputCoreExecutor::task_entry(void* arg) {
    static_cast<OutputCoreExecutor*>(arg)->run();
}

void OutputCoreExecutor::run() {
    executor_core_ = xPortGetCoreID();
    ESP_LOGI("output", "HW_DOMAIN cpu=%d", executor_core_);

    while (true) {
        OutputCoreCommand* cmd = nullptr;
        if (xQueueReceive(queue_, &cmd, portMAX_DELAY) == pdTRUE && cmd != nullptr) {
            ESP_LOGI("output", "Hardware lifecycle executing on core=%d", xPortGetCoreID());
            if (cmd->type == OutputCoreCommandType::Init) {
                cmd->result = cmd->controller->init_on_output_core();
            } else if (cmd->type == OutputCoreCommandType::Reconfigure) {
                if (cmd->config != nullptr) {
                    cmd->result = cmd->controller->reconfigure_on_output_core(*cmd->config);
                } else {
                    cmd->result = ESP_ERR_INVALID_ARG;
                }
            } else if (cmd->type == OutputCoreCommandType::RecoverPca) {
                cmd->result = cmd->controller->recover_pca_on_output_core();
            }
            if (cmd->completion_sem != nullptr) {
                xSemaphoreGive(cmd->completion_sem);
            }
        }
    }
}

} // namespace lg
