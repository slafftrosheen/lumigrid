// components/signage/ColorPipeline.cpp
#include "ColorPipeline.hpp"
#include <algorithm>
#include <cmath>

namespace lg {
namespace {

inline uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((uint16_t(value) * scale) / 255);
}

} // namespace

uint8_t ColorPipeline::gamma8_lut_[256] = {};
uint16_t ColorPipeline::gamma12_lut_[256] = {};
uint8_t ColorPipeline::current_gamma_x10_ = 0;
bool ColorPipeline::initialized_ = false;

void ColorPipeline::init(uint8_t gamma_x10) {
    set_gamma(gamma_x10);
}

void ColorPipeline::set_gamma(uint8_t gamma_x10) {
    current_gamma_x10_ = gamma_x10;
    if (gamma_x10 == 0) {
        // Linear (gamma 1.0)
        for (int i = 0; i < 256; ++i) {
            gamma8_lut_[i] = static_cast<uint8_t>(i);
            gamma12_lut_[i] = static_cast<uint16_t>((i * 4095) / 255);
        }
    } else {
        const float gamma = static_cast<float>(gamma_x10) / 10.0f;
        for (int i = 0; i < 256; ++i) {
            const float norm = static_cast<float>(i) / 255.0f;
            const float curved = std::pow(norm, gamma);
            gamma8_lut_[i] = static_cast<uint8_t>(std::round(curved * 255.0f));
            gamma12_lut_[i] = static_cast<uint16_t>(std::round(curved * 4095.0f));
        }
    }
    initialized_ = true;
}

uint8_t ColorPipeline::get_gamma() {
    return current_gamma_x10_;
}

LightValue ColorPipeline::rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t w = std::min({r, g, b});
    return LightValue(r - w, g - w, b - w, w);
}

LightValue ColorPipeline::to_addressable(
    const LightValue& input,
    const FixtureConfig& fixture,
    uint8_t global_brightness
) {
    if (!initialized_) {
        init(22);
    }

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
        if (input.w == 0) {
            // Subtractive extraction if no explicit white channel was supplied
            uint8_t min_rgb = std::min({c.r, c.g, c.b});
            c.r -= min_rgb;
            c.g -= min_rgb;
            c.b -= min_rgb;
            c.w = min_rgb;
        }
    }

    // 4. Perceptual Gamma Correction
    if (fixture.gamma_correction) {
        c.r = gamma8_lut_[c.r];
        c.g = gamma8_lut_[c.g];
        c.b = gamma8_lut_[c.b];
        c.w = gamma8_lut_[c.w];
    }

    return c;
}

void ColorPipeline::to_pwm(
    const LightValue& input,
    uint16_t single_pwm_in,
    const FixtureConfig& fixture,
    uint8_t global_brightness,
    std::array<uint16_t, 4>& out_duties
) {
    if (!initialized_) {
        init(22);
    }

    out_duties[0] = 0;
    out_duties[1] = 0;
    out_duties[2] = 0;
    out_duties[3] = 0;

    const uint8_t effective_brightness = scale8(global_brightness, fixture.max_brightness);

    if (fixture.profile == FixtureProfile::PWM_Single) {
        uint32_t val = (single_pwm_in > 0) ? single_pwm_in : static_cast<uint32_t>(std::max({input.r, input.g, input.b, input.w}));
        uint32_t duty = (val * effective_brightness) / 255;
        if (fixture.gamma_correction) {
            uint8_t b8 = static_cast<uint8_t>((duty * 255) / 4095);
            duty = gamma12_lut_[b8];
        }
        out_duties[0] = static_cast<uint16_t>(duty);
    } else {
        // PWM_RGB or PWM_RGBW
        LightValue c = input;
        c.r = scale8(c.r, effective_brightness);
        c.g = scale8(c.g, effective_brightness);
        c.b = scale8(c.b, effective_brightness);
        c.w = scale8(c.w, effective_brightness);

        c.r = scale8(c.r, fixture.white_balance[0]);
        c.g = scale8(c.g, fixture.white_balance[1]);
        c.b = scale8(c.b, fixture.white_balance[2]);

        // Note: For PWM_RGBW, we treat input.w authoritatively without destructive subtractive extraction.
        if (fixture.gamma_correction) {
            out_duties[0] = gamma12_lut_[c.r];
            out_duties[1] = gamma12_lut_[c.g];
            out_duties[2] = gamma12_lut_[c.b];
            out_duties[3] = (fixture.profile == FixtureProfile::PWM_RGBW) ? gamma12_lut_[c.w] : 0;
        } else {
            out_duties[0] = static_cast<uint16_t>((static_cast<uint32_t>(c.r) * 4095) / 255);
            out_duties[1] = static_cast<uint16_t>((static_cast<uint32_t>(c.g) * 4095) / 255);
            out_duties[2] = static_cast<uint16_t>((static_cast<uint32_t>(c.b) * 4095) / 255);
            out_duties[3] = (fixture.profile == FixtureProfile::PWM_RGBW)
                ? static_cast<uint16_t>((static_cast<uint32_t>(c.w) * 4095) / 255)
                : 0;
        }
    }
}

} // namespace lg
