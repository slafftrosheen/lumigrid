// components/signage/SceneTransition.cpp
#include "SceneTransition.hpp"

namespace lg {

uint16_t smoothstep_q15(uint16_t t) {
    if (t >= 32768) return 32768;
    const uint32_t x = t;
    const uint32_t x2 = (x * x) >> 15;
    const uint32_t x3 = (x2 * x) >> 15;
    return static_cast<uint16_t>(std::min<uint32_t>(32768, 3 * x2 - 2 * x3));
}

uint16_t apply_curve_q15(TransitionCurve curve, uint16_t t_q15) {
    if (t_q15 >= 32768) return 32768;
    switch (curve) {
        case TransitionCurve::Linear:
            return t_q15;
        case TransitionCurve::EaseIn: {
            const uint32_t x = t_q15;
            return static_cast<uint16_t>((x * x) >> 15);
        }
        case TransitionCurve::EaseOut: {
            const uint32_t inv = 32768 - t_q15;
            const uint32_t inv2 = (inv * inv) >> 15;
            return static_cast<uint16_t>(32768 - inv2);
        }
        case TransitionCurve::EaseInOut:
            return smoothstep_q15(t_q15);
        case TransitionCurve::Step:
            return (t_q15 < 16384) ? 0 : 32768;
    }
    return t_q15;
}

bool can_parameter_morph(const LayerConfig& from, const LayerConfig& to) {
    return from.effect == to.effect &&
           from.blend_mode == to.blend_mode &&
           from.palette_id == to.palette_id &&
           from.enabled == to.enabled;
}

LayerConfig sample_layer_morph(const LayerConfig& from, const LayerConfig& to, uint16_t t_q15) {
    LayerConfig sample = to;
    sample.primary_color.r = lerp_q15_u8(from.primary_color.r, to.primary_color.r, t_q15);
    sample.primary_color.g = lerp_q15_u8(from.primary_color.g, to.primary_color.g, t_q15);
    sample.primary_color.b = lerp_q15_u8(from.primary_color.b, to.primary_color.b, t_q15);

    sample.secondary_color.r = lerp_q15_u8(from.secondary_color.r, to.secondary_color.r, t_q15);
    sample.secondary_color.g = lerp_q15_u8(from.secondary_color.g, to.secondary_color.g, t_q15);
    sample.secondary_color.b = lerp_q15_u8(from.secondary_color.b, to.secondary_color.b, t_q15);

    sample.opacity = lerp_q15_u8(from.opacity, to.opacity, t_q15);
    sample.speed_ms = lerp_q15_u16(from.speed_ms, to.speed_ms, t_q15);
    sample.fx_param = lerp_q15_u8(from.fx_param, to.fx_param, t_q15);
    return sample;
}

void sample_scene_morph(SceneConfig& dest, const SceneConfig& from, const SceneConfig& to, uint16_t t_q15) {
    dest = to;
    dest.global_brightness = lerp_q15_u8(from.global_brightness, to.global_brightness, t_q15);

    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint8_t l = 0; l < 3; ++l) {
            const auto& fl = from.channels[ch].layers[l];
            const auto& tl = to.channels[ch].layers[l];
            if (can_parameter_morph(fl, tl)) {
                dest.channels[ch].layers[l] = sample_layer_morph(fl, tl, t_q15);
            } else {
                if (t_q15 < 16384) {
                    dest.channels[ch].layers[l] = fl;
                    uint16_t sub_t = t_q15 * 2;
                    dest.channels[ch].layers[l].opacity = lerp_q15_u8(fl.opacity, 0, sub_t);
                } else {
                    dest.channels[ch].layers[l] = tl;
                    uint16_t sub_t = (t_q15 - 16384) * 2;
                    dest.channels[ch].layers[l].opacity = lerp_q15_u8(0, tl.opacity, sub_t);
                }
            }
        }
    }
}

void blend_frames(FixtureFrame& dest, const FixtureFrame& to_frame, uint16_t t_q15, uint16_t num_pixels) {
    if (t_q15 == 0) return;
    if (t_q15 >= 32768) {
        dest = to_frame;
        return;
    }
    const uint16_t pcount = std::min<uint16_t>(num_pixels, MAX_PIXELS);
    for (uint16_t p = 0; p < pcount; ++p) {
        dest.pixels[p].r = lerp_q15_u8(dest.pixels[p].r, to_frame.pixels[p].r, t_q15);
        dest.pixels[p].g = lerp_q15_u8(dest.pixels[p].g, to_frame.pixels[p].g, t_q15);
        dest.pixels[p].b = lerp_q15_u8(dest.pixels[p].b, to_frame.pixels[p].b, t_q15);
        dest.pixels[p].w = lerp_q15_u8(dest.pixels[p].w, to_frame.pixels[p].w, t_q15);
    }
    dest.pwm = lerp_q15_u16(dest.pwm, to_frame.pwm, t_q15);
}

} // namespace lg
