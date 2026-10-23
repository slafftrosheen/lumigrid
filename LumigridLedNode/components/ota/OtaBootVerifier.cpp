// components/ota/OtaBootVerifier.cpp
#include "OtaBootVerifier.hpp"
#include "esp_log.h"
#include "nvs.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <algorithm>

namespace lg {

static const char* TAG = "ota_verify";
class StateGuard {
public:
    explicit StateGuard(SemaphoreHandle_t mutex) : mutex_(mutex) { if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    ~StateGuard() { if (mutex_) xSemaphoreGiveRecursive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};

namespace {
template<size_t N> bool terminated(const char (&value)[N]) {
    return memchr(value, '\0', N) != nullptr;
}

template<size_t N> void copy_text(char (&dest)[N], const char* source) {
    strncpy(dest, source ? source : "", N - 1);
    dest[N - 1] = '\0';
}

uint32_t calc_ota_state_crc(const PersistedOtaState& st) {
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&st), sizeof(st) - sizeof(st.checksum));
}

template<class T> bool fields_valid(const T& st) {
    return st.state <= OtaBootState::Failed && terminated(st.previous_version) &&
        terminated(st.target_version) && terminated(st.target_partition) && terminated(st.last_reason);
}

esp_err_t load_state(PersistedOtaState& state, bool& loaded) {
    loaded = false;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("ota_state", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    size_t len = 0;
    err = nvs_get_blob(handle, "state", nullptr, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err == ESP_OK && len == sizeof(PersistedOtaState)) {
        PersistedOtaState candidate{};
        err = nvs_get_blob(handle, "state", &candidate, &len);
        if (err == ESP_OK) {
            if (len != sizeof(candidate) || candidate.schema != 2 || !fields_valid(candidate) ||
                candidate.checksum != calc_ota_state_crc(candidate)) {
                err = ESP_ERR_INVALID_CRC;
            } else {
                state = candidate;
                loaded = true;
            }
        }
    } else if (err == ESP_OK) {
        struct LegacyState {
            uint8_t schema; OtaBootState state;
            char previous_version[32]; char target_version[32];
            char target_partition[16]; int32_t last_error; char last_reason[64];
            uint32_t checksum;
        } old{};
        if (len != sizeof(old)) {
            err = ESP_ERR_INVALID_CRC;
        } else {
            err = nvs_get_blob(handle, "state", &old, &len);
            if (err == ESP_OK) {
                if (len != sizeof(old) || old.schema != 1 || !fields_valid(old) ||
                    old.checksum != esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&old), sizeof(old) - 4)) {
                    err = ESP_ERR_INVALID_CRC;
                } else {
                    state = PersistedOtaState{};
                    state.state = old.state;
                    copy_text(state.previous_version, old.previous_version);
                    copy_text(state.target_version, old.target_version);
                    copy_text(state.target_partition, old.target_partition);
                    state.last_error = old.last_error;
                    copy_text(state.last_reason, old.last_reason);
                    loaded = true;
                }
            }
        }
    }
    nvs_close(handle);
    return err;
}
} // namespace

PersistedOtaState OtaBootVerifier::state() const {
    StateGuard guard(mutex_);
    return current_state_;
}

OtaBootVerifier& OtaBootVerifier::instance() {
    static OtaBootVerifier inst;
    return inst;
}

