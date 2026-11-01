// components/scheduler/include/TimelineEvaluator.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"

namespace lg {

struct TimelineStepPosition {
    uint8_t step_index = 0xFF;
    uint8_t preset_index = 0xFF;
    uint32_t beat_in_track = 0;
    uint32_t step_start_beat = 0;
    uint32_t step_end_beat = 0;
};

/// Pure evaluator for a single timeline track given global elapsed beats.
/// Returns step_index, beat_in_track, step_start_beat, step_end_beat.
TimelineStepPosition evaluate_timeline_track(
    const SequencerTrack& track,
    uint64_t total_beats
);

uint32_t get_track_total_beats(const SequencerTrack& track);

/// Exact whole beats without truncating the beat period or overflowing elapsed*bpm.
/// Zero BPM retains the legacy default of 120 BPM.
uint64_t timeline_elapsed_beats(uint64_t elapsed_ms, uint16_t bpm);

} // namespace lg
