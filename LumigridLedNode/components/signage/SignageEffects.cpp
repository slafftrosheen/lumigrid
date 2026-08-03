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
    // Quarter-wave symmetry using parabolic approximation.
    const uint8_t quarter = phase & 0x3F; // 0–63
    const uint8_t half    = phase & 0x7F; // 0–127
    uint8_t val;

    if (half < 64) {
        val = static_cast<uint8_t>((uint16_t(quarter) * quarter * 4) >> 8);
    } else {
        const uint8_t inv = 63 - quarter;
        val = static_cast<uint8_t>(255 - ((uint16_t(inv) * inv * 4) >> 8));
    }

    return (phase & 0x80) ? (255 - val) : val;
}

// Pseudo-random for sparkle (xorshift32).
uint32_t xorshift(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void apply_channels_solid(OutputController& output, const BrandPreset& preset, uint8_t brightness) {
    for (uint8_t i = 0; i < 8; ++i) {
        const auto& ch = preset.channels[i];
        if (ch.pixel_enabled) {
            output.pixels().fill(i, apply_gamma(scale_rgb(ch.pixel_color, brightness)));
        }
        if (ch.pwm_enabled) {
            output.pwm().set(i, scale_pwm(ch.pwm_duty, brightness));
        }
    }
}

} // namespace

void render_signage_effect(
    SignageEffect effect,
    OutputController& output,
    const BrandPreset& preset,
    uint32_t elapsed_ms
) {
    output.pixels().clear();
    output.pwm().set_all(0);

    const uint8_t b = preset.brightness;
    const uint16_t speed = preset.effect_speed_ms > 0 ? preset.effect_speed_ms : 1;

    switch (effect) {
        case SignageEffect::Solid:
            apply_channels_solid(output, preset, b);
            break;

        case SignageEffect::Breathe: {
            // Full cycle over effect_speed_ms. Phase 0–255.
            const uint8_t phase = static_cast<uint8_t>(
                (uint32_t(elapsed_ms % speed) * 255) / speed
            );
            const uint8_t sine_val = sine8(phase);
            // Scale brightness by sine envelope (min 10% to keep sign visible)
            const uint8_t scaled_b = static_cast<uint8_t>(
                10 + (uint16_t(sine_val) * (b - 10)) / 255
            );
            apply_channels_solid(output, preset, scaled_b);
            break;
        }

        case SignageEffect::Chase: {
            // Pixel chase along each strip, period = speed.
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
            const uint16_t pos = static_cast<uint16_t>(
                (uint32_t(elapsed_ms % speed) * pixels) / speed
            );

            for (uint8_t lane = 0; lane < 8; ++lane) {
                const auto& ch = preset.channels[lane];
                if (!ch.pixel_enabled) continue;

                // Draw a 3-pixel wide comet with tail
                for (int16_t offset = 0; offset < 3; ++offset) {
                    const int16_t px = static_cast<int16_t>(pos) - offset;
                    if (px < 0 || px >= pixels) continue;
                    const uint8_t tail_b = static_cast<uint8_t>(
                        (uint16_t(b) * (3 - offset)) / 3
                    );
                    output.pixels().set(lane, px, apply_gamma(scale_rgb(ch.pixel_color, tail_b)));
                }

                if (ch.pwm_enabled) {
                    output.pwm().set(lane, scale_pwm(ch.pwm_duty, b));
                }
            }
            break;
        }

        case SignageEffect::Sweep: {
            // Wipe across lanes sequentially: each lane activates for 1/8 of the period.
            const uint8_t active_lane = static_cast<uint8_t>(
                (uint32_t(elapsed_ms % speed) * 8) / speed
            );

            for (uint8_t lane = 0; lane < 8; ++lane) {
                const auto& ch = preset.channels[lane];
                const bool lit = (lane <= active_lane);

                if (ch.pixel_enabled && lit) {
                    output.pixels().fill(lane, apply_gamma(scale_rgb(ch.pixel_color, b)));
                }
                if (ch.pwm_enabled && lit) {
                    output.pwm().set(lane, scale_pwm(ch.pwm_duty, b));
                }
            }
            break;
        }

        case SignageEffect::Reveal: {
            // Sequential lane-by-lane reveal with fade-in per lane.
            const uint32_t per_lane_ms = speed / 8;
            const uint32_t cycle_pos = elapsed_ms % speed;

            for (uint8_t lane = 0; lane < 8; ++lane) {
                const auto& ch = preset.channels[lane];
                const uint32_t lane_start = uint32_t(lane) * per_lane_ms;

                if (cycle_pos < lane_start) continue;

                const uint32_t lane_elapsed = cycle_pos - lane_start;
                const uint8_t fade = (lane_elapsed >= per_lane_ms)
                    ? b
                    : static_cast<uint8_t>((uint32_t(b) * lane_elapsed) / per_lane_ms);

                if (ch.pixel_enabled) {
                    output.pixels().fill(lane, apply_gamma(scale_rgb(ch.pixel_color, fade)));
                }
                if (ch.pwm_enabled) {
                    output.pwm().set(lane, scale_pwm(ch.pwm_duty, fade));
                }
            }
            break;
        }

        case SignageEffect::Sparkle: {
            // Base solid color with random pixel twinkle on top.
            apply_channels_solid(output, preset, b);

            // Overlay sparkles using deterministic PRNG seeded from time.
            uint32_t rng = elapsed_ms / 50; // Change sparkle pattern every 50ms
            if (rng == 0) rng = 1;

            for (int s = 0; s < 12; ++s) { // 12 sparkles per frame
                const uint32_t rv = xorshift(rng);
                const uint8_t lane = rv % 8;
                const uint16_t px = (rv >> 3) % LG_FACTORY_PIXELS_PER_LANE;
                output.pixels().set(lane, px, {255, 255, 255}); // White sparkle
            }
            break;
        }

        case SignageEffect::Gradient: {
            // Smooth gradient across each strip's length using the channel color.
            const uint16_t pixels = LG_FACTORY_PIXELS_PER_LANE;
            const uint8_t shift = static_cast<uint8_t>(
                (uint32_t(elapsed_ms % speed) * 255) / speed
            );

            for (uint8_t lane = 0; lane < 8; ++lane) {
                const auto& ch = preset.channels[lane];
                if (!ch.pixel_enabled) continue;

                for (uint16_t px = 0; px < pixels; ++px) {
                    const uint8_t pos_phase = static_cast<uint8_t>(
                        (uint16_t(px) * 255 / pixels) + shift
                    );
                    // Intensity follows a sine wave across the strip
                    const uint8_t intensity = sine8(pos_phase);
                    const uint8_t pixel_b = scale8(b, intensity);
                    output.pixels().set(lane, px, apply_gamma(scale_rgb(ch.pixel_color, pixel_b)));
                }

                if (ch.pwm_enabled) {
                    output.pwm().set(lane, scale_pwm(ch.pwm_duty, b));
                }
            }
            break;
        }
    }
}

} // namespace lg
