// components/certification/CertificationPlan.cpp
#include "CertificationPlan.hpp"
#include <cstring>

namespace lg {

namespace {

// Static const table of all certification tests in Plan v1
static constexpr CertificationTestDefinition kPlanV1Tests[] = {
    // ── BOOT ──
    { "BOOT-001", "BOOT", "Boot Sequence & Identity Assertion",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      5000, 0.0f, 0.0f, "" },

    { "BOOT-OUTPUT-SAFE", "BOOT", "Boot Safe-State Output Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    { "BOOT-REBOOT-001", "BOOT", "Reboot Configuration Persistence",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      15000, 0.0f, 0.0f, "" },

    { "BOOT-SAFE-001", "BOOT", "Safe Mode Boot & Recovery Isolation",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      15000, 0.0f, 0.0f, "" },

    // ── NETWORK ──
    { "NET-AP-001", "NETWORK", "Access Point Reachability & DHCP",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    { "NET-STA-001", "NETWORK", "Station Mode Association & IP Assignment",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      15000, 0.0f, 0.0f, "" },

    { "NET-RECONNECT-001", "NETWORK", "Network Interruption & Resilient Reconnection",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    { "NET-DHCP-001", "NETWORK", "DHCP Route Reconciliation",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      15000, 0.0f, 0.0f, "" },

    // ── ADDRESSABLE OUTPUTS ──
    { "ADDR-A1", "ADDRESSABLE OUTPUTS", "Addressable Channel A1 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A2", "ADDRESSABLE OUTPUTS", "Addressable Channel A2 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A3", "ADDRESSABLE OUTPUTS", "Addressable Channel A3 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A4", "ADDRESSABLE OUTPUTS", "Addressable Channel A4 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A5", "ADDRESSABLE OUTPUTS", "Addressable Channel A5 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A6", "ADDRESSABLE OUTPUTS", "Addressable Channel A6 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A7", "ADDRESSABLE OUTPUTS", "Addressable Channel A7 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },
    { "ADDR-A8", "ADDRESSABLE OUTPUTS", "Addressable Channel A8 Full Sequence",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    { "ADDR-XTALK", "ADDRESSABLE OUTPUTS", "Addressable Crosstalk Isolation",
      EvidenceSource::OperatorObservation, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    // ── PWM OUTPUTS ──
    { "PWM-P1", "PWM OUTPUTS", "PWM Channel P1 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P2", "PWM OUTPUTS", "PWM Channel P2 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P3", "PWM OUTPUTS", "PWM Channel P3 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P4", "PWM OUTPUTS", "PWM Channel P4 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P5", "PWM OUTPUTS", "PWM Channel P5 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P6", "PWM OUTPUTS", "PWM Channel P6 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P7", "PWM OUTPUTS", "PWM Channel P7 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },
    { "PWM-P8", "PWM OUTPUTS", "PWM Channel P8 Stepped Duty & Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 50.0f, 3.0f, "percent" },

    { "PWM-XTALK", "PWM OUTPUTS", "PWM Crosstalk & Inactive Port Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    // ── OUTPUT SAFETY ──
    { "SAFE-ESTOP-LOGIC", "OUTPUT SAFETY", "E-Stop Logic Latch & Suppression",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      5000, 0.0f, 0.0f, "" },

    { "SAFE-ESTOP-PWM", "OUTPUT SAFETY", "E-Stop Hardware PWM Inhibit",
      EvidenceSource::InstrumentMeasurement, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 5000.0f, "us" },

    { "SAFE-ESTOP-ADDRESSABLE", "OUTPUT SAFETY", "E-Stop Addressable Blackout Protocol",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    { "SAFE-DISABLE-001", "OUTPUT SAFETY", "Output Disable Hardware Inhibit",
      EvidenceSource::OperatorObservation, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    { "SAFE-WATCHDOG-001", "OUTPUT SAFETY", "Watchdog Input Timeout Fallback",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    // ── POWER ──
    { "POWER-LIMIT-001", "POWER", "Power Limiter Load Measurement",
      EvidenceSource::InstrumentMeasurement, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      300000, 0.0f, 5000.0f, "mW" },

    // ── DMX ──
    { "DMX-ARTNET-001", "DMX", "Art-Net Universe Ingestion & Port Mapping",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    { "DMX-SACN-001", "DMX", "sACN Universe Ingestion & Port Mapping",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    // ── RECOVERY ──
    { "REC-BACKUP-001", "RECOVERY", "Configuration & Show Backup Export",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    { "REC-RESTORE-001", "RECOVERY", "Backup Package Restore Verification",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    { "REC-INTERRUPT-001", "RECOVERY", "Interrupted Restore Recovery Gate",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    // ── OTA ──
    { "OTA-GOOD-001", "OTA", "OTA Candidate Image Verification",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      60000, 0.0f, 0.0f, "" },

    { "OTA-ROLLBACK-001", "OTA", "OTA Validation Failure Rollback",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      60000, 0.0f, 0.0f, "" },

    // ── BURN-IN ──
    { "BURN-001", "BURN-IN", "Hardware Endurance Burn-In",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Required, RequirementLevel::Required,
      259200000, 0.0f, 0.0f, "" }, // up to 72 hours timeout

    // ── API ──
    { "API-001", "API", "Canonical HTTP API Smoke & Contract",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    { "WS-001", "API", "WebSocket Schema 3 Stream Monotonicity",
      EvidenceSource::Automatic, RequirementLevel::Required, RequirementLevel::Required, RequirementLevel::Required,
      10000, 0.0f, 0.0f, "" },

    // ── MULTI-NODE SYNC ──
    { "SYNC-ID-001", "MULTI-NODE SYNC", "Multi-Node Discovery & Unique Immutable Identity",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      15000, 0.0f, 0.0f, "" },

    { "SYNC-LEADER-001", "MULTI-NODE SYNC", "Leader Election Consensus Stability",
      EvidenceSource::Automatic, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      30000, 0.0f, 0.0f, "" },

    { "SYNC-SCENE-001", "MULTI-NODE SYNC", "Synchronized Scene Transition Physical Test",
      EvidenceSource::OperatorObservation, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      300000, 0.0f, 0.0f, "" },

    { "SYNC-TIMING-001", "MULTI-NODE SYNC", "Physical Output Transition Timing Offset",
      EvidenceSource::InstrumentMeasurement, RequirementLevel::Optional, RequirementLevel::Optional, RequirementLevel::Required,
      300000, 0.0f, 5000.0f, "us" }
};

static constexpr size_t kPlanV1TestCount = sizeof(kPlanV1Tests) / sizeof(kPlanV1Tests[0]);

} // namespace

size_t CertificationPlan::test_count() {
    return kPlanV1TestCount;
}

const CertificationTestDefinition* CertificationPlan::test_definitions() {
    return kPlanV1Tests;
}

const CertificationTestDefinition* CertificationPlan::find_test(const char* test_id) {
    if (!test_id) return nullptr;
    for (size_t i = 0; i < kPlanV1TestCount; ++i) {
        if (strcmp(kPlanV1Tests[i].id, test_id) == 0) {
            return &kPlanV1Tests[i];
        }
    }
    return nullptr;
}

int CertificationPlan::find_test_index(const char* test_id) {
    if (!test_id) return -1;
    for (size_t i = 0; i < kPlanV1TestCount; ++i) {
        if (strcmp(kPlanV1Tests[i].id, test_id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

RequirementLevel CertificationPlan::get_requirement(const char* test_id, CertificationProfile profile) {
    const auto* def = find_test(test_id);
    if (!def) return RequirementLevel::Optional;
    return def->requirement_for_profile(profile);
}

uint32_t CertificationPlan::get_timeout_ms(const char* test_id) {
    const auto* def = find_test(test_id);
    return def ? def->timeout_ms : 30000;
}

EvidenceSource CertificationPlan::get_required_evidence(const char* test_id) {
    const auto* def = find_test(test_id);
    return def ? def->required_evidence : EvidenceSource::Automatic;
}

} // namespace lg
