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

struct FixtureFrame {
    std::array<Rgb, MAX_PIXELS> pixels = {};
    uint16_t pwm = 0;
};

struct RenderFrame {
    std::array<FixtureFrame, MAX_FIXTURES> fixtures = {};
};

} // namespace lg
