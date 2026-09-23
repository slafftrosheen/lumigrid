// components/signage/include/ManualControlTypes.hpp
#pragma once
#include <cstdint>
#include <array>
#include "RenderTypes.hpp"

namespace lg {

struct ManualChannelState {
    bool active = false;
    // Addressable channel: RGB + brightness (0-255)
    LightValue color{};
    uint8_t brightness = 255;
    // PWM channel: 12-bit duty (0-4095)
    uint16_t pwm = 0;
};

struct ManualOverrideFrame {
    std::array<ManualChannelState, 16> channels{};
    uint16_t channel_mask = 0;
    uint32_t timestamp_ms = 0;
    uint32_t lease_ms = 15000;
};

struct ManualControlStatus {
    bool active = false;
    uint16_t channel_mask = 0;
    uint32_t lease_remaining_ms = 0;
};

} // namespace lg
