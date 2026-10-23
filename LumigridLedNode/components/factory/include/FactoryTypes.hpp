// components/factory/include/FactoryTypes.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "CertificationTypes.hpp"

namespace lg {

enum class FactoryEffect : uint8_t {
    SafeBlackout,
    Identity,
    PixelChase,
    Rainbow,
    WhiteWalk,
    PwmWalk,
    PwmFade,
    CombinedPulse,
    StressCycle, // Formerly BurnIn: a 60-second diagnostic stress pattern
    BurnIn = StressCycle, // Retained for wire/enum compatibility
    HardwareCoexistence, // LG-HW-001: 8-lane RMT + 8-channel PCA continuous coexistence stress
    Manual
};

enum class FactoryTestState : uint8_t {
    Idle = 0,
    Preparing = 1,
    Running = 2,
    WaitingForOperator = 3,
    Passed = 4,
    Failed = 5,
    Aborted = 6
};

enum class FactoryTestKind : uint8_t {
    AddressableChannel = 0,
    PwmChannel = 1,
    CombinedOutputs = 2,
    SafetyGate = 3,
    BurnIn = 4
};

enum class FactoryTestStage : uint8_t {
    None = 0,

    Red,
    Green,
    Blue,
    White,
    Chase,
    Blackout,

    Pwm0,
    Pwm25,
    Pwm50,
    Pwm75,
    Pwm100,
};

inline const char* test_stage_name(FactoryTestStage stage) {
    switch (stage) {
        case FactoryTestStage::None: return "none";
        case FactoryTestStage::Red: return "red";
        case FactoryTestStage::Green: return "green";
        case FactoryTestStage::Blue: return "blue";
        case FactoryTestStage::White: return "white";
        case FactoryTestStage::Chase: return "chase";
        case FactoryTestStage::Blackout: return "blackout";
        case FactoryTestStage::Pwm0: return "pwm_0";
        case FactoryTestStage::Pwm25: return "pwm_25";
        case FactoryTestStage::Pwm50: return "pwm_50";
        case FactoryTestStage::Pwm75: return "pwm_75";
        case FactoryTestStage::Pwm100: return "pwm_100";
        default: return "none";
    }
}

inline const char* test_state_name(FactoryTestState state) {
    switch (state) {
        case FactoryTestState::Idle: return "idle";
        case FactoryTestState::Preparing: return "preparing";
        case FactoryTestState::Running: return "running";
        case FactoryTestState::WaitingForOperator: return "waiting_for_operator";
        case FactoryTestState::Passed: return "passed";
        case FactoryTestState::Failed: return "failed";
        case FactoryTestState::Aborted: return "aborted";
        default: return "unknown";
    }
}

inline const char* test_kind_name(FactoryTestKind kind) {
    switch (kind) {
        case FactoryTestKind::AddressableChannel: return "addressable_channel";
        case FactoryTestKind::PwmChannel: return "pwm_channel";
        case FactoryTestKind::CombinedOutputs: return "combined_outputs";
        case FactoryTestKind::SafetyGate: return "safety_gate";
        case FactoryTestKind::BurnIn: return "burn_in";
        default: return "unknown";
    }
}

inline bool parse_test_kind(const char* str, FactoryTestKind& kind) {
    if (!str) return false;
    if (strcmp(str, "addressable_channel") == 0 || strcmp(str, "addressable") == 0) {
        kind = FactoryTestKind::AddressableChannel;
        return true;
    }
    if (strcmp(str, "pwm_channel") == 0 || strcmp(str, "pwm") == 0) {
        kind = FactoryTestKind::PwmChannel;
        return true;
    }
    if (strcmp(str, "combined_outputs") == 0 || strcmp(str, "combined") == 0) {
        kind = FactoryTestKind::CombinedOutputs;
        return true;
    }
    if (strcmp(str, "safety_gate") == 0 || strcmp(str, "safety") == 0 || strcmp(str, "estop") == 0) {
        kind = FactoryTestKind::SafetyGate;
        return true;
    }
    if (strcmp(str, "burn_in") == 0 || strcmp(str, "burnin") == 0) {
        kind = FactoryTestKind::BurnIn;
        return true;
    }
    return false;
}

enum class FactorySessionOwner : uint8_t {
    ManualFactory = 0,
    Certification = 1,
};

struct FactoryTestSession {
    uint32_t session_id = 0;
    FactoryTestKind kind = FactoryTestKind::AddressableChannel;
    FactoryTestState state = FactoryTestState::Idle;
    uint8_t channel = 0xFF; // 0..15 or 0xFF if not channel-specific
    uint64_t started_ms = 0;
    uint64_t finished_ms = 0;
    uint32_t command_id = 0;
    bool operator_confirmation_required = false;
    int32_t error_code = 0;
    char notes[64] = {0};
    FactorySessionOwner owner = FactorySessionOwner::ManualFactory;
    char certification_test_id[kCertificationTestIdCapacity] = {0};
    FactoryTestStage stage = FactoryTestStage::None;
    uint8_t stage_index = 0;
    uint8_t stage_count = 0;
};

struct ChannelValidationStatus {
    bool tested = false;
    bool passed = false;
    uint32_t timestamp_ms = 0;
};

struct FactoryValidationReport {
    ChannelValidationStatus addressable[8]{};
    ChannelValidationStatus pwm[8]{};
    ChannelValidationStatus combined{};
    ChannelValidationStatus safety_gate{};
    ChannelValidationStatus burn_in{};
    uint32_t burn_in_duration_s = 0;
    uint32_t burn_in_errors = 0;
};

enum class FactoryCommandType : uint8_t {
    SelectEffect,
    PixelLaneSolid,
    PwmChannelLevel,
    StreamFrame,
    Abort,
    SetAutoLoop,
    ChannelTest,
    StartSession,
    ConfirmSession
};

struct StreamFramePayload {
    uint8_t lane_rgb[8][3] = {};
};

struct FactoryCommand {
    uint32_t command_id = 0;
    FactoryCommandType type = FactoryCommandType::Abort;
    FactoryEffect effect = FactoryEffect::SafeBlackout;

    uint8_t lane_or_channel = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    uint8_t brightness = 32;
    uint16_t pwm_duty = 0;
    uint32_t duration_ms = 0;
    bool enabled = false;

    // Session controls
    FactoryTestKind session_kind = FactoryTestKind::AddressableChannel;
    uint32_t session_id = 0;
    bool session_passed = false;
    char session_notes[64] = {0};
    FactorySessionOwner session_owner = FactorySessionOwner::ManualFactory;
    char session_cert_test_id[kCertificationTestIdCapacity] = {0};

    StreamFramePayload stream{};
};

struct FactoryStatus {
    FactoryEffect active_effect = FactoryEffect::SafeBlackout;
    uint32_t effect_started_ms = 0;
    uint32_t effect_duration_ms = 0;
    uint8_t brightness = 0;
    bool auto_loop = false;
    bool outputs_enabled = false;
    bool test_active = false;
    uint32_t command_rejections = 0;
    uint32_t commit_failures = 0;
    uint8_t active_channel = 0xFF;
    FactoryTestSession session{};
};

} // namespace lg

