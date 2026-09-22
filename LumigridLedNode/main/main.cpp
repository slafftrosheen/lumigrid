#include "LumigridRuntime.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
    static lg::LumigridRuntime runtime;
    runtime.init();
    runtime.start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI("lumigrid", "Heartbeat: state=%s, free_heap=%lu",
                 runtime.startup_state_str(), (unsigned long)esp_get_free_heap_size());
    }
}
