// components/signage/include/SignageEffects.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"
#include "OutputController.hpp"

namespace lg {

/// Renders a single lane's effect using the given channel configuration.
void render_lane_effect(
    uint8_t lane,
    OutputController& output,
    const ChannelConfig& channel,
    uint8_t global_brightness,
    uint32_t elapsed_ms
);

} // namespace lg
