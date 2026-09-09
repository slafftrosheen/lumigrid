// components/signage/EffectRenderer.cpp
#include "EffectRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace lg {
namespace {

uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((uint16_t(value) * scale) / 255);
}

Rgb scale_rgb(const BrandColor& c, uint8_t scale) {
    return {
        scale8(c.r, scale),
        scale8(c.g, scale),
        scale8(c.b, scale)
    };
}

uint16_t scale_pwm(uint16_t duty, uint8_t scale) {
    return static_cast<uint16_t>((uint32_t(duty) * scale) / 255);
}

uint8_t sine8(uint8_t phase) {
    const float x = phase * (3.14159265359f / 128.0f);
    return static_cast<uint8_t>(127.5f - 127.5f * std::cos(x));
}

uint32_t xorshift(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint8_t effect_parameter(const LayerConfig& layer) {
    const EffectMetadata& metadata = signage_effect_metadata(static_cast<SignageEffect>(layer.effect));
    if (!metadata.has_parameter) return 0;
    if (layer.fx_param < metadata.parameter_min || layer.fx_param > metadata.parameter_max) {
        return metadata.parameter_default;
    }
    return layer.fx_param;
}

BrandColor get_palette_color(uint8_t palette_id, uint8_t index, const BrandColor& c1, const BrandColor& c2) {
    if (palette_id == 0) {
        return {
            static_cast<uint8_t>((c1.r * (255 - index) + c2.r * index) / 255),
            static_cast<uint8_t>((c1.g * (255 - index) + c2.g * index) / 255),
            static_cast<uint8_t>((c1.b * (255 - index) + c2.b * index) / 255)
        };
    }
    if (palette_id == 1) { // Rainbow
        if (index < 85) return { static_cast<uint8_t>(index * 3), static_cast<uint8_t>(255 - index * 3), 0 };
        if (index < 170) { index -= 85; return { static_cast<uint8_t>(255 - index * 3), 0, static_cast<uint8_t>(index * 3) }; }
        index -= 170; return { 0, static_cast<uint8_t>(index * 3), static_cast<uint8_t>(255 - index * 3) };
    }
    if (palette_id == 2) { // Ocean
        return { 0, static_cast<uint8_t>(100 + (index * 155 / 255)), static_cast<uint8_t>(155 + (index * 100 / 255)) };
    }
    if (palette_id == 3) { // Lava
        if (index < 128) return { static_cast<uint8_t>(127 + index), static_cast<uint8_t>(index), 0 };
        return { 255, static_cast<uint8_t>(127 + (index - 128)), static_cast<uint8_t>(index - 128) };
    }
    if (palette_id == 4) { // Forest
        return { static_cast<uint8_t>(index / 2), static_cast<uint8_t>(128 + index / 2), static_cast<uint8_t>(index / 4) };
    }
    if (palette_id == 5) { // Sunset
        if (index < 128) return { 255, static_cast<uint8_t>(index * 2), static_cast<uint8_t>(index) };
        return { static_cast<uint8_t>(255 - (index - 128)), 255, static_cast<uint8_t>(128 + (index - 128)) };
    }
    return c1;
}

} // namespace

