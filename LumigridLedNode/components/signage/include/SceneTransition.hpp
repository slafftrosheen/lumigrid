// components/signage/include/SceneTransition.hpp
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstring>
#include "SignageTypes.hpp"
#include "RenderTypes.hpp"

namespace lg {

/// 32768 represents normalized 1.0 in Q15
constexpr uint16_t kQ15One = 32768;

uint16_t smoothstep_q15(uint16_t t);
uint16_t apply_curve_q15(TransitionCurve curve, uint16_t t_q15);

inline uint8_t lerp_q15_u8(uint8_t a, uint8_t b, uint16_t t_q15) {
    return static_cast<uint8_t>(a + ((int32_t(b) - int32_t(a)) * int32_t(t_q15)) / kQ15One);
}

inline uint16_t lerp_q15_u16(uint16_t a, uint16_t b, uint16_t t_q15) {
    return static_cast<uint16_t>(a + ((int32_t(b) - int32_t(a)) * int32_t(t_q15)) / kQ15One);
}

bool can_parameter_morph(const LayerConfig& from, const LayerConfig& to);
LayerConfig sample_layer_morph(const LayerConfig& from, const LayerConfig& to, uint16_t t_q15);
void sample_scene_morph(SceneConfig& dest, const SceneConfig& from, const SceneConfig& to, uint16_t t_q15);

void blend_frames(FixtureFrame& dest, const FixtureFrame& to_frame, uint16_t t_q15, uint16_t num_pixels);

} // namespace lg
