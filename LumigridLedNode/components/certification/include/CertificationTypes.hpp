// components/certification/include/CertificationTypes.hpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace lg {

constexpr size_t kCertificationTestIdCapacity = 24;

enum class EvidenceSource : uint8_t {
    Automatic = 0,
    OperatorObservation = 1,
    InstrumentMeasurement = 2,
};

inline const char* evidence_source_to_string(EvidenceSource src) {
    switch (src) {
        case EvidenceSource::Automatic:             return "automatic";
        case EvidenceSource::OperatorObservation:   return "operator_observation";
        case EvidenceSource::InstrumentMeasurement: return "instrument_measurement";
        default:                                   return "unknown";
    }
}

inline bool parse_evidence_source(const char* str, EvidenceSource& src) {
    if (!str) return false;
    if (strcmp(str, "automatic") == 0 || strcmp(str, "auto") == 0) {
        src = EvidenceSource::Automatic;
        return true;
    }
    if (strcmp(str, "operator_observation") == 0 || strcmp(str, "operator") == 0) {
        src = EvidenceSource::OperatorObservation;
        return true;
    }
    if (strcmp(str, "instrument_measurement") == 0 || strcmp(str, "instrument") == 0) {
        src = EvidenceSource::InstrumentMeasurement;
        return true;
    }
    return false;
}

enum class CertificationState : uint8_t {
    NotStarted = 0,
    InProgress = 1,
    Passed = 2,
    Failed = 3,
    Incomplete = 4,
    Invalidated = 5,
};

inline const char* certification_state_to_string(CertificationState st) {
    switch (st) {
        case CertificationState::NotStarted:  return "not_started";
        case CertificationState::InProgress:  return "in_progress";
        case CertificationState::Passed:      return "passed";
        case CertificationState::Failed:      return "failed";
        case CertificationState::Incomplete:  return "incomplete";
        case CertificationState::Invalidated: return "invalidated";
        default:                              return "unknown";
    }
}

inline bool parse_certification_state(const char* str, CertificationState& st) {
    if (!str) return false;
    if (strcmp(str, "not_started") == 0) { st = CertificationState::NotStarted; return true; }
    if (strcmp(str, "in_progress") == 0) { st = CertificationState::InProgress; return true; }
    if (strcmp(str, "passed") == 0)      { st = CertificationState::Passed; return true; }
    if (strcmp(str, "failed") == 0)      { st = CertificationState::Failed; return true; }
    if (strcmp(str, "incomplete") == 0)  { st = CertificationState::Incomplete; return true; }
    if (strcmp(str, "invalidated") == 0) { st = CertificationState::Invalidated; return true; }
    return false;
}

enum class CertificationTestState : uint8_t {
    Pending = 0,
    Running = 1,
    WaitingForOperator = 2,
    Passed = 3,
    Failed = 4,
    Skipped = 5,
    Blocked = 6,
};

inline const char* cert_test_state_to_string(CertificationTestState st) {
    switch (st) {
        case CertificationTestState::Pending:            return "pending";
        case CertificationTestState::Running:            return "running";
        case CertificationTestState::WaitingForOperator: return "waiting_for_operator";
        case CertificationTestState::Passed:             return "passed";
        case CertificationTestState::Failed:             return "failed";
        case CertificationTestState::Skipped:            return "skipped";
        case CertificationTestState::Blocked:            return "blocked";
        default:                                         return "unknown";
    }
}

inline bool parse_cert_test_state(const char* str, CertificationTestState& st) {
    if (!str) return false;
    if (strcmp(str, "pending") == 0)              { st = CertificationTestState::Pending; return true; }
    if (strcmp(str, "running") == 0)              { st = CertificationTestState::Running; return true; }
    if (strcmp(str, "waiting_for_operator") == 0) { st = CertificationTestState::WaitingForOperator; return true; }
    if (strcmp(str, "passed") == 0)               { st = CertificationTestState::Passed; return true; }
    if (strcmp(str, "failed") == 0)               { st = CertificationTestState::Failed; return true; }
    if (strcmp(str, "skipped") == 0)              { st = CertificationTestState::Skipped; return true; }
    if (strcmp(str, "blocked") == 0)              { st = CertificationTestState::Blocked; return true; }
    return false;
}

enum class RequirementLevel : uint8_t {
    Required = 0,
    Conditional = 1,
    Optional = 2,
};

