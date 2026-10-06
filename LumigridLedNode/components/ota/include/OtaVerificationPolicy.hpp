#pragma once
#include <cstdint>
namespace lg {
constexpr uint32_t kOtaVerificationDwellMs = 10000;
constexpr uint32_t kOtaVerificationTimeoutMs = 60000;
enum class OtaVerificationAction { Wait, Confirm, RollbackStartup, RollbackMemory, RollbackTimeout };
inline OtaVerificationAction ota_verification_action(bool healthy, bool fatal, bool critical_memory,
                                                     uint32_t now, uint32_t candidate_boot,
                                                     uint32_t healthy_since, bool dwell_started) {
    if (fatal) return OtaVerificationAction::RollbackStartup;
    if (critical_memory) return OtaVerificationAction::RollbackMemory;
    if (now - candidate_boot >= kOtaVerificationTimeoutMs) return OtaVerificationAction::RollbackTimeout;
    if (healthy && dwell_started && now - healthy_since >= kOtaVerificationDwellMs)
        return OtaVerificationAction::Confirm;
    return OtaVerificationAction::Wait;
}
}
