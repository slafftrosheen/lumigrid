// components/show/include/ShowTransport.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include "esp_err.h"
#include "CueRunner.hpp"

namespace lg {

enum class ShowTransportMode : uint8_t {
    Standalone = 0,
    Scene      = 1,
    Timeline   = 2,
    Cue        = 3,
};

inline const char* show_transport_mode_str(ShowTransportMode mode) {
    switch (mode) {
        case ShowTransportMode::Standalone: return "standalone";
        case ShowTransportMode::Scene:      return "scene";
        case ShowTransportMode::Timeline:   return "timeline";
        case ShowTransportMode::Cue:        return "cue";
        default:                            return "standalone";
    }
}

inline bool parse_show_transport_mode(const char* str, ShowTransportMode& out) {
    if (!str) return false;
    if (strcmp(str, "standalone") == 0) { out = ShowTransportMode::Standalone; return true; }
    if (strcmp(str, "scene") == 0)      { out = ShowTransportMode::Scene;      return true; }
    if (strcmp(str, "timeline") == 0)   { out = ShowTransportMode::Timeline;   return true; }
    if (strcmp(str, "cue") == 0)        { out = ShowTransportMode::Cue;        return true; }
    return false;
}

struct CueTransportSnapshot {
    bool active = false;

    uint8_t list_index = 0;
    uint8_t state = 0;

    uint8_t current_cue = 0;
    uint8_t next_cue = 0;

    uint16_t manual_fade_q15 = 0;

    uint64_t cue_anchor_ms = 0;
    uint32_t paused_elapsed_ms = 0;

    uint32_t generation = 0;
};

struct ShowTransportSnapshot {
    ShowTransportMode mode = ShowTransportMode::Standalone;

    uint8_t active_scene = 0;

    uint16_t bpm = 120;

    uint8_t cue_list = 0;
    uint8_t cue_index = 0;
    uint8_t cue_next = 0;
    CueRunnerState cue_state = CueRunnerState::Stopped;

    uint32_t cue_generation = 0;
    uint32_t cue_elapsed_ms = 0;

    uint16_t manual_fade_q15 = 0;

    // Additional fields and helpers for backward compatibility
    uint32_t generation = 0;
    CueTransportSnapshot cue{};

    uint8_t scene_index() const { return active_scene; }
    uint16_t timeline_bpm() const { return bpm; }
    bool timeline_running() const { return mode == ShowTransportMode::Timeline; }
};

class ShowTransportCoordinator {
public:
    ShowTransportCoordinator();

    ShowTransportMode mode() const;
    uint32_t generation() const;
    ShowTransportSnapshot snapshot() const;

    void set_scene_mode(uint8_t scene_index);
    void set_timeline_mode(bool running, uint16_t bpm);
    void set_cue_mode(const CueTransportSnapshot& cue_snap);
    void update_cue_snapshot(const CueTransportSnapshot& cue_snap);
    void update_snapshot(const ShowTransportSnapshot& snap);

    // Convenience aliases
    void set_scene(uint8_t scene_index) { set_scene_mode(scene_index); }
    void set_timeline(bool running, uint16_t bpm) { set_timeline_mode(running, bpm); }
    void set_cue(const CueTransportSnapshot& cue_snap) { set_cue_mode(cue_snap); }
    void set_mode(ShowTransportMode mode);

private:
    mutable std::mutex mutex_{};
    ShowTransportSnapshot snapshot_{};
};

} // namespace lg