inline const char* requirement_level_to_string(RequirementLevel lvl) {
    switch (lvl) {
        case RequirementLevel::Required:    return "required";
        case RequirementLevel::Conditional: return "conditional";
        case RequirementLevel::Optional:    return "optional";
        default:                            return "unknown";
    }
}

inline bool parse_requirement_level(const char* str, RequirementLevel& lvl) {
    if (!str) return false;
    if (strcmp(str, "required") == 0)    { lvl = RequirementLevel::Required; return true; }
    if (strcmp(str, "conditional") == 0) { lvl = RequirementLevel::Conditional; return true; }
    if (strcmp(str, "optional") == 0)    { lvl = RequirementLevel::Optional; return true; }
    return false;
}

enum class CertificationProfile : uint8_t {
    Development = 0,
    ProductionUnit = 1,
    ReleaseCandidate = 2,
};

inline const char* certification_profile_to_string(CertificationProfile prof) {
    switch (prof) {
        case CertificationProfile::Development:      return "development";
        case CertificationProfile::ProductionUnit:   return "production_unit";
        case CertificationProfile::ReleaseCandidate: return "release_candidate";
        default:                                     return "unknown";
    }
}

inline bool parse_certification_profile(const char* str, CertificationProfile& prof) {
    if (!str) return false;
    if (strcmp(str, "development") == 0 || strcmp(str, "dev") == 0) {
        prof = CertificationProfile::Development;
        return true;
    }
    if (strcmp(str, "production_unit") == 0 || strcmp(str, "production") == 0 || strcmp(str, "prod") == 0) {
        prof = CertificationProfile::ProductionUnit;
        return true;
    }
    if (strcmp(str, "release_candidate") == 0 || strcmp(str, "rc") == 0) {
        prof = CertificationProfile::ReleaseCandidate;
        return true;
    }
    return false;
}

struct CertificationIdentity {
    char certification_id[32] = {0};
    char node_id[24] = {0};
    char firmware_version[32] = {0};
    char project_name[32] = {0};
    char hardware_revision[16] = {0};
    char serial_number[32] = {0};
    uint16_t plan_version = 0;
};

struct CertificationTestDefinition {
    const char* id;
    const char* category;
    const char* title;
    EvidenceSource required_evidence;
    RequirementLevel req_dev;
    RequirementLevel req_prod;
    RequirementLevel req_rc;
    uint32_t timeout_ms;
    float expected_value;
    float tolerance;
    const char* unit;

    RequirementLevel requirement_for_profile(CertificationProfile prof) const {
        switch (prof) {
            case CertificationProfile::Development:      return req_dev;
            case CertificationProfile::ProductionUnit:   return req_prod;
            case CertificationProfile::ReleaseCandidate: return req_rc;
            default:                                     return RequirementLevel::Required;
        }
    }
};

struct CertificationTestResult {
    char test_id[kCertificationTestIdCapacity] = {0};
    CertificationTestState state = CertificationTestState::Pending;
    EvidenceSource evidence_source = EvidenceSource::Automatic;
    uint64_t started_ms = 0;
    uint64_t completed_ms = 0;
    int32_t error_code = 0;
    float measured_value = 0.0f;
    char unit[8] = {0};
    char note[96] = {0};
    uint8_t attempts = 0;
};

struct BurnInMetrics {
    uint32_t duration_s = 0;
    uint32_t commit_failures = 0;
    uint32_t output_faults = 0;
    uint32_t min_free_heap = 0;
    uint32_t start_free_heap = 0;
    uint32_t end_free_heap = 0;
    uint32_t network_disconnects = 0;
    uint32_t watchdog_resets = 0;
};

struct CertificationSession {
    CertificationIdentity identity{};
    CertificationProfile profile = CertificationProfile::Development;
    CertificationState state = CertificationState::NotStarted;
    uint64_t started_ms = 0;
    uint64_t completed_ms = 0;
    uint16_t test_count = 0;
    uint16_t passed_count = 0;
    uint16_t failed_count = 0;
    uint16_t skipped_count = 0;
    uint32_t config_fingerprint = 0;
    uint32_t checksum = 0;
};

struct CertificationProbeSnapshot {
    bool runtime_ready = false;

    bool api_running = false;
    uint32_t authenticated_ws_clients = 0;

    bool ap_active = false;
    bool sta_connected = false;
    bool mdns_running = false;

    bool backup_serializer_healthy = false;

    uint32_t reset_reason = 0;
};

} // namespace lg
