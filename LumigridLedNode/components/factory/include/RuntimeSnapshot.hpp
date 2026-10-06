// components/factory/include/RuntimeSnapshot.hpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include "ChannelHardware.hpp"

namespace lg {

class FactoryApi;

/**
 * Bounded JSON writer for ESP32 with strict failure/overflow detection.
 * Never silently truncates JSON.
 */
class FixedJsonWriter {
public:
    FixedJsonWriter(char* buffer, size_t capacity)
        : buf_(buffer), cap_(capacity), len_(0), ok_(true) {
        if (cap_ > 0) {
            buf_[0] = '\0';
        } else {
            ok_ = false;
        }
    }

    bool append(const char* str);
    bool append_printf(const char* fmt, ...);

    bool ok() const { return ok_; }
    size_t size() const { return len_; }
    size_t capacity() const { return cap_; }
    const char* c_str() const { return buf_; }

private:
    char* buf_;
    size_t cap_;
    size_t len_;
    bool ok_;
};

struct NodeSnapshot {
    char id[32] = {0};
};

struct RuntimeStateSnapshot {
    char effect[32] = {0};
    uint8_t brightness = 0;
    bool test_active = false;
    bool auto_loop = false;
    char control_owner[32] = {0};
    uint32_t frame_count = 0;
};

struct FrameSnapshot {
    float loop_fps = 0.0f;
    float committed_fps = 0.0f;
    uint32_t deadline_misses = 0;
};

struct OutputSnapshot {
    bool enabled = false;
    uint32_t power_mw = 0;
    char state[16] = {0}; // "healthy", "armed", "fault", "estop"
    bool fault = false;
    uint32_t fault_count = 0;
    bool estop_active = false;

