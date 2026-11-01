// components/show/include/CueRunner.hpp
#pragma once
#include <cstdint>
#include <functional>
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

/**
 * LG-034: by-value runtime snapshot. The only sanctioned way for external
 * callers (Scheduler, HTTP, NodeSync, RuntimeSnapshot) to observe CueRunner
 * state. Lock is held once while the copy is taken; no mutable reference to
 * CueRunner internals ever escapes.
 */
struct CueRunnerSnapshot {
    bool running = false;

    CueRunnerState state = CueRunnerState::Stopped;
    CueRunnerState paused_from = CueRunnerState::Stopped;

    uint8_t list_index = 0;
    CueRunMode mode = CueRunMode::Loop;

    uint8_t current_cue = 0;
    uint8_t next_cue = 0;

    uint8_t current_scene = 0;
    uint8_t next_scene = 0;

    uint32_t elapsed_ms = 0;
    uint32_t remaining_ms = 0;

    uint16_t manual_fade_q15 = 0;
    uint16_t transition_progress_q15 = 0;

    uint32_t generation = 0;

    uint32_t active_checksum = 0;
    uint32_t saved_checksum = 0;
    bool content_stale = false;

    bool remote_driven = false;
};

/**
 * Legacy synchronized-playback payload (LG-026). Retained for the scheduler
 * transport snapshot; superseded for general observation by CueRunnerSnapshot.
 */
struct CueSyncSnapshot {
    bool active = false;
    CueRunnerState state = CueRunnerState::Stopped;
    CueRunnerState paused_from = CueRunnerState::Stopped; // LG-034 §27/§31
    uint8_t list_index = 0;
    uint8_t current_cue = 0;
    uint8_t next_cue = 0;
    uint64_t cue_anchor_ms = 0;
    uint32_t paused_elapsed_ms = 0;
    uint16_t manual_fade_q15 = 0;
    uint32_t generation = 0;

    // LG-034 §27/§28: NodeSync v6 frozen-fade + parity fields
    uint32_t list_checksum = 0;
    uint16_t transition_progress_q15 = 0;
    uint8_t source_scene = 0;
};

// Admission detail for the runtime callback; not part of the wire protocol.
enum class CueSyncError : uint8_t {
    None,
    ListMissing,
    ListMismatch,
    CueInvalid,
    StateInvalid,
    SceneMissing,
};

class CueRunner {
public:
    /// Meaningful Cue runtime transitions (LG-034 §25). Listeners use these to
    /// request a coalesced status broadcast; snapshot collection itself stays
    /// purely observational.
    enum class Event : uint8_t {
        CueChanged = 0,
        PhaseChanged = 1,
        Paused = 2,
        Resumed = 3,
        ManualCueCompleted = 4,
        Stopped = 5,
    };

    using EventCallback = void (*)(void* user_ctx, Event event);
    using EventCallbackWithCtx = std::function<void(Event event)>;

    explicit CueRunner(ConfigStore& config, LightingRuntime& signage);

    // ── Transport lifecycle ──
    // RUN always copies the latest persisted CueList into the immutable active
    // snapshot. Saving while running updates NVS but never active_list_.
    esp_err_t start(const CueList& list, uint8_t start_cue = 0, uint64_t current_ms = 0);
    esp_err_t start(uint8_t list_index, uint8_t start_cue = 0, uint64_t current_ms = 0);

    // RESTART: load latest saved list, new generation, begin at requested cue.
    // Never executes the previous list's end action; no blackout transient.
    esp_err_t restart(uint8_t list_index, uint8_t start_cue, uint64_t current_ms);

    /**
     * Stop with explicit reason (LG-034 §2):
     *  - Operator:           hold current output, no end action
     *  - Preempted:          Scene/Timeline ownership, no end action
     *  - NaturalCompletion:  Single Shot reached its end, end action applies
     *  - SyncLost:           remote transport released, no end action
     */
    esp_err_t stop(CueStopReason reason, uint64_t current_ms = 0);

    esp_err_t pause(uint64_t current_ms = 0);
    esp_err_t resume(uint64_t current_ms = 0);
    esp_err_t next(uint64_t current_ms = 0);
    esp_err_t previous(uint64_t current_ms = 0);
    esp_err_t jump(uint8_t cue_index, uint64_t current_ms = 0);
    esp_err_t set_manual_fade(uint16_t fade_q15);

    // Advances the state machine. Never blocks on its own mutex recursively.
    void update(uint64_t current_ms);

    // ── Observation ──
    CueRunnerSnapshot snapshot(uint64_t current_ms) const;
    CueSyncSnapshot sync_snapshot(uint64_t now_ms) const;

    esp_err_t apply_remote_sync(const CueSyncSnapshot& state, uint64_t current_ms,
                               CueSyncError* error = nullptr);
    void clear_remote_sync(uint64_t current_ms = 0);

    // Compatibility aliases (legacy call sites)
    esp_err_t apply_sync_snapshot(const CueSyncSnapshot& snapshot, uint64_t now_ms,
                                 CueSyncError* error = nullptr) {
        return apply_remote_sync(snapshot, now_ms, error);
    }

    /// Status-change notifications for status broadcast coalescing (§25).
    void set_event_callback(EventCallbackWithCtx callback) { event_callback_ = std::move(callback); }

private:
    // Lock-aware internal helpers. Never acquire mutex_ twice.
    void stop_locked(CueStopReason reason, uint64_t current_ms);
    void trigger_cue_locked(uint8_t cue_idx, uint64_t current_ms);
    void manual_advance_locked(uint8_t new_current, uint64_t current_ms);
    void emit_event_locked(Event event);

    // Timing math; callers must hold mutex_ (or be mid pause-capture).
    uint32_t cue_elapsed_ms_unsafe(uint64_t current_ms) const;
    uint32_t cue_remaining_ms_unsafe(uint64_t current_ms) const;

    uint8_t compute_next_index(uint8_t from_idx) const;
    uint8_t compute_prev_index(uint8_t from_idx) const;

    ConfigStore& config_;
    LightingRuntime& signage_;
    mutable std::mutex mutex_{};

    // Immutable active content snapshot (LG-034 §11): copied on RUN, never
    // replaced by saves while running.
    CueList active_list_{};
    CueRunnerState state_ = CueRunnerState::Stopped;
    CueRunnerState paused_from_ = CueRunnerState::Stopped;

    uint8_t active_list_index_ = 0;
    uint8_t current_cue_index_ = 0;
    uint8_t next_cue_index_ = 0;
    mutable int8_t step_direction_ = 1;

    uint64_t cue_start_ms_ = 0;
    uint32_t hold_duration_ms_ = 0;
    uint32_t fade_duration_ms_ = 0;
    uint32_t paused_elapsed_ms_ = 0;
    uint32_t paused_remaining_ms_ = 0;
    uint16_t manual_fade_q15_ = 0;

    uint32_t last_applied_generation_ = 0;
    uint32_t transport_generation_ = 0;
    bool remote_sync_active_ = false;
    uint32_t remote_generation_ = 0;

    EventCallbackWithCtx event_callback_{};
};

} // namespace lg
