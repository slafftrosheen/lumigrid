// components/show/ShowTransport.cpp
#include "ShowTransport.hpp"

namespace lg {

ShowTransportCoordinator::ShowTransportCoordinator() {
    snapshot_.mode = ShowTransportMode::Scene;
    snapshot_.generation = 1;
    snapshot_.active_scene = 0;
    snapshot_.bpm = 120;
    snapshot_.cue_list = 0;
    snapshot_.cue_index = 0;
    snapshot_.cue_next = 0;
    snapshot_.cue_state = CueRunnerState::Stopped;
    snapshot_.cue_generation = 0;
    snapshot_.cue_elapsed_ms = 0;
    snapshot_.manual_fade_q15 = 0;
    snapshot_.cue = {};
}

ShowTransportMode ShowTransportCoordinator::mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.mode;
}

uint32_t ShowTransportCoordinator::generation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.generation;
}

ShowTransportSnapshot ShowTransportCoordinator::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void ShowTransportCoordinator::set_scene_mode(uint8_t scene_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.mode != ShowTransportMode::Scene || snapshot_.active_scene != scene_index) {
        snapshot_.mode = ShowTransportMode::Scene;
        snapshot_.active_scene = scene_index;
        snapshot_.cue.active = false;
        snapshot_.cue_state = CueRunnerState::Stopped;
        snapshot_.generation++;
    }
}

void ShowTransportCoordinator::set_timeline_mode(bool running, uint16_t bpm) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.mode != ShowTransportMode::Timeline ||
        snapshot_.bpm != bpm ||
        (snapshot_.mode == ShowTransportMode::Timeline && !running)) {
        snapshot_.mode = running ? ShowTransportMode::Timeline : ShowTransportMode::Scene;
        snapshot_.bpm = bpm;
        snapshot_.cue.active = false;
        snapshot_.cue_state = CueRunnerState::Stopped;
        snapshot_.generation++;
    }
}

void ShowTransportCoordinator::set_cue_mode(const CueTransportSnapshot& cue_snap) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.mode = ShowTransportMode::Cue;
    snapshot_.cue = cue_snap;
    snapshot_.cue.active = true;
    snapshot_.cue_list = cue_snap.list_index;
    snapshot_.cue_index = cue_snap.current_cue;
    snapshot_.cue_next = cue_snap.next_cue;
    snapshot_.cue_state = static_cast<CueRunnerState>(cue_snap.state);
    snapshot_.cue_generation = cue_snap.generation;
    snapshot_.cue_elapsed_ms = cue_snap.paused_elapsed_ms;
    snapshot_.manual_fade_q15 = cue_snap.manual_fade_q15;
    snapshot_.generation++;
}

void ShowTransportCoordinator::set_mode(ShowTransportMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.mode != mode) {
        snapshot_.mode = mode;
        if (mode == ShowTransportMode::Scene) {
            snapshot_.cue.active = false;
            snapshot_.cue_state = CueRunnerState::Stopped;
        }
        snapshot_.generation++;
    }
}

void ShowTransportCoordinator::update_cue_snapshot(const CueTransportSnapshot& cue_snap) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.mode == ShowTransportMode::Cue) {
        snapshot_.cue = cue_snap;
        snapshot_.cue.active = true;
        snapshot_.cue_list = cue_snap.list_index;
        snapshot_.cue_index = cue_snap.current_cue;
        snapshot_.cue_next = cue_snap.next_cue;
        snapshot_.cue_state = static_cast<CueRunnerState>(cue_snap.state);
        snapshot_.cue_generation = cue_snap.generation;
        snapshot_.cue_elapsed_ms = cue_snap.paused_elapsed_ms;
        snapshot_.manual_fade_q15 = cue_snap.manual_fade_q15;
    }
}

void ShowTransportCoordinator::update_snapshot(const ShowTransportSnapshot& snap) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
}

} // namespace lg