esp_err_t OtaBootVerifier::init() {
    if (!mutex_) mutex_ = xSemaphoreCreateRecursiveMutex();
    if (!mutex_) {
        persistence_error_ = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    StateGuard guard(mutex_);
    initialized_ = true;
    pending_verify_ = false;
    rollback_requested_ = false;
    dwell_started_ = false;
    dwell_satisfied_ = false;
    candidate_boot_ms_ = 0;
    healthy_since_ms_ = 0;
    current_state_ = PersistedOtaState{};
    running_version_[0] = '\0';

    const esp_app_desc_t* desc = esp_app_get_description();
    const bool valid_version = desc && terminated(desc->version) && desc->version[0];
    if (valid_version) copy_text(running_version_, desc->version);

    bool loaded = false;
    const esp_err_t load_err = load_state(current_state_, loaded);
    persistence_error_ = load_err;
    if (!loaded) {
        copy_text(current_state_.previous_version, running_version_);
        copy_text(current_state_.target_version, running_version_);
    }

    // The bootloader's partition state is authoritative even when the journal
    // cannot be read. Keep pending verification visible so failed startup can
    // still request rollback instead of silently bypassing verification.
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t image_err = running ? esp_ota_get_state_partition(running, &image_state) : ESP_ERR_NOT_FOUND;
    if (image_err == ESP_OK && image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        pending_verify_ = true;
        candidate_boot_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        current_state_.state = OtaBootState::PendingVerify;
        current_state_.candidate_boot_ms = candidate_boot_ms_;
        current_state_.candidate_reset_reason = esp_reset_reason();
        current_state_.verification_started_ms = 0;
        current_state_.verification_deadline_ms = candidate_boot_ms_ + kOtaVerificationTimeoutMs;
        if (!loaded) current_state_.previous_version[0] = '\0';
        copy_text(current_state_.target_version, running_version_);
        copy_text(current_state_.target_partition, running->label);
        if (load_err != ESP_OK || !valid_version) {
            current_state_.last_error = load_err != ESP_OK ? load_err : ESP_ERR_INVALID_ARG;
            copy_text(current_state_.last_reason, "candidate_metadata_unavailable");
            return current_state_.last_error;
        }
        ESP_LOGW(TAG, "Candidate partition '%s' requires bounded runtime verification", running->label);
        return save_state(current_state_);
    }

    const bool factory_without_ota_state = running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY &&
        image_err == ESP_ERR_NOT_SUPPORTED;
    if (load_err != ESP_OK || !valid_version || (image_err != ESP_OK && !factory_without_ota_state)) {
        current_state_.state = OtaBootState::Failed;
        current_state_.last_error = load_err != ESP_OK ? load_err : (!valid_version ? ESP_ERR_INVALID_ARG : image_err);
        copy_text(current_state_.last_reason, load_err != ESP_OK ? "ota_journal_unavailable" : "ota_state_unavailable");
        return current_state_.last_error;
    }

    const bool tracked_attempt = current_state_.state == OtaBootState::PendingReboot ||
        current_state_.state == OtaBootState::PendingVerify || current_state_.state == OtaBootState::Failed;
    if (tracked_attempt) {
        const auto* target = current_state_.target_partition[0]
            ? esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, current_state_.target_partition)
            : nullptr;
        const bool target_running = target && running && target->address == running->address;
        esp_ota_img_states_t target_state = ESP_OTA_IMG_UNDEFINED;
        const bool target_state_known = target && esp_ota_get_state_partition(target, &target_state) == ESP_OK;
        if (target_running && image_state == ESP_OTA_IMG_VALID &&
            strcmp(current_state_.target_version, running_version_) == 0) {
            current_state_.state = OtaBootState::Confirmed;
            current_state_.last_error = 0;
            copy_text(current_state_.last_reason, "bootloader_valid");
        } else if (!target_running && target_state_known &&
                   (target_state == ESP_OTA_IMG_INVALID || target_state == ESP_OTA_IMG_ABORTED)) {
            current_state_.state = OtaBootState::RolledBack;
            if (!current_state_.last_reason[0]) {
                const auto reset = esp_reset_reason();
                copy_text(current_state_.last_reason,
                    reset == ESP_RST_TASK_WDT || reset == ESP_RST_INT_WDT || reset == ESP_RST_WDT
                    ? "watchdog_reset" : "bootloader_rollback_detected");
            }
        } else if (current_state_.state != OtaBootState::Failed) {
            // Journal preparation can precede boot selection. A power cut in
            // that interval is an incomplete update, not a verified candidate
            // or proof that the bootloader rolled back.
            current_state_.state = OtaBootState::Failed;
            current_state_.last_error = ESP_ERR_INVALID_STATE;
            copy_text(current_state_.last_reason, target_running ? "candidate_state_unverified" : "candidate_not_booted");
        } else {
            return ESP_OK;
        }
        return save_state(current_state_);
    }
    return ESP_OK;
}

esp_err_t OtaBootVerifier::save_state(const PersistedOtaState& st) {
    if (!mutex_) return ESP_ERR_INVALID_STATE;
    StateGuard guard(mutex_);
    if (!initialized_ || st.schema != 2 || !fields_valid(st)) return ESP_ERR_INVALID_ARG;
    PersistedOtaState next = st;
    next.checksum = calc_ota_state_crc(next);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("ota_state", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "state", &next, sizeof(next));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    persistence_error_ = err;
    // Upload intent is published only after persistence succeeds. Callers
    // applying an observed bootloader transition may already have updated the
    // in-memory state; persistence failure never reverses that hardware fact.
    if (err == ESP_OK) current_state_ = next;
    return err;
}

esp_err_t OtaBootVerifier::record_update_failure(const char* reason, int32_t error_code) {
    StateGuard guard(mutex_);
    if (!initialized_ || pending_verify_) return ESP_ERR_INVALID_STATE;
    current_state_.state = OtaBootState::Failed;
    current_state_.last_error = error_code;
    copy_text(current_state_.last_reason, reason);
    return save_state(current_state_);
}

