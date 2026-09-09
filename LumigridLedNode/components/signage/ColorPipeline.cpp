// components/signage/ColorPipeline.cpp
#include "ColorPipeline.hpp"
#include "GammaLut.hpp"
#include <algorithm>

namespace lg {
namespace {

inline uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((uint16_t(value) * scale) / 255);
}

} // namespace

LightValue ColorPipeline::rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t w = std::min({r, g, b});
    return LightValue(r - w, g - w, b - w, w);
}

LightValue ColorPipeline::process_pixel(
    const LightValue& input,
    const FixtureConfig& fixture,
    uint8_t global_brightness
) {
    const uint8_t effective_brightness = scale8(global_brightness, fixture.max_brightness);
    LightValue c = input;

    // 1. Effective Brightness
    c.r = scale8(c.r, effective_brightness);
    c.g = scale8(c.g, effective_brightness);
    c.b = scale8(c.b, effective_brightness);
    c.w = scale8(c.w, effective_brightness);

    // 2. White Balance
    c.r = scale8(c.r, fixture.white_balance[0]);
    c.g = scale8(c.g, fixture.white_balance[1]);
    c.b = scale8(c.b, fixture.white_balance[2]);

    // 3. RGBW Conversion (for SK6812)
    if (fixture.profile == FixtureProfile::SK6812) {
        uint8_t min_rgb = std::min({c.r, c.g, c.b});
        c.r -= min_rgb;
        c.g -= min_rgb;
        c.b -= min_rgb;
        c.w = min_rgb;
    }

    // 4. Perceptual Gamma Correction (addressable 8-bit table)
    if (fixture.gamma_correction) {
        c.r = kGamma8[c.r];
        c.g = kGamma8[c.g];
        c.b = kGamma8[c.b];
        c.w = kGamma8[c.w];
    }

    return c;
}

void ColorPipeline::process_pwm(
    const LightValue& input,
    uint16_t single_pwm_in,
    const FixtureConfig& fixture,
    uint8_t global_brightness,
    std::array<uint16_t, 4>& out_duties
) {
    out_duties[0] = 0;
    out_duties[1] = 0;
    out_duties[2] = 0;
    out_duties[3] = 0;

    const uint8_t effective_brightness = scale8(global_brightness, fixture.max_brightness);

    if (fixture.profile == FixtureProfile::PWM_Single) {
        uint32_t duty = (static_cast<uint32_t>(single_pwm_in) * effective_brightness) / 255;
        if (fixture.gamma_correction) {
            uint8_t b8 = static_cast<uint8_t>((duty * 255) / 4095);
            duty = apply_gamma_pwm(b8);
        }
        out_duties[0] = static_cast<uint16_t>(duty);
    } else {
        // PWM_RGB or PWM_RGBW
        LightValue c = input;
        c.r = scale8(c.r, effective_brightness);
        c.g = scale8(c.g, effective_brightness);
        c.b = scale8(c.b, effective_brightness);
        c.r = scale8(c.r, fixture.white_balance[0]);
        c.g = scale8(c.g, fixture.white_balance[1]);
        c.b = scale8(c.b, fixture.white_balance[2]);

        if (fixture.profile == FixtureProfile::PWM_RGBW) {
            uint8_t min_rgb = std::min({c.r, c.g, c.b});
            c.r -= min_rgb;
            c.g -= min_rgb;
            c.b -= min_rgb;
            c.w = min_rgb;
        }

        if (fixture.gamma_correction) {
            out_duties[0] = apply_gamma_pwm(c.r);
            out_duties[1] = apply_gamma_pwm(c.g);
            out_duties[2] = apply_gamma_pwm(c.b);
            out_duties[3] = (fixture.profile == FixtureProfile::PWM_RGBW) ? apply_gamma_pwm(c.w) : 0;
        } else {
            out_duties[0] = (static_cast<uint32_t>(c.r) * 4095) / 255;
            out_duties[1] = (static_cast<uint32_t>(c.g) * 4095) / 255;
            out_duties[2] = (static_cast<uint32_t>(c.b) * 4095) / 255;
            out_duties[3] = (fixture.profile == FixtureProfile::PWM_RGBW) ? ((static_cast<uint32_t>(c.w) * 4095) / 255) : 0;
        }
    }
}

} // namespace lg
