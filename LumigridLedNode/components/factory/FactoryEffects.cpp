#include "FactoryEffects.hpp"
#include "RenderTypes.hpp"
#include <algorithm>

namespace lg {
namespace {
inline void fill_rgb(OutputController& out, uint8_t lane, Rgb c) { out.pixels().fill(lane, c.r, c.g, c.b); }
inline void set_rgb(OutputController& out, uint8_t lane, uint16_t px, Rgb c) { out.pixels().set(lane, px, c.r, c.g, c.b, 0); }

constexpr Rgb kIdentityColors[] = {
    {255, 0, 0},     // lane 0: red
    {0, 255, 0},     // lane 1: green
    {0, 0, 255},     // lane 2: blue
    {255, 255, 0},   // lane 3: yellow
    {0, 255, 255},   // lane 4: cyan
    {255, 0, 255},   // lane 5: magenta
    {255, 128, 0},   // lane 6: orange
    {255, 255, 255}  // lane 7: white
};

uint8_t scale8(uint8_t value, uint8_t brightness) {
    return static_cast<uint8_t>((uint16_t(value) * brightness) / 255);
}

Rgb scale(Rgb c, uint8_t brightness) {
    return {
        scale8(c.r, brightness),
        scale8(c.g, brightness),
        scale8(c.b, brightness)
    };
}

Rgb wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return {static_cast<uint8_t>(255 - pos * 3), 0, static_cast<uint8_t>(pos * 3)};
    if (pos < 170) {
        pos -= 85;
        return {0, static_cast<uint8_t>(pos * 3), static_cast<uint8_t>(255 - pos * 3)};
    }

    pos -= 170;
    return {static_cast<uint8_t>(pos * 3), static_cast<uint8_t>(255 - pos * 3), 0};
}

void clear_pwm(OutputController& output) {
    output.pwm().set_all(0);
}

void clear_pixels(OutputController& output) {
    output.pixels().clear();
}

}

const char* effect_name(FactoryEffect effect) {
    switch (effect) {
        case FactoryEffect::SafeBlackout: return "blackout";
        case FactoryEffect::Identity: return "identity";
        case FactoryEffect::PixelChase: return "pixel_chase";
        case FactoryEffect::Rainbow: return "rainbow";
        case FactoryEffect::WhiteWalk: return "white_walk";
        case FactoryEffect::PwmWalk: return "pwm_walk";
        case FactoryEffect::PwmFade: return "pwm_fade";
        case FactoryEffect::CombinedPulse: return "combined_pulse";
        case FactoryEffect::StressCycle: return "stress_cycle";
        case FactoryEffect::HardwareCoexistence: return "hardware_coexistence";
        case FactoryEffect::Manual: return "manual";
    }
    return "unknown";
}

