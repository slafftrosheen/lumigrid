#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "rest_api.h"
#include "ui_server.h"

static const char *TAG = "UI_FS";

void task_ui_fs_start(void) {
    httpd_handle_t server = rest_api_get_handle();
    if (!server) {
        ESP_LOGE(TAG, "REST API not running; UI handlers not attached");
        return;
    }

    esp_err_t err = ui_server_attach(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UI server start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "UI server handlers registered");
    }
}
