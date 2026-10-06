// components/ota/OtaBootVerifier.cpp
#include "OtaBootVerifier.hpp"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_system.h"

namespace lg {

static const char* TAG = "ota_verify";
class StateGuard {
public:
    explicit StateGuard(SemaphoreHandle_t mutex) : mutex_(mutex) { if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    ~StateGuard() { if (mutex_) xSemaphoreGiveRecursive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};
PersistedOtaState OtaBootVerifier::state() const {
    StateGuard guard(mutex_);
    return current_state_;
}

OtaBootVerifier& OtaBootVerifier::instance() {
    static OtaBootVerifier inst;
    return inst;
}

static uint32_t calc_ota_state_crc(const PersistedOtaState& st) {
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&st), sizeof(PersistedOtaState) - sizeof(uint32_t));
}

esp_err_t OtaBootVerifier::init() {
    if (!mutex_) mutex_ = xSemaphoreCreateRecursiveMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;
    StateGuard guard(mutex_);
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
            if (loaded.schema == 2 && loaded.checksum == calc_ota_state_crc(loaded)) {
                current_state_ = loaded;
                state_loaded = true;
            }
        }
        if (!state_loaded) {
            // Keep prior update identity/reason when upgrading the persisted schema.
            struct LegacyState {
                uint8_t schema; OtaBootState state;
                char previous_version[32]; char target_version[32];
                char target_partition[16]; int32_t last_error; char last_reason[64];
                uint32_t checksum;
            } old{};
            len = sizeof(old);
            if (nvs_get_blob(handle, "state", &old, &len) == ESP_OK && len == sizeof(old) &&
                old.schema == 1 && old.checksum == esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&old), sizeof(old)-4)) {
                current_state_.state = old.state;
                memcpy(current_state_.previous_version, old.previous_version, sizeof(old.previous_version));
                memcpy(current_state_.target_version, old.target_version, sizeof(old.target_version));
                memcpy(current_state_.target_partition, old.target_partition, sizeof(old.target_partition));
                current_state_.last_error = old.last_error;
                memcpy(current_state_.last_reason, old.last_reason, sizeof(old.last_reason));
                state_loaded = true;
            }
        }
        nvs_close(handle);
    }

    if (!state_loaded) {
        current_state_.schema = 2;
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
            candidate_boot_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            dwell_started_ = false;
            dwell_satisfied_ = false;
            current_state_.candidate_boot_ms = candidate_boot_ms_;
            current_state_.candidate_reset_reason = esp_reset_reason();
            current_state_.verification_started_ms = 0;
            current_state_.verification_deadline_ms = candidate_boot_ms_ + kOtaVerificationTimeoutMs;
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
                if (!current_state_.last_reason[0]) {
                    const auto reset = esp_reset_reason();
                    const char* reason = (reset == ESP_RST_TASK_WDT || reset == ESP_RST_INT_WDT || reset == ESP_RST_WDT)
                        ? "watchdog_reset" : "bootloader_rollback_detected";
                    strncpy(current_state_.last_reason, reason, sizeof(current_state_.last_reason) - 1);
                }
                save_state(current_state_);
            }
        }
    }

    return ESP_OK;
}

esp_err_t OtaBootVerifier::save_state(const PersistedOtaState& st) {
    StateGuard guard(mutex_);
    current_state_ = st;
    current_state_.checksum = calc_ota_state_crc(current_state_);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("ota_state", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, "state", &current_state_, sizeof(PersistedOtaState));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t OtaBootVerifier::confirm_candidate() {
    StateGuard guard(mutex_);
    if (pending_verify_) {
        if (!dwell_satisfied_) return ESP_ERR_INVALID_STATE;
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
    return save_state(current_state_);
}

esp_err_t OtaBootVerifier::observe_runtime(bool healthy, bool fatal, bool critical_memory, uint32_t now_ms) {
    StateGuard guard(mutex_);
    if (!pending_verify_) return ESP_OK;
    if (healthy && !dwell_started_) {
        healthy_since_ms_ = now_ms;
        dwell_started_ = true;
        current_state_.verification_started_ms = now_ms;
        current_state_.verification_deadline_ms = now_ms + kOtaVerificationDwellMs;
        const auto err = save_state(current_state_);
        if (err != ESP_OK) return err;
    } else if (!healthy && dwell_started_) {
        dwell_started_ = false;
        current_state_.verification_started_ms = 0;
        current_state_.verification_deadline_ms = candidate_boot_ms_ + kOtaVerificationTimeoutMs;
        save_state(current_state_);
    }
    switch (ota_verification_action(healthy, fatal, critical_memory, now_ms, candidate_boot_ms_, healthy_since_ms_, dwell_started_)) {
    case OtaVerificationAction::Confirm:
        dwell_satisfied_ = true;
        return confirm_candidate();
    case OtaVerificationAction::RollbackStartup: return rollback_candidate("startup_failure");
    case OtaVerificationAction::RollbackMemory: return rollback_candidate("memory_critical");
    case OtaVerificationAction::RollbackTimeout: return rollback_candidate("verification_timeout");
    default: return ESP_OK;
    }
}

esp_err_t OtaBootVerifier::rollback_candidate(const char* reason, int32_t error_code) {
    StateGuard guard(mutex_);
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
