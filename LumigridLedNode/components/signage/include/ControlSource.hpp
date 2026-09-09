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

} // namespace lg
