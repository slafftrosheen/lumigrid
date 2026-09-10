#pragma once
#include <cstdint>
#include <array>

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

/// Explicit centralized priority function (Item 8)
inline constexpr uint8_t source_priority(ControlSource s) {
    switch (s) {
        case ControlSource::EStop:          return 100;
        case ControlSource::Factory:        return 60;
        case ControlSource::LiveDmx:        return 50;
        case ControlSource::ManualOverride: return 40;
        case ControlSource::Scheduler:      return 30;
        case ControlSource::ScenePlayback:  return 20;
        case ControlSource::Fallback:       return 10;
        case ControlSource::Idle:           return 0;
        default:                            return 0;
    }
}

/// Explicit descending priority table ordering
inline constexpr std::array<ControlSource, 8> kPriorityOrder = {
    ControlSource::EStop,
    ControlSource::Factory,
    ControlSource::LiveDmx,
    ControlSource::ManualOverride,
    ControlSource::Scheduler,
    ControlSource::ScenePlayback,
    ControlSource::Fallback,
    ControlSource::Idle
};

struct ControlState {
    ControlSource source = ControlSource::Idle;
    uint32_t timestamp_ms = 0;
    uint32_t expires_ms = 0;    // 0 = indefinite
    uint8_t  zone_mask = 0xFF;  // Bitmask of controlled zones (0-7)
    uint8_t  layer_mask = 0x07; // Bitmask of controlled layers (0-2)
    uint16_t fixture_mask = 0;  // Bitmask of controlled fixtures (0-15)
    bool     takeover = false;
    bool     active = false;
};

} // namespace lg