esp_err_t OtaBootVerifier::confirm_candidate() {
    StateGuard guard(mutex_);
    if (!pending_verify_ || !dwell_satisfied_ || rollback_requested_) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        current_state_.last_error = err;
        return err;
    }
    pending_verify_ = false;
    current_state_.state = OtaBootState::Confirmed;
    current_state_.last_error = 0;
    copy_text(current_state_.last_reason, "verified_ok");
    return save_state(current_state_);
}

esp_err_t OtaBootVerifier::observe_runtime(bool healthy, bool fatal, bool critical_memory, uint32_t now_ms) {
    StateGuard guard(mutex_);
    if (!pending_verify_) return ESP_OK;
    if (rollback_requested_) return current_state_.last_error != ESP_OK ? current_state_.last_error : ESP_ERR_INVALID_STATE;
    const auto action = ota_verification_action(healthy, fatal, critical_memory,
        now_ms, candidate_boot_ms_, healthy_since_ms_, dwell_started_);
    // Failure handling must not be skipped because the journal is unavailable.
    if (action == OtaVerificationAction::RollbackStartup) return rollback_candidate("startup_failure");
    if (action == OtaVerificationAction::RollbackMemory) return rollback_candidate("memory_critical");
    if (action == OtaVerificationAction::RollbackTimeout) return rollback_candidate("verification_timeout");

    if (healthy && !dwell_started_) {
        auto next = current_state_;
        next.verification_started_ms = now_ms;
        next.verification_deadline_ms = now_ms + std::min(kOtaVerificationDwellMs,
            kOtaVerificationTimeoutMs - (now_ms - candidate_boot_ms_));
        const auto err = save_state(next);
        if (err != ESP_OK) return err;
        healthy_since_ms_ = now_ms;
        dwell_started_ = true;
    } else if (!healthy && dwell_started_) {
        dwell_started_ = false;
        dwell_satisfied_ = false;
        auto next = current_state_;
        next.verification_started_ms = 0;
        next.verification_deadline_ms = candidate_boot_ms_ + kOtaVerificationTimeoutMs;
        return save_state(next);
    }
    if (action == OtaVerificationAction::Confirm) {
        dwell_satisfied_ = true;
        return confirm_candidate();
    }
    return ESP_OK;
}

esp_err_t OtaBootVerifier::rollback_failed_startup(const char* reason, int32_t error_code) {
    {
        StateGuard guard(mutex_);
        if (rollback_requested_) return current_state_.last_error != ESP_OK ? current_state_.last_error : ESP_ERR_INVALID_STATE;
    }
    // Product initialization can fail before the normal verifier entry point.
    // NVS is best-effort here: its failure cannot prevent the bootloader from
    // rolling back an image whose startup has already failed.
    if (!initialized_) init();

    StateGuard guard(mutex_);
    if (!pending_verify_) {
        const auto* running = esp_ota_get_running_partition();
        if (!running) return ESP_ERR_NOT_FOUND;
        esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
        const esp_err_t err = esp_ota_get_state_partition(running, &image_state);
        if (err == ESP_ERR_NOT_SUPPORTED && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY)
            return ESP_ERR_INVALID_STATE;
        if (err != ESP_OK) return err;
        if (image_state != ESP_OTA_IMG_PENDING_VERIFY) return ESP_ERR_INVALID_STATE;

        // This path also covers a failed mutex allocation. Startup is faulted
        // and the caller is the sole recovery owner; no allocation or journal
        // is needed to request the authoritative rollback operation.
        pending_verify_ = true;
        const auto* description = esp_app_get_description();
        if (description && terminated(description->version)) copy_text(running_version_, description->version);
        copy_text(current_state_.target_version, running_version_);
        copy_text(current_state_.target_partition, running->label);
    }
    return rollback_candidate(reason, error_code);
}

esp_err_t OtaBootVerifier::rollback_candidate(const char* reason, int32_t error_code) {
    StateGuard guard(mutex_);
    if (!pending_verify_) return ESP_ERR_INVALID_STATE;
    if (rollback_requested_) return current_state_.last_error != ESP_OK ? current_state_.last_error : ESP_ERR_INVALID_STATE;
    rollback_requested_ = true;
    dwell_started_ = false;
    dwell_satisfied_ = false;
    current_state_.state = OtaBootState::Failed;
    current_state_.last_error = error_code;
    copy_text(current_state_.last_reason, reason ? reason : "candidate_failure");
    save_state(current_state_);

    // Successful rollback resets the CPU and never returns. Until a subsequent
    // boot observes a different partition and an invalid/aborted target, expose
    // Failed rather than claiming a rollback that may be impossible.
    esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err == ESP_OK) err = ESP_FAIL;
    current_state_.last_error = err;
    save_state(current_state_);
    ESP_LOGE(TAG, "Candidate rollback failed: %s", esp_err_to_name(err));
    return err;
}

} // namespace lg
