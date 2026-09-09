// components/signage/FixtureMapper.cpp
#include "FixtureMapper.hpp"
#include "GammaLut.hpp"
#include <algorithm>

namespace lg {
namespace {

uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((uint16_t(value) * scale) / 255);
}

} // namespace

void FixtureMapper::map(
    FixtureFrame& dest,
    const FixtureFrame& src,
    const FixtureConfig& fixture,
    uint8_t global_brightness
) {
    uint16_t pcount = std::min<uint16_t>(fixture.num_pixels, MAX_PIXELS);
    
    for (uint16_t i = 0; i < pcount; ++i) {
        Rgb c = src.pixels[i];
        
        // Apply global brightness
        c.r = scale8(c.r, global_brightness);
        c.g = scale8(c.g, global_brightness);
        c.b = scale8(c.b, global_brightness);
        
        // Apply white balance
        c.r = scale8(c.r, fixture.white_balance[0]);
        c.g = scale8(c.g, fixture.white_balance[1]);
        c.b = scale8(c.b, fixture.white_balance[2]);
        
        // Apply gamma correction
        if (fixture.gamma_correction) {
            c = apply_gamma(c);
        }
        
        dest.pixels[i] = c;
    }
    
    // Process PWM
    uint32_t duty = src.pwm;
    duty = (duty * global_brightness) / 255;
    
    if (fixture.gamma_correction) {
        // Approximate gamma for PWM channel if required (often handled by the PCA itself, but we can do simple squaring)
        // Since original code didn't explicitly gamma correct PWM duty cycles, we just pass it through or square it.
        // Looking at SignageEffects.cpp: "if (fixture.gamma_correction) c = apply_gamma(c); duty = c.r * 4095 / 255;"
        // We'll leave duty as is and let OutputScheduler derive it from pixels[0] if needed for RGB profiles.
    }
    dest.pwm = static_cast<uint16_t>(duty);
}

} // namespace lg
