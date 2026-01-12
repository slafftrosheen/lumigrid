#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "rest_api.h"

static const char *TAG = "REST_SERVER";

void task_rest_server_start(void) {
    esp_err_t err = rest_api_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "REST server start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "REST server ready");
    }
}
