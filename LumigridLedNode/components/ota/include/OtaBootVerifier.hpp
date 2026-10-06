// components/ota/include/OtaBootVerifier.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "RecoveryTypes.hpp"
#include "OtaVerificationPolicy.hpp"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

struct PersistedOtaState {
    uint8_t schema = 2;
    OtaBootState state = OtaBootState::Idle;
    char previous_version[32] = "";
    char target_version[32] = "";
    char target_partition[16] = "";
    int32_t last_error = 0;
    char last_reason[64] = "";
    uint32_t candidate_boot_ms = 0;
    uint32_t verification_started_ms = 0;
    uint32_t verification_deadline_ms = 0;
    uint32_t candidate_reset_reason = 0;
    uint32_t checksum = 0;
};

class OtaBootVerifier {
public:
    static OtaBootVerifier& instance();

    esp_err_t init();

    // Check if current running app is in ESP_OTA_IMG_PENDING_VERIFY state
    bool is_pending_verify() const { return pending_verify_; }

    // Read current persisted OTA state
    PersistedOtaState state() const;

    // Save persisted OTA state to NVS
    esp_err_t save_state(const PersistedOtaState& st);

    // Call after core startup self-test passes to confirm candidate
    esp_err_t confirm_candidate();
    // Polled by the existing recovery worker; no permanent verification task.
    esp_err_t observe_runtime(bool healthy, bool fatal, bool critical_memory, uint32_t now_ms);

    // Call if core startup self-test fails fatally to rollback to previous firmware
    esp_err_t rollback_candidate(const char* reason, int32_t error_code = -1);

    // Query OTA status information for API
    OtaBootState boot_state() const { return state().state; }
    const char* running_version() const { return running_version_; }
    const char* target_version() const { return current_state_.target_version; }
    const char* previous_version() const { return current_state_.previous_version; }
    bool rolled_back() const { return boot_state() == OtaBootState::RolledBack; }
    const char* last_reason() const { return current_state_.last_reason; }

private:
    OtaBootVerifier() = default;

    std::atomic<bool> pending_verify_{false};
    bool dwell_started_ = false;
    bool dwell_satisfied_ = false;
    uint32_t candidate_boot_ms_ = 0;
    uint32_t healthy_since_ms_ = 0;
    char running_version_[32] = "";
    PersistedOtaState current_state_{};
    SemaphoreHandle_t mutex_ = nullptr;
};

} // namespace lg
