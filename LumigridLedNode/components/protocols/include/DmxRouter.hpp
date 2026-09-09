// components/protocols/include/DmxRouter.hpp
#pragma once
#include <cstdint>
#include <array>
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "SignageTypes.hpp"

namespace lg {

class LightingRuntime;

/// Dedicated DMX routing engine for Art-Net and sACN protocols.
///
/// Handles universe filtering, sACN stream priority resolution,
/// HTP / LTP channel merging, and route dispatch to either:
///   1. Zone Wash (8 zones consecutively mapped from universe_start and zone_start_channel)
///   2. Direct Fixture Patch (per-fixture dmx_universe, dmx_start_channel, and dmx_mode)
///
/// Operates entirely on cached configuration in memory to eliminate NVS thrashing on high-frequency UDP packets.
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

    /// Process a generic DMX frame
    void process_frame(uint16_t universe, const uint8_t* data, uint16_t length, uint8_t priority);

private:
    LightingRuntime& lighting_;
    ProtocolConfig protocol_cfg_{};
    std::array<FixtureConfig, 16> fixtures_{};

    // Tracking state per active universe (we support up to 16 universes concurrently)
    struct UniverseState {
        uint16_t universe = 0xFFFF;
        uint8_t  last_priority = 0;
        uint32_t last_packet_ms = 0;
        std::array<uint8_t, 512> merged_data = {};
        bool active = false;
    };
    static constexpr size_t kMaxTrackedUniverses = 4;
    std::array<UniverseState, kMaxTrackedUniverses> tracked_universes_{};

    UniverseState* get_or_create_universe(uint16_t universe);
    void dispatch_patch(uint16_t universe, const uint8_t* data, uint16_t length);
};

} // namespace lg