void render_effect(FactoryEffect effect,
                   OutputController& output,
                   uint32_t elapsed_ms,
                   uint8_t brightness) {
    if (effect == FactoryEffect::Manual) {
        return;
    }

    clear_pixels(output);
    clear_pwm(output);

    const uint8_t phase8 = static_cast<uint8_t>((elapsed_ms / 10) & 0xFF);
    const uint16_t pwm_level =
        static_cast<uint16_t>(brightness) * 16;

    switch (effect) {
        case FactoryEffect::SafeBlackout:
            break;

        case FactoryEffect::Identity:
            for (uint8_t lane = 0; lane < 8; ++lane) {
                fill_rgb(output, lane, scale(kIdentityColors[lane], brightness));
                output.pwm().set(lane, pwm_level);
            }
            break;

        case FactoryEffect::PixelChase: {
            for (uint8_t lane = 0; lane < 8; ++lane) {
                const uint16_t lane_px = output.pixels().configured_pixels(lane);
                if (lane_px == 0) continue;
                const uint16_t pixel = (elapsed_ms / 75) % lane_px;
                set_rgb(output, 
                    lane,
                    pixel,
                    scale(kIdentityColors[lane], brightness)
                );
            }

            output.pwm().set(
                static_cast<uint8_t>((elapsed_ms / 250) % 8),
                pwm_level
            );
            break;
        }

        case FactoryEffect::Rainbow:
            for (uint8_t lane = 0; lane < 8; ++lane) {
                const uint16_t lane_px = output.pixels().configured_pixels(lane);
                for (uint16_t px = 0; px < lane_px; ++px) {
                    const uint8_t hue = phase8 + px * 8 + lane * 24;
                    set_rgb(output, lane, px, scale(wheel(hue), brightness));
                }

                output.pwm().set(lane, static_cast<uint16_t>(
                    (uint32_t(wheel(phase8 + lane * 24).r) * 4095) / 255
                ));
            }
            break;

        case FactoryEffect::WhiteWalk: {
            const uint8_t lane = (elapsed_ms / 500) % 8;
            fill_rgb(output, lane, {brightness, brightness, brightness});
            output.pwm().set(lane, pwm_level);
            break;
        }

        case FactoryEffect::PwmWalk: {
            const uint8_t channel = (elapsed_ms / 300) % 8;
            output.pwm().set(channel, pwm_level);

            for (uint8_t lane = 0; lane < 8; ++lane) {
                fill_rgb(output, 
                    lane,
                    lane == channel ? scale(kIdentityColors[lane], brightness)
                                    : Rgb{0, 0, 0}
                );
            }
            break;
        }

        case FactoryEffect::PwmFade: {
            const uint8_t triangle = phase8 < 128
                ? phase8 * 2
                : (255 - phase8) * 2;

            const uint16_t level =
                (uint32_t(triangle) * brightness * 16) / 255;

            for (uint8_t channel = 0; channel < 8; ++channel) {
                output.pwm().set(channel, level);
                fill_rgb(output, 
                    channel,
                    scale(kIdentityColors[channel], triangle)
                );
            }
            break;
        }

        case FactoryEffect::CombinedPulse: {
            const uint8_t lane = (elapsed_ms / 400) % 8;
            const Rgb c = scale(kIdentityColors[lane], brightness);

            for (uint8_t index = 0; index < 8; ++index) {
                fill_rgb(output, index, index == lane ? c : Rgb{0, 0, 0});
                output.pwm().set(index, index == lane ? pwm_level : 0);
            }
            break;
        }

        case FactoryEffect::BurnIn:
            for (uint8_t lane = 0; lane < 8; ++lane) {
                const Rgb c = scale(wheel(phase8 + lane * 32), brightness);
                fill_rgb(output, lane, c);
                output.pwm().set(lane, static_cast<uint16_t>(
                    (uint32_t(c.r + c.g + c.b) * 4095) / (255 * 3)
                ));
            }
            break;

        case FactoryEffect::HardwareCoexistence: {
            // LG-HW-001 Section 10: Hardware coexistence stress mode.
            // Continuously exercises all 8 RMT channels (synthetic 256-pixel payload)
            // with 6 rapidly changing sparse + dense patterns to detect WS2812 latch errors,
            // while simultaneously driving all 8 PCA channels with dynamic duty cycles.
            // Deterministic, zero per-frame dynamic allocation.

            // 1. PCA9685: All 8 channels active with continuously changing duty cycles
            const uint16_t pca_phase = static_cast<uint16_t>((elapsed_ms * 4) % 4096);
            for (uint8_t ch = 0; ch < 8; ++ch) {
                uint32_t duty = (pca_phase + ch * 512) % 4096;
                duty = (duty * brightness) / 255;
                output.pwm().set(ch, static_cast<uint16_t>(duty));
            }

            // 2. Addressable: All 8 lanes active, 256 pixels per lane, cycling 6 patterns
            const uint8_t pattern = (elapsed_ms / 1000) % 6;
            constexpr uint16_t kCoexPixels = 256;

            switch (pattern) {
                case 0: {
                    // Pattern 1: One moving pixel (sparse WS2812 latch detector)
                    const uint16_t moving_base = static_cast<uint16_t>((elapsed_ms / 16) % kCoexPixels);
                    for (uint8_t lane = 0; lane < 8; ++lane) {
                        const uint16_t px = (moving_base + lane * 32) % kCoexPixels;
                        set_rgb(output, lane, px, scale(kIdentityColors[lane], brightness));
                    }
                    break;
                }
                case 1: {
                    // Pattern 2: Alternating pixels (odd/even alternating)
                    const bool flip = ((elapsed_ms / 250) % 2) != 0;
                    for (uint8_t lane = 0; lane < 8; ++lane) {
                        const Rgb c = scale(kIdentityColors[lane], brightness);
                        for (uint16_t px = 0; px < kCoexPixels; ++px) {
                            if (((px % 2) != 0) ^ flip) {
                                set_rgb(output, lane, px, c);
                            }
                        }
                    }
                    break;
                }
                case 2: {
                    // Pattern 3: Checker pattern
                    const uint8_t phase_check = static_cast<uint8_t>((elapsed_ms / 250) % 2);
                    for (uint8_t lane = 0; lane < 8; ++lane) {
                        const Rgb c = scale(kIdentityColors[lane], brightness);
                        for (uint16_t px = 0; px < kCoexPixels; ++px) {
                            if ((((px / 8) + lane + phase_check) % 2) == 0) {
                                set_rgb(output, lane, px, c);
                            }
                        }
                    }
                    break;
                }
                case 3: {
                    // Pattern 4: Full white / color (dense full payload)
                    for (uint8_t lane = 0; lane < 8; ++lane) {
                        const Rgb c = scale(wheel((phase8 + lane * 32) & 0xFF), brightness);
                        fill_rgb(output, lane, c);
                    }
                    break;
                }
                case 4: {
                    // Pattern 5: All black (verify zero transmission integrity)
                    // (clear_pixels already executed at start of render_effect)
                    break;
                }
                case 5: {
                    // Pattern 6: Random deterministic PRNG pattern (reproducible seed)
                    uint32_t seed = static_cast<uint32_t>((elapsed_ms / 200) * 0x9E3779B9u + 0x1234567u);
                    for (uint8_t lane = 0; lane < 8; ++lane) {
                        for (uint16_t px = 0; px < kCoexPixels; ++px) {
                            seed = seed * 1664525u + 1013904223u;
                            const uint8_t r = scale8(static_cast<uint8_t>((seed >> 16) & 0xFF), brightness);
                            seed = seed * 1664525u + 1013904223u;
                            const uint8_t g = scale8(static_cast<uint8_t>((seed >> 16) & 0xFF), brightness);
                            seed = seed * 1664525u + 1013904223u;
                            const uint8_t b = scale8(static_cast<uint8_t>((seed >> 16) & 0xFF), brightness);
                            set_rgb(output, lane, px, {r, g, b});
                        }
                    }
                    break;
                }
            }
            break;
        }

        case FactoryEffect::Manual:
            break;
    }
}

} // namespace lg
