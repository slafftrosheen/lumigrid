// components/protocols/include/DmxRouter.hpp
#pragma once
#include <cstdint>
#include <array>
#include <mutex>
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "SignageTypes.hpp"
#include "RenderTypes.hpp"

namespace lg {

class LightingRuntime;

enum class DmxProtocol : uint8_t {
    ArtNet = 1,
    sACN = 2
};

struct SourceId {
    DmxProtocol protocol = DmxProtocol::ArtNet;
    uint32_t identifier = 0; // Source IP for Art-Net, CID hash for sACN

    bool operator==(const SourceId& other) const {
        return protocol == other.protocol && identifier == other.identifier;
    }
    bool operator!=(const SourceId& other) const {
        return !(*this == other);
    }
};

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
                       uint8_t priority, SourceId source_id, uint8_t sequence = 0);

    /// Extract list of active universes patched across zone wash and fixtures
    size_t get_active_universes(uint16_t* out_universes, size_t max_count) const;

    /// Unified stream timeout helper
    uint32_t stream_timeout_ms() const {
        return protocol_cfg_.stream_timeout_ms > 0 ? protocol_cfg_.stream_timeout_ms : 2000;
    }

private:
    LightingRuntime& lighting_;
    mutable std::mutex frame_mutex_;
    ProtocolConfig protocol_cfg_{};
    std::array<FixtureConfig, 16> fixtures_{};

    // Tracking state per active stream contributor within a universe
    struct SourceStream {
        SourceId source_id{};
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

    // Pre-allocated live frame buffer to eliminate stack/heap allocation in streaming path
    LiveDmxFrame live_frame_{};

    UniverseState* get_or_create_universe(uint16_t universe);
    void recompute_merge(UniverseState& state, uint32_t current_ms);
    void dispatch_patch(uint16_t universe, const uint8_t* data, uint16_t length);
};

} // namespace lg

