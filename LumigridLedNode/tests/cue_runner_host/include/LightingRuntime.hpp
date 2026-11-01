#pragma once
#include "SignageTypes.hpp"
#include "ControlSource.hpp"
#include <cstdint>

namespace lg {
// Record visual requests without emulating the scene renderer or hardware.
struct LightingRuntime {
    uint8_t scene = 0;
    uint32_t generation = 0;
    unsigned activations = 0, preparations = 0, freezes = 0, resumes = 0;
    uint16_t fade_ms = 0, progress = 0;
    uint32_t resumed_ms = 0;
    bool available = true, transitioning = false;
    uint8_t active_scene_index() const { return scene; }
    bool activate_scene_custom(uint8_t index, uint16_t fade, TransitionCurve, uint32_t* out = nullptr) {
        if (!available) return false;
        scene = index; fade_ms = fade; transitioning = fade > 0; ++activations;
        ++generation; if (out) *out = generation;
        return true;
    }
    bool activate_scene_checked(uint8_t index, uint32_t* out = nullptr) {
        return activate_scene_custom(index, 0, TransitionCurve::Linear, out);
    }
    void activate_scene_direct(const SceneConfig&, bool) { ++activations; }
    bool prepare_manual_transition(uint8_t) { ++preparations; return available; }
    void set_manual_transition_progress(uint16_t value) { progress = value; }
    bool freeze_transition() { ++freezes; return available; }
    bool resume_frozen_transition(uint32_t remaining, uint32_t) {
        ++resumes; resumed_ms = remaining; return available;
    }
    bool is_transitioning() const { return transitioning; }
    uint16_t transition_progress_q15() const { return progress; }
    DeviceConfig get_config_snapshot() const { DeviceConfig config; return config; }
    void feed_watchdog() {}
    void release_control(ControlSource) {}
};
}
