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

uint8_t mix8(uint8_t a, uint8_t b, uint8_t amount) {
    return static_cast<uint8_t>((uint16_t(a) * (255 - amount) + uint16_t(b) * amount) / 255);
}

// Integer smoothstep and an interpolated cosine table keep new effects cheap
// per pixel. The legacy sine function above stays unchanged for saved scenes.
uint8_t ease8(uint8_t value) {
    const uint32_t x = value;
    return static_cast<uint8_t>((x * x * (765 - 2 * x) + 32512) / 65025);
}

uint8_t wave16(uint16_t phase) {
    static constexpr uint8_t kWave[33] = {
        0, 2, 10, 21, 37, 57, 79, 103, 128, 152, 176, 198, 218, 234, 245, 253,
        255, 253, 245, 234, 218, 198, 176, 152, 128, 103, 79, 57, 37, 21, 10, 2, 0
    };
    const uint8_t index = phase >> 11;
    const uint16_t fraction = phase & 2047;
    return static_cast<uint8_t>((uint32_t(kWave[index]) * (2048 - fraction)
                               + uint32_t(kWave[index + 1]) * fraction) / 2048);
}

uint16_t cycle_phase(uint32_t time_ms, uint16_t period_ms) {
    return static_cast<uint16_t>((uint32_t(time_ms % period_ms) * 65536U) / period_ms);
}

uint16_t triangle16(uint16_t phase) {
    const uint32_t distance = phase <= 32768 ? phase : 65536U - phase;
    return static_cast<uint16_t>((distance * 65535U) / 32768U);
}

uint32_t hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

uint8_t smooth_noise(uint32_t time_ms, uint32_t interval_ms, uint32_t seed) {
    const uint32_t tick = time_ms / interval_ms;
    const uint8_t fraction = ease8(static_cast<uint8_t>((time_ms % interval_ms) * 255U / interval_ms));
    return mix8(static_cast<uint8_t>(hash32(tick ^ seed) >> 24),
                static_cast<uint8_t>(hash32((tick + 1) ^ seed) >> 24), fraction);
}

uint8_t pulse16(uint16_t phase, uint16_t start, uint16_t peak, uint16_t end) {
    if (phase <= start || phase >= end) return 0;
    if (phase < peak) {
        return ease8(static_cast<uint8_t>(uint32_t(phase - start) * 255U / (peak - start)));
    }
    return 255 - ease8(static_cast<uint8_t>(uint32_t(phase - peak) * 255U / (end - peak)));
}

