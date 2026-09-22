// components/signage/Compositor.cpp
#include "Compositor.hpp"
#include <algorithm>

namespace lg {
namespace {

LightValue blend_light(const LightValue& bg, const LightValue& fg, BlendMode mode, uint8_t fg_opacity) {
    if (fg_opacity == 0) return bg;

    auto blend_channel = [mode, fg_opacity](uint8_t b_val, uint8_t f_val) -> uint8_t {
        switch (mode) {
            case BlendMode::Normal: {
                if (fg_opacity == 255) return f_val;
                int32_t delta = static_cast<int32_t>(f_val) - static_cast<int32_t>(b_val);
                int32_t result = static_cast<int32_t>(b_val) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
                return static_cast<uint8_t>(std::clamp<int32_t>(result, 0, 255));
            }
            case BlendMode::Add: {
                uint32_t add = static_cast<uint32_t>(b_val) + (static_cast<uint32_t>(f_val) * fg_opacity) / 255;
                return static_cast<uint8_t>(std::min<uint32_t>(add, 255));
            }
            case BlendMode::Multiply: {
                uint32_t mult = (static_cast<uint32_t>(b_val) * f_val) / 255;
                int32_t delta = static_cast<int32_t>(mult) - static_cast<int32_t>(b_val);
                int32_t result = static_cast<int32_t>(b_val) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
                return static_cast<uint8_t>(std::clamp<int32_t>(result, 0, 255));
            }
            case BlendMode::Screen: {
                uint32_t screen = 255 - ((static_cast<uint32_t>(255 - b_val) * (255 - f_val)) / 255);
                int32_t delta = static_cast<int32_t>(screen) - static_cast<int32_t>(b_val);
                int32_t result = static_cast<int32_t>(b_val) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
                return static_cast<uint8_t>(std::clamp<int32_t>(result, 0, 255));
            }
        }
        return f_val;
    };

    return {
        blend_channel(bg.r, fg.r),
        blend_channel(bg.g, fg.g),
        blend_channel(bg.b, fg.b),
        blend_channel(bg.w, fg.w)
    };
}

uint16_t blend_pwm(uint16_t bg, uint16_t fg, BlendMode mode, uint8_t fg_opacity) {
    if (fg_opacity == 0) return bg;
    
    switch (mode) {
        case BlendMode::Normal: {
            if (fg_opacity == 255) return fg;
            int32_t delta = static_cast<int32_t>(fg) - static_cast<int32_t>(bg);
            int32_t result = static_cast<int32_t>(bg) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
            return static_cast<uint16_t>(std::clamp<int32_t>(result, 0, 4095));
        }
        case BlendMode::Add: {
            uint32_t a = bg + (static_cast<uint32_t>(fg) * fg_opacity) / 255;
            return static_cast<uint16_t>(std::min<uint32_t>(a, 4095));
        }
        case BlendMode::Multiply: {
            uint32_t m = (static_cast<uint32_t>(bg) * fg) / 4095;
            int32_t delta = static_cast<int32_t>(m) - static_cast<int32_t>(bg);
            int32_t result = static_cast<int32_t>(bg) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
            return static_cast<uint16_t>(std::clamp<int32_t>(result, 0, 4095));
        }
        case BlendMode::Screen: {
            uint32_t s = 4095 - ((static_cast<uint32_t>(4095 - bg) * (4095 - fg)) / 4095);
            int32_t delta = static_cast<int32_t>(s) - static_cast<int32_t>(bg);
            int32_t result = static_cast<int32_t>(bg) + ((delta * static_cast<int32_t>(fg_opacity)) / 255);
            return static_cast<uint16_t>(std::clamp<int32_t>(result, 0, 4095));
        }
    }
    return fg;
}

} // namespace

void Compositor::blend(
    FixtureFrame& dest,
    const FixtureFrame& src,
    BlendMode mode,
    uint8_t opacity,
    uint16_t num_pixels
) {
    if (opacity == 0) return;

    uint16_t pcount = std::min<uint16_t>(num_pixels, MAX_PIXELS);
    for (uint16_t i = 0; i < pcount; ++i) {
        dest.pixels[i] = blend_light(dest.pixels[i], src.pixels[i], mode, opacity);
    }
    dest.pwm = blend_pwm(dest.pwm, src.pwm, mode, opacity);
}

} // namespace lg
