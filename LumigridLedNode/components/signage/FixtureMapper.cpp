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
    
    // 1. Process addressable pixels through ColorPipeline
    for (uint16_t i = 0; i < pcount; ++i) {
        dest.pixels[i] = ColorPipeline::process_pixel(src.pixels[i], fixture, global_brightness);
    }
    
    // 2. Process PWM channels through ColorPipeline
    const bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                         fixture.profile == FixtureProfile::PWM_RGB ||
                         fixture.profile == FixtureProfile::PWM_RGBW);
    if (is_pwm) {
        ColorPipeline::process_pwm(src.pixels[0], src.pwm[0], fixture, global_brightness, dest.pwm);
        dest.pixels[0] = ColorPipeline::process_pixel(src.pixels[0], fixture, global_brightness);
    } else {
        dest.pwm[0] = 0;
        dest.pwm[1] = 0;
        dest.pwm[2] = 0;
        dest.pwm[3] = 0;
    }
}

} // namespace lg