uint8_t shuffled_threshold(uint8_t index) {
    // Reversible byte operations give each of the 256 pixels a unique rank.
    index ^= index >> 4;
    index = static_cast<uint8_t>(index * 45U);
    index ^= index >> 4;
    index = static_cast<uint8_t>(index * 45U);
    return index ^ (index >> 4);
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
    if (palette_id < kSignagePaletteCount) {
        const auto& colors = signage_palette_metadata(palette_id).colors;
        const uint16_t position = uint16_t(index) * 3;
        const uint8_t segment = std::min<uint16_t>(position / 255, 2);
        const uint8_t fraction = static_cast<uint8_t>(position - uint16_t(segment) * 255);
        return {
            mix8(colors[segment].r, colors[segment + 1].r, fraction),
            mix8(colors[segment].g, colors[segment + 1].g, fraction),
            mix8(colors[segment].b, colors[segment + 1].b, fraction)
        };
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
    // Clamp before clearing: callers can supply persisted or untrusted counts.
    num_pixels = std::min<uint16_t>(num_pixels, MAX_PIXELS);
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
    out_frame.pwm = 0;

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
        out_frame.pwm = duty;
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
            const uint32_t phase = static_cast<uint32_t>((uint64_t(t_ms) * 255) / speed);
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
            const uint32_t shift = static_cast<uint32_t>((uint64_t(t_ms) * 255) / speed);
            
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

        case SignageEffect::Rainbow: {
            if (num_pixels == 0) break;
            const uint8_t shift = static_cast<uint8_t>((uint64_t(t_ms) * 256) / speed);
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint8_t hue = static_cast<uint8_t>((uint32_t(px) * parameter * 256U) / num_pixels + shift);
                apply_color(px, scale_rgb(get_palette_color(1, hue, layer.primary_color, layer.secondary_color), 255));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::ColorFade: {
            const uint16_t phase = cycle_phase(t_ms, speed);
            const uint16_t hold = uint32_t(parameter) * 65536U / 100;
            const uint16_t half_phase = phase & 32767;
            uint8_t position = half_phase <= hold ? 0
                : static_cast<uint8_t>(uint32_t(half_phase - hold) * 255U / (32768U - hold));
            position = ease8(position);
            if (phase >= 32768) position = 255 - position;
            fill_pixels(scale_rgb(get_palette_color(layer.palette_id, position, layer.primary_color, layer.secondary_color), 255));
            set_pwm(4095);
            break;
        }

        case SignageEffect::Scanner: {
            if (num_pixels == 0) break;
            const uint32_t center = uint32_t(triangle16(cycle_phase(t_ms, speed))) * (num_pixels - 1) * 256U / 65535U;
            const uint32_t radius = uint32_t(parameter) * 128U + 128U;
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t position = uint32_t(px) * 256U;
                const uint32_t distance = position > center ? position - center : center - position;
                if (distance >= radius) continue;
                const uint8_t edge = static_cast<uint8_t>(distance * 255U / radius);
                const auto color = get_palette_color(layer.palette_id, edge, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, 255 - ease8(edge)));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Theater: {
            if (num_pixels == 0) break;
            const uint16_t phase = cycle_phase(t_ms, speed);
            const uint32_t spacing = uint32_t(parameter) * 256U;
            const uint32_t offset = uint32_t(phase) * spacing / 65536U;
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t position = (uint32_t(px) * 256U + spacing - offset) % spacing;
                const uint32_t distance = std::min(position, spacing - position);
                if (distance >= 256) continue;
                const uint8_t intensity = static_cast<uint8_t>(255 - distance * 255U / 256U);
                const uint8_t color_phase = static_cast<uint8_t>(uint32_t(px) * 255U / num_pixels + (phase >> 8));
                const auto color = get_palette_color(layer.palette_id, color_phase, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, intensity));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Twinkle: {
            if (num_pixels == 0) break;
            const uint64_t timeline = uint64_t(t_ms) * 65536U / speed;
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t seed = hash32(uint32_t(px) + 0x51ed270bU);
                const uint64_t local = timeline + (seed & 65535U);
                const uint32_t event = hash32(static_cast<uint32_t>(local >> 16) ^ seed);
                if (event % 100 >= parameter) continue;
                const uint8_t intensity = wave16(static_cast<uint16_t>(local));
                const auto color = get_palette_color(layer.palette_id, event >> 24, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, intensity));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Candle: {
            const uint32_t interval = std::max<uint32_t>(1, speed / 12);
            const uint8_t drift = smooth_noise(t_ms, interval * 3, 0xa341316cU);
            const uint8_t flutter = smooth_noise(t_ms, interval, 0xc8013ea4U);
            const uint8_t noise = static_cast<uint8_t>((uint16_t(drift) * 3 + flutter) / 4);
            const uint8_t depth = uint16_t(parameter) * 255 / 100;
            const uint8_t intensity = 255 - scale8(noise, depth);
            fill_pixels(scale_rgb(layer.primary_color, intensity));
            set_pwm(scale_pwm(4095, intensity));
            break;
        }

        case SignageEffect::Heartbeat: {
            const uint16_t phase = cycle_phase(t_ms, speed);
            const uint8_t first = pulse16(phase, 0, 3277, 14418);
            const uint8_t second = scale8(pulse16(phase, 15729, 19661, 32768), 180);
            const uint8_t floor = uint16_t(parameter) * 255 / 100;
            const uint8_t intensity = floor + scale8(std::max(first, second), 255 - floor);
            fill_pixels(scale_rgb(layer.primary_color, intensity));
            set_pwm(scale_pwm(4095, intensity));
            break;
        }

        case SignageEffect::Ripple: {
            if (num_pixels == 0) break;
            const uint16_t phase = cycle_phase(t_ms, speed);
            const uint32_t center = uint32_t(num_pixels - 1) * 128U;
            const uint32_t width = uint32_t(parameter) * 128U + 128U;
            const uint32_t radius = uint32_t(phase) * (center + width) / 65536U;
            const auto color = get_palette_color(layer.palette_id, phase >> 8, layer.primary_color, layer.secondary_color);
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t position = uint32_t(px) * 256U;
                const uint32_t from_center = position > center ? position - center : center - position;
                const uint32_t distance = from_center > radius ? from_center - radius : radius - from_center;
                if (distance >= width) continue;
                const uint8_t edge = static_cast<uint8_t>(distance * 255U / width);
                apply_color(px, scale_rgb(color, 255 - ease8(edge)));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Dissolve: {
            if (num_pixels == 0) break;
            const uint16_t softness = std::max<uint16_t>(1, uint16_t(parameter) * 255 / 100);
            const uint16_t progress = uint32_t(triangle16(cycle_phase(t_ms, speed))) * (255U + softness) / 65535U;
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint8_t threshold = shuffled_threshold(static_cast<uint8_t>(px));
                const uint8_t amount = progress <= threshold ? 0
                    : static_cast<uint8_t>(std::min<uint32_t>(255, uint32_t(progress - threshold) * 255U / softness));
                const auto color = get_palette_color(layer.palette_id, ease8(amount), layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, 255));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Plasma: {
            if (num_pixels == 0) break;
            const uint32_t motion = static_cast<uint32_t>(uint64_t(t_ms) * 65536U / speed);
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t position = uint32_t(px) * 65536U / num_pixels;
                const uint8_t a = wave16(static_cast<uint16_t>(position * parameter + motion));
                const uint8_t b_wave = wave16(static_cast<uint16_t>(position * (parameter + 1) - motion * 2));
                const uint8_t c = wave16(static_cast<uint16_t>(position * (parameter + 2) / 2 + motion / 2));
                const uint8_t interference = static_cast<uint8_t>((uint16_t(a) + b_wave + c) / 3);
                const uint8_t intensity = 96 + scale8(interference, 159);
                const auto color = get_palette_color(layer.palette_id, interference, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, intensity));
            }
            set_pwm(4095);
            break;
        }

        case SignageEffect::Lightning: {
            const uint16_t phase = cycle_phase(t_ms, speed);
            if (phase >= 32768) break;
            const uint32_t burst = uint32_t(phase) * parameter;
            const uint8_t flash = burst / 32768U;
            const uint16_t local = static_cast<uint16_t>((burst % 32768U) * 2U);
            const uint8_t strength = flash == 0 ? 255
                : 180 + scale8(static_cast<uint8_t>(hash32(t_ms / speed ^ (uint32_t(flash) * 0x9e3779b9U)) >> 24), 75);
            const uint8_t intensity = scale8(pulse16(local, 0, 2048, 24576), strength);
            fill_pixels(scale_rgb(layer.primary_color, intensity));
            set_pwm(scale_pwm(4095, intensity));
            break;
        }

        case SignageEffect::Confetti: {
            if (num_pixels == 0) break;
            const uint64_t timeline = uint64_t(t_ms) * 65536U / speed;
            for (uint16_t px = 0; px < num_pixels; ++px) {
                const uint32_t seed = hash32(uint32_t(px) + 0x68bc21ebU);
                const uint64_t local = timeline + (seed & 65535U);
                const uint32_t event = hash32(static_cast<uint32_t>(local >> 16) ^ seed);
                if (event % 100 >= parameter) continue;
                const uint8_t decay = 255 - (static_cast<uint16_t>(local) >> 8);
                const uint8_t intensity = scale8(decay, decay);
                const auto color = get_palette_color(layer.palette_id, event >> 24, layer.primary_color, layer.secondary_color);
                apply_color(px, scale_rgb(color, intensity));
            }
            set_pwm(4095);
            break;
        }
    }
}

} // namespace lg
