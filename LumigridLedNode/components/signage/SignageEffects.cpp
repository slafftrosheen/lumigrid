// components/signage/SignageEffects.cpp
#include "SignageEffects.hpp"
#include "GammaLut.hpp"
#include "FactoryApp.hpp"
#include <cstdlib>
#include <algorithm>

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
    const uint8_t quarter = phase & 0x3F;
    const uint8_t half    = phase & 0x7F;
    uint8_t val;

    if (half < 64) {
        val = static_cast<uint8_t>((uint16_t(quarter) * quarter * 4) >> 8);
    } else {
        const uint8_t inv = 63 - quarter;
        val = static_cast<uint8_t>(255 - ((uint16_t(inv) * inv * 4) >> 8));
    }

    return (phase & 0x80) ? (255 - val) : val;
}

uint32_t xorshift(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

uint8_t effect_parameter(const ChannelConfig& channel) {
    const EffectMetadata& metadata = signage_effect_metadata(channel.effect);
    if (!metadata.has_parameter) return 0;
    if (channel.fx_param < metadata.parameter_min ||
        channel.fx_param > metadata.parameter_max) {
        return metadata.parameter_default;
    }
    return channel.fx_param;
}

} // namespace

void render_lane_effect(
    uint8_t lane,
    OutputController& output,
    const ChannelConfig& channel,
    uint8_t global_brightness,
    uint32_t elapsed_ms
) {
    const uint8_t b = global_brightness;
    const uint16_t speed = channel.effect_speed_ms > 0 ? channel.effect_speed_ms : 1;
    const uint8_t parameter = effect_parameter(channel);
    // apply phase offset (0-255 mapped to full speed period)
    const uint32_t phase_time_offset = (uint32_t(channel.phase_offset) * speed) / 255;
    const uint32_t t_ms = elapsed_ms + phase_time_offset;

    auto set_solid = [&]() {
        if (channel.pixel_enabled) {
            output.pixels().fill(lane, apply_gamma(scale_rgb(channel.pixel_color, b)));
        }
        if (channel.pwm_enabled) {
            output.pwm().set(lane, scale_pwm(channel.pwm_duty, b));
        }
    };

    switch (channel.effect) {
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
            
            if (channel.pixel_enabled) {
                output.pixels().fill(lane, apply_gamma(scale_rgb(channel.pixel_color, scaled_b)));
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, scaled_b));
            }
            break;
        }

        case SignageEffect::Chase: {
            if (!channel.pixel_enabled) break;
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * pixels) / speed);

            for (uint8_t offset = 0; offset < parameter; ++offset) {
                const int16_t px = static_cast<int16_t>(pos) - offset;
                if (px < 0 || px >= pixels) continue;
                const uint8_t tail_b = static_cast<uint8_t>(
                    (uint16_t(b) * (parameter - offset)) / parameter
                );
                output.pixels().set(lane, px, apply_gamma(scale_rgb(channel.pixel_color, tail_b)));
            }

            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, b));
            }
            break;
        }

        case SignageEffect::Sweep: {
            // Wipe across a single lane
            if (!channel.pixel_enabled) break;
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
            const uint16_t pos = static_cast<uint16_t>((uint32_t(t_ms % speed) * pixels) / speed);
            for (uint16_t px = 0; px <= pos; ++px) {
                if (px < pixels) {
                    output.pixels().set(lane, px, apply_gamma(scale_rgb(channel.pixel_color, b)));
                }
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, b));
            }
            break;
        }

        case SignageEffect::Reveal: {
            // Reveal this lane based on phase time offset
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

            if (channel.pixel_enabled) {
                output.pixels().fill(lane, apply_gamma(scale_rgb(channel.pixel_color, fade)));
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, fade));
            }
            break;
        }

        case SignageEffect::Sparkle: {
            set_solid(); // Base color

            uint32_t rng = t_ms / 50; 
            if (rng == 0) rng = 1;

            if (channel.pixel_enabled) {
                for (uint8_t s = 0; s < parameter; ++s) {
                    const uint32_t rv = xorshift(rng);
                    const uint16_t px = (rv >> 3) % LG_FACTORY_PIXELS_PER_LANE;
                    output.pixels().set(lane, px, {255, 255, 255});
                }
            }
            break;
        }

        case SignageEffect::Gradient: {
            if (!channel.pixel_enabled) break;
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
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
                output.pixels().set(lane, px, apply_gamma(scale_rgb(channel.pixel_color, pixel_b)));
            }

            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, b));
            }
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
            if (!channel.pixel_enabled) break;
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
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
                output.pixels().set(lane, px, apply_gamma(scale_rgb(channel.pixel_color, intensity)));
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, b));
            }
            break;
        }

        case SignageEffect::Aurora: {
            if (!channel.pixel_enabled && !channel.pwm_enabled) break;
            const uint32_t phase = (t_ms * 255) / speed;
            const uint8_t shift = parameter;
            
            uint8_t r_b = static_cast<uint8_t>(255 - ((255 - sine8(phase & 0xFF)) * shift / 255));
            uint8_t g_b = static_cast<uint8_t>(255 - ((255 - sine8((phase + 85) & 0xFF)) * shift / 255));
            uint8_t b_b = static_cast<uint8_t>(255 - ((255 - sine8((phase + 170) & 0xFF)) * shift / 255));
            
            BrandColor aurora_col = channel.pixel_color;
            aurora_col.r = scale8(aurora_col.r, r_b);
            aurora_col.g = scale8(aurora_col.g, g_b);
            aurora_col.b = scale8(aurora_col.b, b_b);
            
            if (channel.pixel_enabled) {
                output.pixels().fill(lane, apply_gamma(scale_rgb(aurora_col, b)));
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, scale8(b, r_b)));
            }
            break;
        }

        case SignageEffect::ColorWave: {
            if (!channel.pixel_enabled) break;
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
            const uint8_t wave_length = parameter > 0 ? parameter : 50;
            const uint32_t shift = (t_ms * 255) / speed;
            
            for (uint16_t px = 0; px < pixels; ++px) {
                const uint8_t phase = static_cast<uint8_t>(((px * 255) / wave_length) + shift);
                const uint8_t val = sine8(phase);
                const uint8_t pixel_b = scale8(b, val);
                output.pixels().set(lane, px, apply_gamma(scale_rgb(channel.pixel_color, pixel_b)));
            }
            if (channel.pwm_enabled) {
                const uint8_t pwm_phase = static_cast<uint8_t>(shift & 0xFF);
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, scale8(b, sine8(pwm_phase))));
            }
            break;
        }

        case SignageEffect::StaticPulse: {
            const uint8_t phase = static_cast<uint8_t>((uint32_t(t_ms % speed) * 255) / speed);
            const uint8_t sine_val = sine8(phase);
            const uint8_t min_b = static_cast<uint8_t>((uint16_t(b) * parameter) / 100);
            
            const uint8_t scaled_b = static_cast<uint8_t>(
                min_b + (uint16_t(sine_val) * (b - min_b)) / 255
            );
            
            if (channel.pixel_enabled) {
                output.pixels().fill(lane, apply_gamma(scale_rgb(channel.pixel_color, scaled_b)));
            }
            if (channel.pwm_enabled) {
                output.pwm().set(lane, scale_pwm(channel.pwm_duty, scaled_b));
            }
            break;
        }
    }
}

bool preset_is_visible(
    const BrandPreset& preset,
    uint32_t elapsed_ms,
    uint16_t sequencer_beat_ms
) {
    if (preset.overlay_effect == SignageModifier::None) return true;

    if (preset.overlay_effect == SignageModifier::Strobe) {
        const uint32_t speed = preset.overlay_speed_ms == 0
            ? std::max<uint16_t>(1, sequencer_beat_ms)
            : preset.overlay_speed_ms;
        const uint8_t duty_cycle = std::max<uint8_t>(
            5,
            std::min<uint8_t>(
                preset.overlay_param == 0 ? 50 : preset.overlay_param,
                95
            )
        );
        return (elapsed_ms % speed) < (speed * duty_cycle) / 100;
    }

    return true;
}

} // namespace lg
