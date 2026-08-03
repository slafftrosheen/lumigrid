// components/signage/include/SignageEffects.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"
#include "OutputController.hpp"

namespace lg {

/// Renders a single frame of a signage effect using the given preset's
/// channel configuration. Both pixel strips and PWM channels are driven
/// simultaneously for synchronized halo + contour animation.
void render_signage_effect(
    SignageEffect effect,
    OutputController& output,
    const BrandPreset& preset,
    uint32_t elapsed_ms
);

} // namespace lg
