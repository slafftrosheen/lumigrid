// components/scheduler/TimelineEvaluator.cpp
#include "TimelineEvaluator.hpp"
#include <algorithm>

namespace lg {

uint32_t get_track_total_beats(const SequencerTrack& track) {
    if (!track.enabled || track.num_steps == 0) return 0;
    uint32_t total = 0;
    const uint8_t n = std::min<uint8_t>(track.num_steps, 16);
    for (uint8_t s = 0; s < n; ++s) {
        total += track.steps[s].length_beats;
    }
    return total;
}

TimelineStepPosition evaluate_timeline_track(
    const SequencerTrack& track,
    uint64_t total_beats
) {
    TimelineStepPosition pos{};
    if (!track.enabled || track.num_steps == 0) {
        return pos;
    }

    uint32_t track_total_length = 0;
    const uint8_t n = std::min<uint8_t>(track.num_steps, 16);
    for (uint8_t s = 0; s < n; ++s) {
        track_total_length += track.steps[s].length_beats;
    }
    if (track_total_length == 0) track_total_length = 1;

    pos.beat_in_track = static_cast<uint32_t>(total_beats % track_total_length);

    uint32_t current_beat = 0;
    for (uint8_t s = 0; s < n; ++s) {
        const uint32_t next_beat = current_beat + track.steps[s].length_beats;
        if (pos.beat_in_track >= current_beat && pos.beat_in_track < next_beat) {
            pos.step_index = s;
            pos.preset_index = track.steps[s].preset_index;
            pos.step_start_beat = current_beat;
            pos.step_end_beat = next_beat;
            break;
        }
        current_beat = next_beat;
    }
    return pos;
}

} // namespace lg
