// components/signage/include/ColorPipeline.hpp
#pragma once
#include <cstdint>
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

class ColorPipeline {
public:
    /// Convert an RGB color to RGBW using subtractive white point extraction.
    static LightValue rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b);

    /// Process an addressable pixel:
    /// Applies effective brightness, white balance, profile color space (RGB/RGBW), and gamma.
    static LightValue process_pixel(
        const LightValue& input,
        const FixtureConfig& fixture,
        uint8_t global_brightness
    );

    /// Process a PWM fixture:
    /// Applies effective brightness, white balance, profile color space, and perceptual gamma,
    /// producing calibrated 12-bit duty cycles (0..4095) for up to 4 physical channels.
    static void process_pwm(
        const LightValue& input,
        uint16_t single_pwm_in,
        const FixtureConfig& fixture,
        uint8_t global_brightness,
        std::array<uint16_t, 4>& out_duties
    );
};

} // namespace lg