void EffectRenderer::render_layer(
    FixtureFrame& out_frame,
    const LayerConfig& layer,
    uint16_t num_pixels,
    uint32_t elapsed_ms
) {
    // We render the layer logic purely based on full opacity (255) locally.
    // Blending happens in the Compositor.
    const uint8_t b = 255; 
    const uint16_t speed = layer.speed_ms > 0 ? layer.speed_ms : 1;
    const uint8_t parameter = effect_parameter(layer);
    const uint32_t t_ms = elapsed_ms;

    // Reset frame to black
    for (uint16_t i = 0; i < num_pixels; ++i) {
        out_frame.pixels[i] = {0, 0, 0};
    }
    out_frame.pwm.fill(0);

    auto apply_color = [&](uint16_t px, const Rgb& color) {
        if (px < num_pixels && px < MAX_PIXELS) {
            out_frame.pixels[px] = color;
        }
    };
    auto fill_pixels = [&](const Rgb& color) {
        uint16_t pcount = std::min<uint16_t>(num_pixels, MAX_PIXELS);
        for (uint16_t px = 0; px < pcount; ++px) out_frame.pixels[px] = color;
    };
    auto set_pwm = [&](uint16_t duty) {
        out_frame.pwm[0] = duty;
    };
    auto set_solid = [&]() {
        fill_pixels(scale_rgb(layer.primary_color, b));
        set_pwm(scale_pwm(4095, b));
    };

    switch (static_cast<SignageEffect>(layer.effect)) {
        case SignageEffect::Solid:
            set_solid();
            break;

        case SignageEffect::Breathe: {
            const uint8_t phase = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t sine_val = sine8(phase);
            const uint8_t min_b = static_cast<uint8_t>((uint16_t(b) * parameter) / 100);
            const uint8_t scaled_b = static_cast<uint8_t>(min_b + (uint16_t(sine_val) * (b - min_b)) / 255);

            BrandColor blended_col = get_palette_color(layer.palette_id, 255 - sine_val, layer.primary_color, layer.secondary_color);
            fill_pixels(scale_rgb(blended_col, scaled_b));
            set_pwm(scale_pwm(4095, scaled_b));
            break;
        }

        case SignageEffect::Chase: {
            if (num_pixels == 0) break;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * num_pixels) / speed);

            for (uint8_t offset = 0; offset < parameter; ++offset) {
                const int16_t px = static_cast<int16_t>(pos) - offset;
                if (px < 0 || px >= num_pixels) continue;
                const uint8_t tail_b = static_cast<uint8_t>((uint16_t(b) * (parameter - offset)) / parameter);
                uint8_t phase = static_cast<uint8_t>((offset * 255) / parameter);
                BrandColor px_col = get_palette_color(layer.palette_id, phase, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(px_col, tail_b));
            }
            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Sweep: {
            if (num_pixels == 0) break;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * num_pixels) / speed);
            for (uint16_t px = 0; px <= pos; ++px) {
                if (px < num_pixels) {
                    apply_color(px, scale_rgb(layer.primary_color, b));
                }
            }
            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Reveal: {
            const uint32_t cycle_pos = t_ms % speed;
            const uint32_t fade_dur = std::max<uint32_t>(1, (uint32_t(speed) * parameter) / 100);
            
            uint8_t fade = 0;
            if (cycle_pos < fade_dur) {
                fade = static_cast<uint8_t>((uint32_t(b) * cycle_pos) / fade_dur);
            } else {
                fade = b;
            }
            fill_pixels(scale_rgb(layer.primary_color, fade));
            set_pwm(scale_pwm(4095, fade));
            break;
        }

        case SignageEffect::Sparkle: {
            set_solid(); 
            uint32_t rng = t_ms / 50; 
            if (rng == 0) rng = 1;

            for (uint8_t s = 0; s < parameter; ++s) {
                const uint32_t rv = xorshift(rng);
                if (num_pixels > 0) {
                    const uint16_t px = (rv >> 3) % num_pixels;
                    apply_color(px, {b, b, b});
                }
            }
            break;
        }

        case SignageEffect::Gradient: {
            if (num_pixels == 0) break;
            const uint8_t shift = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t contrast = static_cast<uint8_t>((uint16_t(parameter) * 255) / 100);

            for (uint16_t px = 0; px < num_pixels && px < MAX_PIXELS; ++px) {
                const uint8_t pos_phase = static_cast<uint8_t>((uint16_t(px) * 255 / num_pixels) + shift);
                const uint8_t intensity = static_cast<uint8_t>(255 - ((uint16_t(255 - sine8(pos_phase)) * contrast) / 255));
                const uint8_t pixel_b = scale8(b, intensity);
                BrandColor px_col = get_palette_color(layer.palette_id, pos_phase, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(px_col, pixel_b));
            }
            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Strobe: {
            const uint32_t cycle = t_ms % speed;
            const uint32_t on_time = (uint32_t(speed) * parameter) / 100;
            if (cycle < on_time) {
                set_solid();
            }
            break;
        }

        case SignageEffect::Fire: {
            if (num_pixels == 0) break;
            uint32_t rng = t_ms / (speed / 10 + 1); 
            if (rng == 0) rng = 1;
            const uint8_t flame_height = static_cast<uint8_t>((uint16_t(parameter) * 255) / 100);
            
            for (uint16_t px = 0; px < num_pixels && px < MAX_PIXELS; ++px) {
                const uint32_t rv = xorshift(rng);
                uint8_t heat = static_cast<uint8_t>(rv & 0xFF);
                const uint8_t height = static_cast<uint8_t>((uint32_t(px) * 255) / std::max<uint16_t>(1, num_pixels - 1));
                if (height > flame_height && flame_height < 255) {
                    heat = static_cast<uint8_t>((uint16_t(heat) * (255 - height)) / (255 - flame_height));
                }
                const uint8_t intensity = scale8(b, heat);
                BrandColor px_col = get_palette_color(layer.palette_id, heat, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(px_col, intensity));
            }
            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Aurora: {
            const uint32_t phase = (t_ms * 255) / speed;
            const uint8_t shift = parameter;
            
            uint8_t r_b = static_cast<uint8_t>(255 - ((255 - sine8(phase & 0xFF)) * shift / 255));
            uint8_t g_b = static_cast<uint8_t>(255 - ((255 - sine8((phase + 85) & 0xFF)) * shift / 255));
            uint8_t b_b = static_cast<uint8_t>(255 - ((255 - sine8((phase + 170) & 0xFF)) * shift / 255));
            
            BrandColor aurora_col = layer.primary_color;
            aurora_col.r = scale8(aurora_col.r, r_b);
            aurora_col.g = scale8(aurora_col.g, g_b);
            aurora_col.b = scale8(aurora_col.b, b_b);
            
            fill_pixels(scale_rgb(aurora_col, b));
            set_pwm(scale_pwm(4095, scale8(b, r_b)));
            break;
        }

        case SignageEffect::ColorWave: {
            if (num_pixels == 0) break;
            const uint8_t wave_length = parameter > 0 ? parameter : 50;
            const uint32_t shift = (t_ms * 255) / speed;
            
            for (uint16_t px = 0; px < num_pixels && px < MAX_PIXELS; ++px) {
                const uint8_t phase = static_cast<uint8_t>(((px * 255) / wave_length) + shift);
                const uint8_t val = sine8(phase);
                const uint8_t pixel_b = scale8(b, val);
                BrandColor px_col = get_palette_color(layer.palette_id, phase, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(px_col, pixel_b));
            }
            const uint8_t pwm_phase = static_cast<uint8_t>(shift & 0xFF);
            set_pwm(scale_pwm(4095, scale8(b, sine8(pwm_phase))));
            break;
        }

        case SignageEffect::StaticPulse: {
            const uint8_t phase = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t sine_val = sine8(phase);
            const uint8_t min_b = static_cast<uint8_t>((uint16_t(b) * parameter) / 100);
            
            const uint8_t scaled_b = static_cast<uint8_t>(min_b + (uint16_t(sine_val) * (b - min_b)) / 255);
            
            fill_pixels(scale_rgb(layer.primary_color, scaled_b));
            set_pwm(scale_pwm(4095, scaled_b));
            break;
        }
    }
}

} // namespace lg
