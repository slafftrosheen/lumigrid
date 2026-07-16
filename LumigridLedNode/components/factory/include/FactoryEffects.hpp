#pragma once
#include <cstdint>
#include "OutputController.hpp"

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

const char* effect_name(FactoryEffect effect);

void render_effect(
    FactoryEffect effect,
    OutputController& output,
    uint32_t elapsed_ms,
    uint8_t brightness
);

} // namespace lg
