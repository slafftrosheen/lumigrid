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
        case FactoryEffect::BurnIn: return "burn_in";
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
            const uint16_t pixel =
                (elapsed_ms / 75) % AddressableDriver::kPixels;

            for (uint8_t lane = 0; lane < 8; ++lane) {
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
                for (uint16_t px = 0; px < AddressableDriver::kPixels; ++px) {
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

        case FactoryEffect::Manual:
            break;
    }
}

} // namespace lg
