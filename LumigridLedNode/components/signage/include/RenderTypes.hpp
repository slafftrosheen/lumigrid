#pragma once
#include <cstdint>
#include <array>
#include "SignageTypes.hpp"

namespace lg {

constexpr uint8_t MAX_FIXTURES = 16;

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct LightValue {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t w = 0;

    constexpr LightValue() = default;
    constexpr LightValue(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t w_ = 0)
        : r(r_), g(g_), b(b_), w(w_) {}
    constexpr LightValue(const Rgb& rgb)
        : r(rgb.r), g(rgb.g), b(rgb.b), w(0) {}
    constexpr LightValue(const BrandColor& bc)
        : r(bc.r), g(bc.g), b(bc.b), w(0) {}
};

struct ChannelFrame {
    std::array<LightValue, MAX_PIXELS> pixels = {};
    uint16_t pwm = 0; // 12-bit PCA9685 duty cycle (0..4095) for PWM channels
};

using FixtureFrame = ChannelFrame;

struct RenderFrame {
    std::array<ChannelFrame, kChannelCount> channels = {};
};

// ── Persistent Live DMX Buffering ──

struct LiveDmxLayer {
    BrandColor color{0, 0, 0};
    uint8_t opacity = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

struct LiveDmxChannel {
    std::array<LightValue, MAX_PIXELS> pixels = {};
    uint16_t pwm = 0;
    uint16_t pixel_count = 0;
    uint8_t opacity = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

using LiveDmxFixture = LiveDmxChannel;

struct LiveDmxFrame {
    std::array<std::array<LiveDmxLayer, 3>, 8> layers = {};
    std::array<LiveDmxChannel, kChannelCount> channels = {};
    std::array<uint8_t, 512> dmx_telemetry = {};
    uint16_t dmx_telemetry_len = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

// ── Frame Timing and Diagnostics (LG-038) ──

enum class FrameResult : uint8_t {
    Committed = 0,
    NoOutput,
    Failed,
    EstopBlackout,
};

struct FrameTimingMetrics {
    float loop_fps = 0.0f;
    float committed_fps = 0.0f;

    uint32_t last_frame_us = 0;
    uint32_t max_frame_us = 0;

    uint32_t deadline_misses = 0;
    uint32_t dropped_periods = 0;

    uint32_t failed_frames = 0;
};

enum class FrameBudgetStatus : uint8_t {
    Healthy = 0,
    NearLimit = 1,
    Impossible = 2,
};

struct FrameBudgetEstimate {
    uint32_t longest_addr_us = 0;
    uint32_t estimated_pwm_us = 0;
    uint32_t total_estimated_us = 0;
    FrameBudgetStatus status = FrameBudgetStatus::Healthy;
};

} // namespace lg
