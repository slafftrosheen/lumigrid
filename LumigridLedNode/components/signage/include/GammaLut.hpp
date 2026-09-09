// components/signage/include/GammaLut.hpp
// Perceptual gamma correction for LEDs and PWM channels.
// Generates compile-time LUTs so no runtime computation is needed.
#pragma once
#include <cstdint>
#include "RenderTypes.hpp"

namespace lg {

/// 8-bit → 8-bit gamma table for addressable LED channels (γ = 2.2).
extern const uint8_t kGamma8[256];

/// 8-bit → 12-bit gamma table for PCA9685 PWM (γ = 2.2, scaled 0–4095).
extern const uint16_t kGamma12[256];

/// Apply gamma correction to an RGB pixel value.
inline Rgb apply_gamma(Rgb c) {
    return {kGamma8[c.r], kGamma8[c.g], kGamma8[c.b]};
}

/// Apply gamma correction to map 8-bit brightness → 12-bit PCA9685 duty.
inline uint16_t apply_gamma_pwm(uint8_t brightness) {
    return kGamma12[brightness];
}

} // namespace lg
