// components/config/include/RecoveryTypes.hpp
#pragma once
#include <cstdint>
#include "esp_err.h"
#include "AuthTypes.hpp"

namespace lg {

enum class OtaBootState : uint8_t {
    Idle,
    PendingReboot,
    PendingVerify,
    Confirmed,
    RolledBack,
    Failed
};

inline const char* ota_boot_state_to_name(OtaBootState st) {
    switch (st) {
        case OtaBootState::Idle:          return "idle";
        case OtaBootState::PendingReboot: return "pending_reboot";
        case OtaBootState::PendingVerify: return "pending_verify";
        case OtaBootState::Confirmed:     return "confirmed";
        case OtaBootState::RolledBack:    return "rolled_back";
        case OtaBootState::Failed:        return "failed";
        default:                          return "unknown";
    }
}

enum class RestoreState : uint8_t {
    Idle,
    Preparing,
    Restoring,
    Verifying,
    Interrupted,
    Complete,
    Failed
};

inline const char* restore_state_to_name(RestoreState st) {
    switch (st) {
        case RestoreState::Idle:        return "idle";
        case RestoreState::Preparing:   return "preparing";
        case RestoreState::Restoring:   return "restoring";
        case RestoreState::Verifying:   return "verifying";
        case RestoreState::Interrupted: return "interrupted";
        case RestoreState::Complete:    return "complete";
        case RestoreState::Failed:      return "failed";
        default:                        return "unknown";
    }
}

enum class MaintenanceMode : uint8_t {
    None,
    ConfigApply,
    Restore,
    Ota
};

inline const char* maintenance_mode_to_name(MaintenanceMode m) {
    switch (m) {
        case MaintenanceMode::None:        return "none";
        case MaintenanceMode::ConfigApply: return "config_apply";
        case MaintenanceMode::Restore:     return "restore";
        case MaintenanceMode::Ota:         return "ota";
        default:                           return "unknown";
    }
}

struct RecoveryStatus {
    bool safe_mode = false;
    bool config_valid = true;
    auth::CommissioningState commissioning_state = auth::CommissioningState::None;
    uint8_t startup_state = 0;   // Matches StartupState enum value
    uint8_t failing_stage = 0;   // Matches StartupStage enum value
    int32_t failing_error = 0;
    bool recovery_network_active = false;
    bool outputs_forced_off = false;
    OtaBootState ota_state = OtaBootState::Confirmed;
    RestoreState restore_state = RestoreState::Idle;
    char failure_reason[64] = "";
};

} // namespace lg
