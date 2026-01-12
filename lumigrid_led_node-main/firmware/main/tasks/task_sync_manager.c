#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "sync_protocol.h"

static const char *TAG = "SYNC_MGR";

static void sync_tick_task(void *ctx) {
    (void)ctx;
    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        sync_tick_local(now_ms);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_sync_manager_start(void) {
    if (xTaskCreate(sync_tick_task, "sync_tick", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sync tick task");
    } else {
        ESP_LOGI(TAG, "Sync tick task started");
    }
}
