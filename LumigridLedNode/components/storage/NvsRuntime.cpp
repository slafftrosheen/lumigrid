#include "NvsRuntime.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

namespace lg {

static const char* TAG = "storage_nvs";
static bool s_ready = false;
static esp_err_t s_last_error = ESP_OK;
static bool s_destructive_recovery_required = false;

esp_err_t NvsRuntime::initialise() {
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        s_last_error = err;
        s_destructive_recovery_required = true;
        ESP_LOGE(TAG, "STORAGE_NVS init_failed reason=no_free_pages destructiveRecoveryRequired=1");
        return err;
    }
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        s_last_error = err;
        s_destructive_recovery_required = true;
        ESP_LOGE(TAG, "STORAGE_NVS init_failed reason=new_version_found destructiveRecoveryRequired=1");
        return err;
    }
    if (err != ESP_OK) {
        s_last_error = err;
        ESP_LOGE(TAG, "STORAGE_NVS init_failed err=%s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    s_last_error = ESP_OK;
    s_destructive_recovery_required = false;
    ESP_LOGI(TAG, "STORAGE_NVS initialised successfully");
    return ESP_OK;
}

bool NvsRuntime::ready() {
    return s_ready;
}

esp_err_t NvsRuntime::last_error() {
    return s_last_error;
}

bool NvsRuntime::destructive_recovery_required() {
    return s_destructive_recovery_required;
}

} // namespace lg
