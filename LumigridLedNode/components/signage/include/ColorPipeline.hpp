// components/signage/include/ColorPipeline.hpp
#pragma once
#include <cstdint>
#include <array>
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

class ColorPipeline {
public:
    /// Initialize or update dynamic gamma lookup tables from DeviceConfig::gamma_x10.
    static void init(uint8_t gamma_x10 = 22);
    static void set_gamma(uint8_t gamma_x10);
    static uint8_t get_gamma();

    /// Convert an RGB color to RGBW using subtractive white point extraction.
    static LightValue rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b);

    /// Authoritative conversion for addressable fixtures (WS2812, SK6812).
    /// Applies effective brightness, white balance, profile color space, and configurable gamma.
    static LightValue to_addressable(
        const LightValue& input,
        const FixtureConfig& fixture,
        uint8_t global_brightness
    );

    /// Authoritative conversion for PWM fixtures (PWM_Single, PWM_RGB, PWM_RGBW).
    /// Applies effective brightness, white balance, profile color space, and configurable 12-bit gamma,
    /// producing calibrated 12-bit duty cycles (0..4095) for up to 4 physical channels.
    static void to_pwm(
        const LightValue& input,
        uint16_t single_pwm_in,
        const FixtureConfig& fixture,
        uint8_t global_brightness,
        std::array<uint16_t, 4>& out_duties
    );

    // Backward-compatible wrappers
    static inline LightValue process_pixel(const LightValue& input, const FixtureConfig& fixture, uint8_t global_brightness) {
        return to_addressable(input, fixture, global_brightness);
    }

    static inline void process_pwm(const LightValue& input, uint16_t single_pwm_in, const FixtureConfig& fixture, uint8_t global_brightness, std::array<uint16_t, 4>& out_duties) {
        to_pwm(input, single_pwm_in, fixture, global_brightness, out_duties);
    }

private:
    static uint8_t gamma8_lut_[256];
    static uint16_t gamma12_lut_[256];
    static uint8_t current_gamma_x10_;
    static bool initialized_;
};

} // namespace lg
