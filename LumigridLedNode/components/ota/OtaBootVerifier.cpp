// components/ota/OtaBootVerifier.cpp
#include "OtaBootVerifier.hpp"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_rom_crc.h"

namespace lg {

static const char* TAG = "ota_verify";

OtaBootVerifier& OtaBootVerifier::instance() {
    static OtaBootVerifier inst;
    return inst;
}

static uint32_t calc_ota_state_crc(const PersistedOtaState& st) {
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&st), sizeof(PersistedOtaState) - sizeof(uint32_t));
}

esp_err_t OtaBootVerifier::init() {
    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) {
        strncpy(running_version_, desc->version, sizeof(running_version_) - 1);
        running_version_[sizeof(running_version_) - 1] = '\0';
    } else {
        strncpy(running_version_, "1.0.0", sizeof(running_version_) - 1);
    }

    // Load persisted state from NVS
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("ota_state", NVS_READWRITE, &handle);
    bool state_loaded = false;
    if (err == ESP_OK) {
        size_t len = sizeof(PersistedOtaState);
        PersistedOtaState loaded{};
        if (nvs_get_blob(handle, "state", &loaded, &len) == ESP_OK && len == sizeof(PersistedOtaState)) {
            if (loaded.schema == 1 && loaded.checksum == calc_ota_state_crc(loaded)) {
                current_state_ = loaded;
                state_loaded = true;
            }
        }
        nvs_close(handle);
    }

    if (!state_loaded) {
        current_state_.schema = 1;
        current_state_.state = OtaBootState::Idle;
        strncpy(current_state_.previous_version, running_version_, sizeof(current_state_.previous_version) - 1);
        strncpy(current_state_.target_version, running_version_, sizeof(current_state_.target_version) - 1);
        current_state_.target_partition[0] = '\0';
        current_state_.last_error = 0;
        current_state_.last_reason[0] = '\0';
        save_state(current_state_);
    }

    // Inspect running partition state
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        ESP_LOGI(TAG, "Running partition '%s' (0x%08" PRIx32 ", size %" PRIu32 "), OTA state: %d",
                 running->label, running->address, running->size, ota_state);
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Current firmware is PENDING VERIFICATION! Starting bounded self-test.");
            pending_verify_ = true;
            current_state_.state = OtaBootState::PendingVerify;
            strncpy(current_state_.target_version, running_version_, sizeof(current_state_.target_version) - 1);
            save_state(current_state_);
        }
    }

    // Check for bootloader rollback if we expected a newer version but booted on older
    if (!pending_verify_) {
        if (current_state_.state == OtaBootState::PendingReboot || current_state_.state == OtaBootState::PendingVerify) {
            if (strcmp(current_state_.target_version, running_version_) == 0) {
                // Booted matching target version cleanly
                current_state_.state = OtaBootState::Confirmed;
                save_state(current_state_);
            } else if (strlen(current_state_.target_version) > 0) {
                // Mismatch: Target was expected, but we booted old running version -> rolled back by bootloader!
                ESP_LOGE(TAG, "OTA Rollback detected! Target was '%s', running is '%s'",
                         current_state_.target_version, running_version_);
                current_state_.state = OtaBootState::RolledBack;
                strncpy(current_state_.last_reason, "bootloader_rollback_detected", sizeof(current_state_.last_reason) - 1);
                save_state(current_state_);
            }
        }
    }

    return ESP_OK;
}

esp_err_t OtaBootVerifier::save_state(const PersistedOtaState& st) {
    current_state_ = st;
    current_state_.checksum = calc_ota_state_crc(current_state_);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("ota_state", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, "state", &current_state_, sizeof(PersistedOtaState));
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t OtaBootVerifier::confirm_candidate() {
    if (pending_verify_) {
        ESP_LOGI(TAG, "Self-test passed! Confirming candidate app and cancelling rollback.");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
            return err;
        }
        pending_verify_ = false;
    }
    current_state_.state = OtaBootState::Confirmed;
    strncpy(current_state_.last_reason, "verified_ok", sizeof(current_state_.last_reason) - 1);
    save_state(current_state_);
    return ESP_OK;
}

esp_err_t OtaBootVerifier::rollback_candidate(const char* reason, int32_t error_code) {
    ESP_LOGE(TAG, "Critical candidate startup failure! Rolling back candidate. Reason: %s (err: %" PRId32 ")",
             reason ? reason : "unknown", error_code);
    current_state_.state = OtaBootState::RolledBack;
    current_state_.last_error = error_code;
    if (reason) {
        strncpy(current_state_.last_reason, reason, sizeof(current_state_.last_reason) - 1);
    }
    save_state(current_state_);

    if (pending_verify_) {
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    return ESP_FAIL;
}

} // namespace lg
