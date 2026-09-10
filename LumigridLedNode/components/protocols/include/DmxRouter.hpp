// components/protocols/include/DmxRouter.hpp
#pragma once
#include <cstdint>
#include <array>
#include <mutex>
#include <atomic>
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "SignageTypes.hpp"
#include "RenderTypes.hpp"

namespace lg {

class LightingRuntime;

enum class DmxSourceProtocol : uint8_t {
    ArtNet = 1,
    Sacn = 2
};

/// Source identity for DMX streams (Batch 6 Item 15):
/// - Art-Net: uses IPv4 identity (first 4 bytes). Two controllers sharing the same
///   source IP are treated as one logical Art-Net source stream.
/// - sACN: uses the full 16-byte CID (Component Identifier per ANSI E1.31).
struct DmxSourceId {
    DmxSourceProtocol protocol{DmxSourceProtocol::ArtNet};
    std::array<uint8_t, 16> identity{};

    bool operator==(const DmxSourceId& other) const {
        return protocol == other.protocol && identity == other.identity;
    }
    bool operator!=(const DmxSourceId& other) const {
        return !(*this == other);
    }
};

using DmxProtocol = DmxSourceProtocol;
using SourceId = DmxSourceId;

/// Shared sequence validation helper per Batch 3 Item 7:
/// Returns true if incoming sequence is strictly newer than previous.
/// - duplicate -> reject (false)
/// - old packet -> reject (false)
/// - forward packet -> accept (true)
/// - wrap 255 -> 0 -> accept (true)
inline bool is_sequence_newer(uint8_t incoming, uint8_t previous) {
    int16_t diff = static_cast<int16_t>(incoming) - static_cast<int16_t>(previous);
    return (diff > 0 && diff <= 127) || (diff < -127);
}

/// Helper determining whether sequence validation is required per protocol.
/// - Art-Net 4: sequence == 0 explicitly disables sequence checking (OpDmx specification).
/// - ANSI E1.31 (sACN): sequence 0 is a standard sequenced value; sequence check is always required.
inline bool sequence_check_required(DmxSourceProtocol protocol, uint8_t sequence) {
    if (protocol == DmxSourceProtocol::ArtNet && sequence == 0) {
        return false;
    }
    return true;
}

/// Dedicated DMX routing engine for Art-Net and sACN protocols.
///
/// Features:
///   1. Multi-source HTP & LTP tracking with stream-based source identity.
///   2. ANSI E1.31 sACN sequence filtering and priority resolution.
///   3. Unified stream timeout handling.
///   4. Thread-safe LiveDmxFrame assembly with mutex synchronization.
///   5. Zone wash (8 zones) & direct fixture mapping (WS2812, SK6812, PWM).
class DmxRouter {
public:
    explicit DmxRouter(LightingRuntime& lighting);
    DmxRouter(LightingRuntime& lighting, const DeviceConfig& config);

    /// Update active patch configuration in memory (called on startup & config reloads)
    void update_config(const DeviceConfig& config);

    /// Ingest and process an Art-Net DMX frame
    void handle_artnet_dmx(const ArtnetDmxPacket& packet);

    /// Ingest and process an sACN DMX frame
    void handle_sacn_dmx(const SacnDmxPacket& packet);

    /// Process a generic DMX frame with source identification, sequence, and priority
    void process_frame(uint16_t universe, const uint8_t* data, uint16_t length,
                       uint8_t priority, DmxSourceId source_id, uint8_t sequence = 0);

    /// Extract list of active universes patched across zone wash and fixtures
    size_t get_active_universes(uint16_t* out_universes, size_t max_count) const;

    /// Thread-safe stream timeout helper (acquires frame_mutex_)
    uint32_t stream_timeout_ms() const;

    struct DmxStats {
        uint32_t active_sources = 0;
        uint32_t active_universes = 0;
        uint32_t sources_dropped = 0;
        uint32_t packets_accepted = 0;
        uint32_t packets_rejected_sequence = 0;
        uint32_t packets_ignored_unpatched = 0;
        uint32_t submission_failures = 0;
    };

