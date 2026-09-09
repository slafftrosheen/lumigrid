#pragma once
#include <cstdint>
#include <array>
#include "SignageTypes.hpp"

namespace lg {

constexpr uint16_t MAX_PIXELS = 256;
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

struct FixtureFrame {
    std::array<LightValue, MAX_PIXELS> pixels = {};
    std::array<uint16_t, 4> pwm = {}; // 12-bit PCA9685 duty cycles (0..4095) for up to 4 physical channels
};

struct RenderFrame {
    std::array<FixtureFrame, MAX_FIXTURES> fixtures = {};
};

// ── Persistent Live DMX Buffering ──

struct LiveDmxLayer {
    BrandColor color{0, 0, 0};
    uint8_t opacity = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

struct LiveDmxFixture {
    std::array<LightValue, MAX_PIXELS> pixels = {};
    std::array<uint16_t, 4> pwm = {};
    uint16_t pixel_count = 0;
    uint8_t opacity = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

struct LiveDmxFrame {
    std::array<std::array<LiveDmxLayer, 3>, 8> layers = {};
    std::array<LiveDmxFixture, MAX_FIXTURES> fixtures = {};
    std::array<uint8_t, 512> dmx_telemetry = {};
    uint16_t dmx_telemetry_len = 0;
    uint32_t timestamp_ms = 0;
    bool active = false;
};

} // namespace lg
