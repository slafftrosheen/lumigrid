// components/signage/SignageEffects.cpp
#include "SignageEffects.hpp"
#include "GammaLut.hpp"
#include "FactoryApp.hpp"
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace lg {
namespace {

uint8_t scale8(uint8_t value, uint8_t brightness) {
    return static_cast<uint8_t>((uint16_t(value) * brightness) / 255);
}

Rgb scale_rgb(const BrandColor& c, uint8_t brightness) {
    return {
        scale8(c.r, brightness),
        scale8(c.g, brightness),
        scale8(c.b, brightness)
    };
}

uint16_t scale_pwm(uint16_t duty, uint8_t brightness) {
    return static_cast<uint16_t>((uint32_t(duty) * brightness) / 255);
}

// Sine approximation for breathe effect (0–255 input → 0–255 output).
uint8_t sine8(uint8_t phase) {
    // Maps phase 0..255 to a smooth 0..255..0 wave
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
    if (layer.fx_param < metadata.parameter_min ||
        layer.fx_param > metadata.parameter_max) {
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

static Rgb blend_rgb(const Rgb& bg, const Rgb& fg, BlendMode mode, uint8_t fg_opacity) {
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

static uint16_t blend_pwm(uint16_t bg, uint16_t fg, BlendMode mode, uint8_t fg_opacity) {
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

void render_fixture_layer(
    Rgb* pixel_buf,
    uint16_t& pwm_buf,
    const FixtureConfig& fixture,
    const LayerConfig& layer,
    uint8_t global_brightness,
    uint32_t elapsed_ms
) {
    const uint8_t b = global_brightness;
    const uint16_t speed = layer.speed_ms > 0 ? layer.speed_ms : 1;
    const uint8_t parameter = effect_parameter(layer);
    const uint32_t t_ms = elapsed_ms;

    auto apply_color_to_pixels = [&](uint16_t px, const Rgb& color) {
        if (px < fixture.num_pixels) {
            pixel_buf[px] = color;
        }
    };
    auto fill_pixels = [&](const Rgb& color) {
        for (uint16_t px = 0; px < fixture.num_pixels; ++px) apply_color_to_pixels(px, color);
    };
    auto set_pwm = [&](uint16_t duty) {
        pwm_buf = duty;
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
            const uint8_t min_b = static_cast<uint8_t>(
                (uint16_t(b) * parameter) / 100
            );
            const uint8_t scaled_b = static_cast<uint8_t>(
                min_b + (uint16_t(sine_val) * (b - min_b)) / 255
            );

            // Sample palette based on breath phase
            BrandColor blended_col = get_palette_color(layer.palette_id, 255 - sine_val, layer.primary_color, layer.secondary_color);
            
            fill_pixels(scale_rgb(blended_col, scaled_b));
            set_pwm(scale_pwm(4095, scaled_b));
            break;
        }

        case SignageEffect::Chase: {
            const uint16_t pixels = fixture.num_pixels;
            if (pixels == 0) break;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * pixels) / speed);

            for (uint8_t offset = 0; offset < parameter; ++offset) {
                const int16_t px = static_cast<int16_t>(pos) - offset;
                if (px < 0 || px >= pixels) continue;
                const uint8_t tail_b = static_cast<uint8_t>(
                    (uint16_t(b) * (parameter - offset)) / parameter
                );
                // Head is primary color, trailing tail shifts towards secondary color
                uint8_t phase = static_cast<uint8_t>((offset * 255) / parameter);
                BrandColor px_col = get_palette_color(layer.palette_id, phase, layer.primary_color, layer.secondary_color);
                apply_color_to_pixels(px, scale_rgb(px_col, tail_b));
            }

            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Sweep: {
            const uint16_t pixels = fixture.num_pixels;
            if (pixels == 0) break;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * pixels) / speed);
            for (uint16_t px = 0; px <= pos; ++px) {
                if (px < pixels) {
                    apply_color_to_pixels(px, scale_rgb(layer.primary_color, b));
                }
            }
            set_pwm(scale_pwm(4095, b));
            break;
        }

        case SignageEffect::Reveal: {
            const uint32_t cycle_pos = t_ms % speed;
            const uint32_t fade_dur = std::max(
                uint32_t(1),
                (uint32_t(speed) * parameter) / 100
            );
            
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
                if (fixture.num_pixels > 0) {
                    const uint16_t px = (rv >> 3) % fixture.num_pixels;
                    apply_color_to_pixels(px, {b, b, b});
                }
            }
            break;
        }

        case SignageEffect::Gradient: {
            const uint16_t pixels = fixture.num_pixels;
            if (pixels == 0) break;
            const uint8_t shift = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t contrast = static_cast<uint8_t>(
                (uint16_t(parameter) * 255) / 100
            );

            for (uint16_t px = 0; px < pixels; ++px) {
                const uint8_t pos_phase = static_cast<uint8_t>((uint16_t(px) * 255 / pixels) + shift);
                const uint8_t intensity = static_cast<uint8_t>(
                    255 - ((uint16_t(255 - sine8(pos_phase)) * contrast) / 255)
                );
                const uint8_t pixel_b = scale8(b, intensity);
                BrandColor px_col = get_palette_color(layer.palette_id, pos_phase, layer.primary_color, layer.secondary_color);
                apply_color_to_pixels(px, scale_rgb(px_col, pixel_b));
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
            const uint16_t pixels = fixture.num_pixels;
            if (pixels == 0) break;
            uint32_t rng = t_ms / (speed / 10 + 1); 
            if (rng == 0) rng = 1;
            const uint8_t flame_height = static_cast<uint8_t>(
                (uint16_t(parameter) * 255) / 100
            );
            
            for (uint16_t px = 0; px < pixels; ++px) {
                const uint32_t rv = xorshift(rng);
                uint8_t heat = static_cast<uint8_t>(rv & 0xFF);
                const uint8_t height = static_cast<uint8_t>(
                    (uint32_t(px) * 255) / std::max<uint16_t>(1, pixels - 1)
                );
                if (height > flame_height && flame_height < 255) {
                    heat = static_cast<uint8_t>(
                        (uint16_t(heat) * (255 - height)) / (255 - flame_height)
                    );
                }
                const uint8_t intensity = scale8(b, heat);
                BrandColor px_col = get_palette_color(layer.palette_id, heat, layer.primary_color, layer.secondary_color);
                apply_color_to_pixels(px, scale_rgb(px_col, intensity));
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
            const uint16_t pixels = fixture.num_pixels;
            if (pixels == 0) break;
            const uint8_t wave_length = parameter > 0 ? parameter : 50;
            const uint32_t shift = (t_ms * 255) / speed;
            
            for (uint16_t px = 0; px < pixels; ++px) {
                const uint8_t phase = static_cast<uint8_t>(((px * 255) / wave_length) + shift);
                const uint8_t val = sine8(phase);
                const uint8_t pixel_b = scale8(b, val);
                BrandColor px_col = get_palette_color(layer.palette_id, phase, layer.primary_color, layer.secondary_color);
                apply_color_to_pixels(px, scale_rgb(px_col, pixel_b));
            }
            const uint8_t pwm_phase = static_cast<uint8_t>(shift & 0xFF);
            set_pwm(scale_pwm(4095, scale8(b, sine8(pwm_phase))));
            break;
        }

        case SignageEffect::StaticPulse: {
            const uint8_t phase = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t sine_val = sine8(phase);
            const uint8_t min_b = static_cast<uint8_t>((uint16_t(b) * parameter) / 100);
            
            const uint8_t scaled_b = static_cast<uint8_t>(
                min_b + (uint16_t(sine_val) * (b - min_b)) / 255
            );
            
            fill_pixels(scale_rgb(layer.primary_color, scaled_b));
            set_pwm(scale_pwm(4095, scaled_b));
            break;
        }
    }
}

void render_fixture_zone(
    OutputController& output,
    const FixtureConfig& fixture,
    const PhysicalOutput* physical_outputs,
    const ZoneContent& zone,
    uint8_t global_brightness,
    uint32_t elapsed_ms
) {
    bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single || fixture.profile == FixtureProfile::PWM_RGB || fixture.profile == FixtureProfile::PWM_RGBW);
    if (fixture.num_pixels == 0 && !is_pwm) {
        return;
    }

    static Rgb final_pixels[1024];
    static Rgb layer_pixels[1024];
    uint16_t final_pwm = 0;

    const uint16_t pcount = is_pwm ? std::max<uint16_t>(1, std::min<uint16_t>(fixture.num_pixels, 1024)) : std::min<uint16_t>(fixture.num_pixels, 1024);
    for (uint16_t i = 0; i < pcount; ++i) final_pixels[i] = {0, 0, 0};

    for (uint8_t l = 0; l < 3; ++l) {
        const LayerConfig& layer = zone.layers[l];
        if (!layer.enabled || layer.opacity == 0) continue;

        for (uint16_t i = 0; i < pcount; ++i) layer_pixels[i] = {0, 0, 0};
        uint16_t layer_pwm = 0;

        render_fixture_layer(layer_pixels, layer_pwm, fixture, layer, global_brightness, elapsed_ms);

        for (uint16_t i = 0; i < pcount; ++i) {
            final_pixels[i] = blend_rgb(final_pixels[i], layer_pixels[i], layer.blend_mode, layer.opacity);
        }
        final_pwm = blend_pwm(final_pwm, layer_pwm, layer.blend_mode, layer.opacity);
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t p_idx = fixture.physical_output_index[i];
        if (p_idx >= 16) continue;
        const auto& phys = physical_outputs[p_idx];
        if (!phys.enabled) continue;

        if (phys.type == OutputType::RMT) {
            for (uint16_t px = 0; px < pcount; ++px) {
                Rgb c = final_pixels[px];
                // Apply white balance
                c.r = scale8(c.r, fixture.white_balance[0]);
                c.g = scale8(c.g, fixture.white_balance[1]);
                c.b = scale8(c.b, fixture.white_balance[2]);
                
                // Color order swap could happen here based on fixture.pixel_order,
                // but AddressableLeds handles GRB natively for now.
                
                output.pixels().set(phys.hardware_id, px, fixture.gamma_correction ? apply_gamma(c) : c);
            }
        } else if (phys.type == OutputType::PWM) {
            uint16_t duty = final_pwm;
            if (fixture.profile == FixtureProfile::PWM_RGB || fixture.profile == FixtureProfile::PWM_RGBW) {
                Rgb c = final_pixels[0];
                if (fixture.gamma_correction) c = apply_gamma(c);
                if (i == 0) duty = (static_cast<uint32_t>(c.r) * 4095) / 255;
                else if (i == 1) duty = (static_cast<uint32_t>(c.g) * 4095) / 255;
                else if (i == 2) duty = (static_cast<uint32_t>(c.b) * 4095) / 255;
                else if (i == 3 && fixture.profile == FixtureProfile::PWM_RGBW) {
                    uint8_t w = std::min({c.r, c.g, c.b});
                    duty = (static_cast<uint32_t>(w) * 4095) / 255;
                }
            }
            output.pwm().set(phys.hardware_id, duty);
        }
    }
}

bool scene_is_visible(
    const SceneConfig& scene,
    uint32_t elapsed_ms,
    uint16_t sequencer_beat_ms
) {
    return true;
}

} // namespace lg