    /// Thread-safe synchronized stats snapshot (acquires frame_mutex_ once)
    DmxStats get_stats() const;

    /// Observable diagnostic counters
    uint32_t active_source_count() const { return get_stats().active_sources; }
    uint32_t dmx_universes_active() const { return get_stats().active_universes; }
    uint32_t dropped_sources_count() const { return dropped_sources_count_.load(std::memory_order_relaxed); }
    uint32_t dmx_sources_dropped() const { return dropped_sources_count_.load(std::memory_order_relaxed); }
    uint32_t dmx_packets_accepted() const { return packets_accepted_.load(std::memory_order_relaxed); }
    uint32_t dmx_packets_rejected_sequence() const { return packets_rejected_sequence_.load(std::memory_order_relaxed); }
    uint32_t dmx_packets_ignored_unpatched() const { return packets_ignored_unpatched_.load(std::memory_order_relaxed); }
    uint32_t submission_failures() const { return submission_failures_count_.load(std::memory_order_relaxed); }

    /// Static protocol sequence validation helper
    static bool sequence_check_required(DmxSourceProtocol protocol, uint8_t sequence) {
        return lg::sequence_check_required(protocol, sequence);
    }

    /// Periodic maintenance tick to prune timed-out sources and clear live frame (Batch 8 Item 4)
    void tick(uint32_t now_ms);

    /// Authoritative universe state clear (Batch 8 Item 5)
    void clear_universe_state(uint16_t universe);

    /// Dedicated universe patch clearing when universe sources expire
    void clear_patch_for_universe(uint16_t universe) { clear_universe_state(universe); }

private:
    enum class ClearResult {
        None,
        SubmitFrame,
        ClearLive
    };
    ClearResult clear_universe_state_locked(uint16_t universe, uint32_t current_ms);
    uint32_t stream_timeout_ms_locked() const {
        return protocol_cfg_.stream_timeout_ms > 0 ? protocol_cfg_.stream_timeout_ms : 2000;
    }

    LightingRuntime& lighting_;
    mutable std::mutex frame_mutex_;
    ProtocolConfig protocol_cfg_{};
    std::array<FixtureConfig, 16> fixtures_{};

    std::atomic<uint32_t> dropped_sources_count_{0};
    std::atomic<uint32_t> packets_accepted_{0};
    std::atomic<uint32_t> packets_rejected_sequence_{0};
    std::atomic<uint32_t> packets_ignored_unpatched_{0};
    std::atomic<uint32_t> submission_failures_count_{0};

    // Tracking state per active stream contributor within a universe
    struct SourceStream {
        DmxSourceId source_id{};
        uint8_t  priority = 100;
        uint32_t last_packet_ms = 0;
        uint16_t length = 0;
        uint8_t  last_sequence = 0;
        bool     sequence_valid = false;
        std::array<uint8_t, 512> data{};
        bool     active = false;
    };

    // Tracking state per universe (up to 4 universes concurrently)
    struct UniverseState {
        uint16_t universe = 0xFFFF;
        uint32_t last_packet_ms = 0;
        bool active = false;
        std::array<SourceStream, 4> sources{};
        std::array<uint8_t, 512> merged_data{};
        uint16_t merged_length = 0;
    };

    static constexpr size_t kMaxTrackedUniverses = 4;
    std::array<UniverseState, kMaxTrackedUniverses> tracked_universes_{};

    // Pre-allocated live frame buffers to eliminate stack/heap allocation in streaming path (Batch 5 Item 13)
    LiveDmxFrame assembled_frame_{};
    LiveDmxFrame submission_frame_{};

    UniverseState* get_or_create_universe(uint16_t universe);
    bool is_universe_live(const UniverseState& u, uint32_t now) const;
    void recompute_merge(UniverseState& state, uint32_t current_ms);
    void dispatch_patch(uint16_t universe, const uint8_t* data, uint16_t length);
};

} // namespace lg

