#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lg {
constexpr size_t kPixelLanes = 8;
constexpr size_t kPwmChannels = 16;
constexpr size_t kMaxPixelsPerLane = 1000;

enum class PixelOrder : uint8_t { GRB, RGB, GRBW, RGBW };
enum class PwmKind : uint8_t { Disabled, Dimmer, RGB, RGBW, CCT };

struct PixelLaneConfig {
    bool enabled = false;
    uint8_t gpio = 0;
    uint16_t pixel_count = 0;
    uint16_t logical_start = 0;
    PixelOrder order = PixelOrder::GRB;
    bool reversed = false;
    uint8_t brightness = 255;
};

struct PwmGroupConfig {
    bool enabled = false;
    PwmKind kind = PwmKind::Disabled;
    uint8_t first_channel = 0;
    uint8_t channel_count = 0;
    uint16_t logical_start = 0;
    bool invert = false;
};

struct ArtnetConfig {
    bool enabled = true;
    uint16_t first_universe = 0;
    uint16_t universe_count = 16;
    uint32_t source_timeout_ms = 3000;
    bool accept_broadcast = true;
};

struct MqttConfig {
    bool enabled = false;
    std::string uri;          // mqtts://broker.example:8883
    std::string username;
    std::string password;
    std::string base_topic;   // lumigrid/<node-id>
    bool tls_verify = true;
};

struct NodeConfig {
    uint16_t schema_version = 1;
    std::string node_id = "lumigrid-node";
    std::string group_id = "default";
    uint16_t target_fps = 60;
    uint16_t pwm_frequency_hz = 1000;
    uint32_t max_pixel_ma = 0; // 0 = no software cap until calibrated
    std::array<PixelLaneConfig, kPixelLanes> lanes{};
    std::array<PwmGroupConfig, kPwmChannels> pwm_groups{};
    ArtnetConfig artnet{};
    MqttConfig mqtt{};
};

} // namespace lg
