// components/signage/FixtureMapper.cpp
#include "FixtureMapper.hpp"
#include "ColorPipeline.hpp"
#include <algorithm>

namespace lg {

void FixtureMapper::map(
    FixtureFrame& dest,
    const FixtureFrame& src,
    const FixtureConfig& fixture,
    uint8_t global_brightness
) {
    const uint16_t pcount = std::min<uint16_t>(fixture.num_pixels, MAX_PIXELS);
    
    const bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                         fixture.profile == FixtureProfile::PWM_RGB ||
                         fixture.profile == FixtureProfile::PWM_RGBW);
    if (is_pwm) {
        ColorPipeline::to_pwm(src.pixels[0], src.pwm[0], fixture, global_brightness, dest.pwm);
        // Zero out addressable representation to guarantee LightValue single source of truth
        for (uint16_t i = 0; i < pcount; ++i) {
            dest.pixels[i] = LightValue{0, 0, 0, 0};
        }
    } else {
        dest.pwm[0] = 0;
        dest.pwm[1] = 0;
        dest.pwm[2] = 0;
        dest.pwm[3] = 0;
        for (uint16_t i = 0; i < pcount; ++i) {
            dest.pixels[i] = ColorPipeline::to_addressable(src.pixels[i], fixture, global_brightness);
        }
    }
}

} // namespace lg
