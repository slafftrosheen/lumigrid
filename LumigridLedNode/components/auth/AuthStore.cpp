// components/auth/AuthStore.cpp
#include "AuthStore.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

namespace lg::auth {
namespace {

static const char* TAG = "auth_store";
static const char* kNvsNamespace = "node_auth";
static const char* kKeyPrimary = "auth_cfg";
static const char* kKeyBackup = "auth_cfg_b";

} // namespace

esp_err_t AuthStore::init() {
    if (initialized_) {
        return ESP_OK;
    }

    AuthConfig loaded{};
    esp_err_t err = load(loaded);
    if (err == ESP_OK) {
        config_ = loaded;
        initialized_ = true;
        ESP_LOGI(TAG, "AuthStore initialized (provisioned=%d, schema=%u)",
                 config_.provisioned ? 1 : 0, config_.schema_version);
        return ESP_OK;
    }

    // Storage faults are not first boot. Fail closed instead of restoring setup access.
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NOT_FOUND) return err;
    // First boot or unformatted: initialize default unprovisioned configuration
    config_ = AuthConfig{};
    config_.schema_version = 1;
    config_.provisioned = false;
    config_.checksum = calc_auth_config_crc(config_);
    initialized_ = true;

    ESP_LOGI(TAG, "AuthStore starting with fresh unprovisioned configuration");
    return ESP_OK;
}

esp_err_t AuthStore::load(AuthConfig& out_cfg) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    AuthConfig candidate{};
    size_t size = sizeof(candidate);
    bool primary_ok = false;
    bool stored_data = false;

    // 1. Try primary slot
    err = nvs_get_blob(handle, kKeyPrimary, &candidate, &size);
    stored_data = err != ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_OK && size == sizeof(candidate)) {
        if (candidate.schema_version == 1 && candidate.checksum == calc_auth_config_crc(candidate)) {
            primary_ok = true;
            out_cfg = candidate;
        } else {
            ESP_LOGW(TAG, "Primary auth config failed CRC or schema version check");
        }
    }

    // 2. Try backup slot if primary invalid
    bool backup_ok = false;
    if (!primary_ok) {
        size = sizeof(candidate);
        err = nvs_get_blob(handle, kKeyBackup, &candidate, &size);
        stored_data = stored_data || err != ESP_ERR_NVS_NOT_FOUND;
        if (err == ESP_OK && size == sizeof(candidate)) {
            if (candidate.schema_version == 1 && candidate.checksum == calc_auth_config_crc(candidate)) {
                backup_ok = true;
                out_cfg = candidate;
                ESP_LOGI(TAG, "Recovered auth config from backup slot");
            }
        }
    }

    nvs_close(handle);

    if (primary_ok) {
        return ESP_OK;
    }

    if (backup_ok) {
        // Self-heal primary slot
        save(out_cfg);
        return ESP_OK;
    }

    return stored_data ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
}

esp_err_t AuthStore::save(const AuthConfig& cfg) {
    AuthConfig to_save = cfg;
    to_save.schema_version = 1;
    to_save.checksum = calc_auth_config_crc(to_save);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace %s: %s", kNvsNamespace, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, kKeyPrimary, &to_save, sizeof(to_save));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kKeyBackup, &to_save, sizeof(to_save));
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) {
            config_ = to_save;
            initialized_ = true;
        }
    } else {
        ESP_LOGE(TAG, "Failed to save auth config: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t AuthStore::erase() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_all(handle);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) return err;
    config_ = AuthConfig{};
    config_.schema_version = 1;
    config_.provisioned = false;
    config_.checksum = calc_auth_config_crc(config_);
    ESP_LOGI(TAG, "AuthStore erased to factory defaults");
    return ESP_OK;
}

} // namespace lg::auth
