// components/ota/include/OtaBootVerifier.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "RecoveryTypes.hpp"

namespace lg {

struct PersistedOtaState {
    uint8_t schema = 1;
    OtaBootState state = OtaBootState::Idle;
    char previous_version[32] = "";
    char target_version[32] = "";
    char target_partition[16] = "";
    int32_t last_error = 0;
    char last_reason[64] = "";
    uint32_t checksum = 0;
};

class OtaBootVerifier {
public:
    static OtaBootVerifier& instance();

    esp_err_t init();

    // Check if current running app is in ESP_OTA_IMG_PENDING_VERIFY state
    bool is_pending_verify() const { return pending_verify_; }

    // Read current persisted OTA state
    PersistedOtaState state() const { return current_state_; }

    // Save persisted OTA state to NVS
    esp_err_t save_state(const PersistedOtaState& st);

    // Call after core startup self-test passes to confirm candidate
    esp_err_t confirm_candidate();

    // Call if core startup self-test fails fatally to rollback to previous firmware
    esp_err_t rollback_candidate(const char* reason, int32_t error_code = -1);

    // Query OTA status information for API
    OtaBootState boot_state() const { return current_state_.state; }
    const char* running_version() const { return running_version_; }
    const char* target_version() const { return current_state_.target_version; }
    const char* previous_version() const { return current_state_.previous_version; }
    bool rolled_back() const { return current_state_.state == OtaBootState::RolledBack; }
    const char* last_reason() const { return current_state_.last_reason; }

private:
    OtaBootVerifier() = default;

    bool pending_verify_ = false;
    char running_version_[32] = "";
    PersistedOtaState current_state_{};
};

} // namespace lg
