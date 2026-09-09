#pragma once
#include <cstdint>
#include <array>

namespace lg {

constexpr uint16_t MAX_PIXELS = 1024;
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
};

struct FixtureFrame {
    std::array<LightValue, MAX_PIXELS> pixels = {};
    std::array<uint16_t, 4> pwm = {}; // 12-bit PCA9685 duty cycles (0..4095) for up to 4 physical channels
};

struct RenderFrame {
    std::array<FixtureFrame, MAX_FIXTURES> fixtures = {};
};

} // namespace lg
