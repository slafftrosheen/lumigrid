// components/signage/AutomationEngine.cpp
#include "AutomationEngine.hpp"
#include <algorithm>

namespace lg {

uint16_t AutomationEngine::sample_track(const AutomationTrack& track, uint32_t elapsed_ms) {
    if (track.point_count == 0) return 0;
    if (track.point_count == 1) return track.points[0].value;

    const uint32_t period = (track.period_ms == 0) ? 5000 : track.period_ms;
    uint32_t elapsed_in_period = track.loop ? (elapsed_ms % period) : std::min(elapsed_ms, period);
    const uint32_t pos_norm = (uint64_t(elapsed_in_period) * 65535ULL) / period;

    const uint8_t count = std::min<uint8_t>(track.point_count, kAutomationPointsPerTrack);

    if (pos_norm <= track.points[0].position) {
        if (!track.loop || track.points[count - 1].position <= track.points[0].position) {
            return track.points[0].value;
        }
        // Loop interpolation across wrap boundary
        const uint32_t wrap_span = (65535 - track.points[count - 1].position) + track.points[0].position;
        if (wrap_span == 0) return track.points[0].value;
        const uint32_t offset = (65535 - track.points[count - 1].position) + pos_norm;
        const uint16_t t_q15 = static_cast<uint16_t>((uint64_t(offset) * 32768ULL) / wrap_span);
        const uint16_t curved_t = apply_curve_q15(static_cast<TransitionCurve>(track.curve), t_q15);
        return lerp_q15_u16(track.points[count - 1].value, track.points[0].value, curved_t);
    }

    if (pos_norm >= track.points[count - 1].position) {
        if (!track.loop || track.points[count - 1].position >= 65535) {
            return track.points[count - 1].value;
        }
        // Loop interpolation across wrap boundary
        const uint32_t wrap_span = (65535 - track.points[count - 1].position) + track.points[0].position;
        if (wrap_span == 0) return track.points[count - 1].value;
        const uint32_t offset = pos_norm - track.points[count - 1].position;
        const uint16_t t_q15 = static_cast<uint16_t>((uint64_t(offset) * 32768ULL) / wrap_span);
        const uint16_t curved_t = apply_curve_q15(static_cast<TransitionCurve>(track.curve), t_q15);
        return lerp_q15_u16(track.points[count - 1].value, track.points[0].value, curved_t);
    }

    for (uint8_t i = 0; i < count - 1; ++i) {
        if (pos_norm >= track.points[i].position && pos_norm <= track.points[i + 1].position) {
            const uint32_t span = track.points[i + 1].position - track.points[i].position;
            if (span == 0) return track.points[i].value;
            const uint32_t offset = pos_norm - track.points[i].position;
            const uint16_t t_q15 = static_cast<uint16_t>((uint64_t(offset) * 32768ULL) / span);
            const uint16_t curved_t = apply_curve_q15(static_cast<TransitionCurve>(track.curve), t_q15);
            return lerp_q15_u16(track.points[i].value, track.points[i + 1].value, curved_t);
        }
    }

    return track.points[count - 1].value;
}

void AutomationEngine::apply(SceneConfig& sample, const SceneMotionConfig& motion, uint32_t scene_elapsed_ms) {
    const uint8_t track_count = std::min<uint8_t>(motion.track_count, kAutomationTracksPerScene);
    for (uint8_t t = 0; t < track_count; ++t) {
        const auto& track = motion.tracks[t];
        if (track.point_count == 0) continue;

        const uint16_t val = sample_track(track, scene_elapsed_ms);
        switch (track.target) {
            case AutomationTarget::GlobalBrightness:
                sample.global_brightness = static_cast<uint8_t>(std::min<uint16_t>(255, val));
                break;
            case AutomationTarget::LayerOpacity:
                if (track.channel < kChannelCount && track.layer < 3) {
                    sample.channels[track.channel].layers[track.layer].opacity = static_cast<uint8_t>(std::min<uint16_t>(255, val));
                }
                break;
            case AutomationTarget::LayerSpeed:
                if (track.channel < kChannelCount && track.layer < 3) {
                    sample.channels[track.channel].layers[track.layer].speed_ms = val;
                }
                break;
            case AutomationTarget::LayerParameter:
                if (track.channel < kChannelCount && track.layer < 3) {
                    sample.channels[track.channel].layers[track.layer].fx_param = static_cast<uint8_t>(std::min<uint16_t>(255, val));
                }
                break;
        }
    }
}

} // namespace lg