    // LG-038 timing telemetry
    uint32_t commit_us = 0;
    uint32_t max_commit_us = 0;
    uint32_t addressable_us = 0;
    uint32_t pwm_us = 0;
    uint32_t commit_failures = 0;
    FrameSnapshot frame{};
};

struct SceneSnapshot {
    uint16_t active_preset = 0;
    uint16_t requested_preset = 0;
    uint32_t requested_generation = 0;
    uint32_t applied_generation = 0;
    bool apply_pending = false;
    bool transitioning = false;
    bool fallback_active = false;
};

struct ManualSnapshot {
    bool active = false;
    uint32_t channel_mask = 0;
    uint32_t lease_remaining_ms = 0;
};

struct TransportCueSnapshot {
    bool running = false;
    char state[16] = {0}; // "stopped", "holding", "fading", "paused", "manual"
    char paused_from[16] = {0}; // source phase while paused ("stopped" = n/a)
    uint16_t list_index = 0;
    uint16_t cue_index = 0;
    uint16_t next_cue_index = 0;
    uint16_t current_scene_index = 0;
    uint16_t next_scene_index = 0;
    uint32_t elapsed_ms = 0;
    uint32_t remaining_ms = 0;
    uint16_t manual_fade = 0;
    uint16_t transition_progress = 0;
    uint32_t generation = 0;
    uint32_t active_checksum = 0;
    uint32_t saved_checksum = 0;
    bool content_stale = false;
    bool remote_driven = false;
    char mode[16] = {0}; // Cue run mode: "loop", "single_shot", ...
};

struct TransportTimelineSnapshot {
    bool running = false;
    uint16_t bpm = 120;
    uint32_t generation = 0;
    uint64_t anchor_ms = 0;
    uint32_t elapsed_ms = 0;
    uint32_t current_beat = 0;
    uint32_t active_checksum = 0;
    uint32_t saved_checksum = 0;
    bool content_stale = false;
};

struct TransportSnapshot {
    char mode[16] = {0}; // "scene", "timeline", "cue"
    uint32_t generation = 0;
    uint16_t scene_index = 0;
    TransportTimelineSnapshot timeline;
    TransportCueSnapshot cue;
};

struct SyncSnapshot {
    char configured_role[16] = {0}; // "auto", "leader", "follower", "disabled"
    char active_role[16] = {0};
    bool clock_locked = false;
    char master_address[32] = {0};
    uint32_t last_packet_age_ms = 0;
    float clock_offset_ms = 0.0f;
    uint32_t clock_jitter_us = 0;
};

struct NetworkSnapshot {
    char state[16] = {0}; // "connected", "connecting", "disconnected", "ap_only"
    int16_t rssi = 0;
    bool sta_connected = false;
    char sta_ip[32] = {0};
    bool ap_active = false;
    char apply_state[16] = {0}; // "idle", "applying", "failed"
};

struct ProtocolStatsSnapshot {
    bool enabled = true;
    uint32_t sources = 0;
    uint32_t packets_per_second = 0;
    uint32_t last_packet_age_ms = 0;
};

struct ProtocolSnapshot {
    ProtocolStatsSnapshot artnet;
    ProtocolStatsSnapshot sacn;
};

struct TimelineSnapshot {
    uint64_t clock_ms = 0;
    int32_t drift_ppm = 0;
    bool synced = false;
};

struct AutomationSnapshot {
    uint8_t active_tracks = 0;
    char transition_curve[24] = {0};
};

struct JobSnapshot {
    bool has_job = false;
    uint32_t job_id = 0;
    char type[24] = {0};
    char state[24] = {0};
    uint8_t progress = 0;
    uint32_t started_ms = 0;
    bool failed = false;
};

struct ChannelSnapshot {
    uint8_t index = 0;
    char port[8] = {0};
    bool configured = false;
    bool enabled = false;
    char owner[24] = {0};
    bool fault = false;
    bool is_addressable = false;
    uint8_t brightness = 0;
    uint16_t pixels = 0;
    uint16_t level = 0;
};

struct CertificationSnapshot {
    char state[16] = {0};
    char current_test[16] = {0};
    uint16_t passed = 0;
    uint16_t failed = 0;
    uint16_t total = 0;
};

// LG-038.3 §3: lightweight memory telemetry for the high-rate RuntimeSnapshot.
// Only 8-bit allocatable heap is a product metric (§59); internal/DMA stay in
// the detailed diagnostics endpoint. No per-allocation telemetry here.
struct MemorySnapshotTelemetry {
    uint32_t free8 = 0;
    uint32_t largest8 = 0;
    uint32_t minimum_free = 0;
    uint32_t free_dma = 0;
};

// LG-038.3 §57: allocation-failure counters (never silently dropped).
struct MemoryFailureCountersSnapshot {
    uint32_t api_allocation_failures = 0;
    uint32_t preview_allocation_failures = 0;
    uint32_t rmt_allocation_failures = 0;
    uint32_t factory_task_start_failures = 0;
    uint32_t network_task_start_failures = 0;
};

// LG-038.3 §60: active service state. Invaluable when explaining RAM use.
struct ServicesSnapshot {
    bool lighting = false;
    bool factory_worker = false;
    bool scheduler = false;
    bool api = false;
    bool mdns = false;
    bool fleet_discovery = false;
    bool artnet = false;
    bool sacn = false;
    bool node_sync = false;
    bool mqtt = false;
    bool network_apply_worker = false;
};

struct RuntimeSnapshot {
    uint8_t schema = 3;
    uint32_t stream_seq = 0;
    uint32_t generated_at_ms = 0;

    NodeSnapshot node;
    RuntimeStateSnapshot runtime;
    OutputSnapshot output;
    SceneSnapshot scene;
    ManualSnapshot manual;
    TransportSnapshot transport;
    SyncSnapshot sync;
    NetworkSnapshot network;
    ProtocolSnapshot protocols;
    TimelineSnapshot timeline;
    AutomationSnapshot automation;
    JobSnapshot job;
    CertificationSnapshot certification;
    MemorySnapshotTelemetry memory;
    MemoryFailureCountersSnapshot memory_failures;
    ServicesSnapshot services;

    ChannelSnapshot channels[kChannelCount];
};

static constexpr size_t kRuntimeSnapshotMaxBytes = 8192;

/**
 * Pure observational collector for RuntimeSnapshot.
 * GUARANTEED ZERO SIDE EFFECTS: Zero command mutations, zero sequence increments.
 */
RuntimeSnapshot collect_runtime_snapshot(FactoryApi* api, uint32_t stream_seq = 0);

/**
 * Serializes RuntimeSnapshot v3 into the supplied bounded writer.
 * Returns true if successful, false if buffer overflowed or writer errored.
 */
bool serialize_runtime_snapshot(const RuntimeSnapshot& snapshot, FixedJsonWriter& writer);

} // namespace lg
