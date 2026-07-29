// components/factory/include/FactoryTypes.hpp
#pragma once
#include <cstdint>

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
    BurnIn,
    Manual
};

enum class FactoryCommandType : uint8_t {
    SelectEffect,
    PixelLaneSolid,
    PwmChannelLevel,
    StreamFrame,
    Abort,
    SetAutoLoop
};

struct StreamFramePayload {
    uint8_t lane_rgb[8][3] = {};
};

struct FactoryCommand {
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
};

} // namespace lg
