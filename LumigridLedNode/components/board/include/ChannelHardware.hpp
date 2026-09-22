// components/board/include/ChannelHardware.hpp
#pragma once
#include "lg_board_pins.hpp"
#include <cstdint>
#include <cstdio>

namespace lg {

constexpr uint8_t kAddressableChannelCount = 8;
constexpr uint8_t kPwmChannelCount = 8;
constexpr uint8_t kChannelCount = 16;

enum class ChannelKind : uint8_t {
    Addressable = 0,
    Pwm = 1
};

struct ChannelHardwareDescriptor {
    uint8_t index;            // 0..15
    ChannelKind kind;         // Addressable or Pwm
    uint8_t hardware_id;      // 0..7 within RMT / PCA9685 device
    int8_t gpio;              // GPIO pin for addressable, -1 for PWM
    const char* port_name;    // "A1".."A8" or "P1".."P8"
};

inline void channel_port_name(uint8_t index, char* out, size_t len) {
    if (!out || len == 0) return;
    if (index < 8) {
        snprintf(out, len, "A%u", index + 1);
    } else if (index < 16) {
        snprintf(out, len, "P%u", index - 7);
    } else {
        snprintf(out, len, "?%u", index);
    }
}

inline ChannelHardwareDescriptor channel_hardware(uint8_t index) {
    static const char* const kPortNames[16] = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8",
        "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"
    };

    if (index < 8) {
        return ChannelHardwareDescriptor{
            index,
            ChannelKind::Addressable,
            index,
            static_cast<int8_t>(board::kAledPins[index]),
            kPortNames[index]
        };
    }
    if (index < 16) {
        return ChannelHardwareDescriptor{
            index,
            ChannelKind::Pwm,
            static_cast<uint8_t>(index - 8),
            -1,
            kPortNames[index]
        };
    }
    return ChannelHardwareDescriptor{
        index,
        ChannelKind::Addressable,
        0,
        -1,
        "?"
    };
}

} // namespace lg
