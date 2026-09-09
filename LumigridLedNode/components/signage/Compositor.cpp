// components/signage/Compositor.cpp
#include "Compositor.hpp"
#include <algorithm>

namespace lg {
namespace {

Rgb blend_rgb(const Rgb& bg, const Rgb& fg, BlendMode mode, uint8_t fg_opacity) {
    if (fg_opacity == 0) return bg;
    
    Rgb blended = fg;
    switch (mode) {
        case BlendMode::Normal: {
            if (fg_opacity == 255) return fg;
            blended.r = bg.r + (((fg.r - bg.r) * fg_opacity) >> 8);
            blended.g = bg.g + (((fg.g - bg.g) * fg_opacity) >> 8);
            blended.b = bg.b + (((fg.b - bg.b) * fg_opacity) >> 8);
            break;
        }
        case BlendMode::Add: {
            uint16_t r = bg.r + ((fg.r * fg_opacity) >> 8);
            uint16_t g = bg.g + ((fg.g * fg_opacity) >> 8);
            uint16_t b = bg.b + ((fg.b * fg_opacity) >> 8);
            blended.r = r > 255 ? 255 : r;
            blended.g = g > 255 ? 255 : g;
            blended.b = b > 255 ? 255 : b;
            break;
        }
        case BlendMode::Multiply: {
            uint16_t mr = (bg.r * fg.r) >> 8;
            uint16_t mg = (bg.g * fg.g) >> 8;
            uint16_t mb = (bg.b * fg.b) >> 8;
            blended.r = bg.r + (((mr - bg.r) * fg_opacity) >> 8);
            blended.g = bg.g + (((mg - bg.g) * fg_opacity) >> 8);
            blended.b = bg.b + (((mb - bg.b) * fg_opacity) >> 8);
            break;
        }
        case BlendMode::Screen: {
            uint16_t sr = 255 - (((255 - bg.r) * (255 - fg.r)) >> 8);
            uint16_t sg = 255 - (((255 - bg.g) * (255 - fg.g)) >> 8);
            uint16_t sb = 255 - (((255 - bg.b) * (255 - fg.b)) >> 8);
            blended.r = bg.r + (((sr - bg.r) * fg_opacity) >> 8);
            blended.g = bg.g + (((sg - bg.g) * fg_opacity) >> 8);
            blended.b = bg.b + (((sb - bg.b) * fg_opacity) >> 8);
            break;
        }
    }
    return blended;
}

uint16_t blend_pwm(uint16_t bg, uint16_t fg, BlendMode mode, uint8_t fg_opacity) {
    if (fg_opacity == 0) return bg;
    
    switch (mode) {
        case BlendMode::Normal:
            if (fg_opacity == 255) return fg;
            return bg + (((fg - bg) * fg_opacity) >> 8);
        case BlendMode::Add: {
            uint32_t a = bg + ((fg * fg_opacity) >> 8);
            return a > 4095 ? 4095 : a;
        }
        case BlendMode::Multiply: {
            uint16_t m = (uint32_t(bg) * fg) / 4095;
            return bg + (((m - bg) * fg_opacity) >> 8);
        }
        case BlendMode::Screen: {
            uint32_t s = 4095 - (((4095 - bg) * (4095 - fg)) / 4095);
            return bg + (((s - bg) * fg_opacity) >> 8);
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
        dest.pixels[i] = blend_rgb(dest.pixels[i], src.pixels[i], mode, opacity);
    }
    dest.pwm = blend_pwm(dest.pwm, src.pwm, mode, opacity);
}

} // namespace lg
