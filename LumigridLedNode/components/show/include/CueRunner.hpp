// components/show/include/CueRunner.hpp
#pragma once
#include <cstdint>
#include <mutex>
#include "SignageTypes.hpp"
#include "ConfigStore.hpp"
#include "LightingRuntime.hpp"
#include "esp_err.h"

namespace lg {

enum class CueRunnerState : uint8_t {
    Stopped = 0,
    Holding = 1,
    Fading  = 2,
    Paused  = 3,
    Manual  = 4
};

inline const char* cue_runner_state_name(CueRunnerState s) {
    switch (s) {
        case CueRunnerState::Stopped: return "stopped";
        case CueRunnerState::Holding: return "holding";
        case CueRunnerState::Fading:  return "fading";
        case CueRunnerState::Paused:  return "paused";
        case CueRunnerState::Manual:  return "manual";
        default:                      return "unknown";
    }
}

class CueRunner {
public:
    CueRunner(ConfigStore& config, LightingRuntime& signage);

    esp_err_t start(const CueList& list, uint8_t start_cue = 0, uint32_t current_ms = 0);
    esp_err_t start(uint8_t list_index, uint8_t start_cue = 0, uint32_t current_ms = 0);
    void stop(uint32_t current_ms = 0);
    void pause();
    void resume(uint32_t current_ms = 0);
    void next(uint32_t current_ms = 0);
    void previous(uint32_t current_ms = 0);
    void jump(uint8_t cue_index, uint32_t current_ms = 0);
    void set_manual_fade(uint16_t fade_q15);

    void update(uint32_t current_ms);

    bool is_running() const {
        return state_ != CueRunnerState::Stopped;
    }
    bool is_paused() const {
        return state_ == CueRunnerState::Paused;
    }
    CueRunnerState state() const { return state_; }
    const char* state_name() const { return cue_runner_state_name(state_); }

    uint8_t active_list_index() const { return active_list_index_; }
    uint8_t current_cue_index() const { return current_cue_index_; }
    uint8_t next_cue_index() const { return next_cue_index_; }
    uint32_t cue_elapsed_ms(uint32_t current_ms) const;
    uint32_t cue_remaining_ms(uint32_t current_ms) const;
    uint16_t manual_fade_q15() const { return manual_fade_q15_; }
    const CueList& current_list() const { return list_; }

    uint32_t last_applied_generation() const { return last_applied_generation_; }

private:
    void trigger_cue(uint8_t cue_idx, uint32_t current_ms);
    uint8_t compute_next_index(uint8_t from_idx) const;
    uint8_t compute_prev_index(uint8_t from_idx) const;

    ConfigStore& config_;
    LightingRuntime& signage_;
    mutable std::mutex mutex_{};

    CueList list_{};
    CueRunnerState state_ = CueRunnerState::Stopped;
    CueRunnerState paused_state_ = CueRunnerState::Holding;

    uint8_t active_list_index_ = 0;
    uint8_t current_cue_index_ = 0;
    uint8_t next_cue_index_ = 0;
    mutable int8_t step_direction_ = 1;

    uint32_t cue_start_ms_ = 0;
    uint32_t hold_duration_ms_ = 0;
    uint32_t fade_duration_ms_ = 0;
    uint32_t paused_remaining_ms_ = 0;
    uint16_t manual_fade_q15_ = 0;

    uint32_t last_applied_generation_ = 0;
};

} // namespace lg
