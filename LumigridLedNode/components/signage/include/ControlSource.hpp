// components/signage/include/ControlSource.hpp
#pragma once
#include <cstdint>

namespace lg {

enum class ControlSource : uint8_t {
    Idle = 0,
    Fallback = 1,
    ScenePlayback = 2,
    Scheduler = 3,
    ManualOverride = 4,
    LiveDmx = 5,
    Factory = 6,
    EStop = 7
};

struct ControlState {
    ControlSource source = ControlSource::Idle;
    uint32_t timestamp_ms = 0;
    uint32_t expires_ms = 0;    // 0 = indefinite
    uint8_t  zone_mask = 0xFF;  // Bitmask of controlled zones (0-7)
    uint8_t  layer_mask = 0x07; // Bitmask of controlled layers (0-2)
    bool     takeover = false;
    bool     active = false;
};

} // namespace lg
