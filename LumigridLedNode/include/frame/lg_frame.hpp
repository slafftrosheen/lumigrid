#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "config/lg_config.hpp"

namespace lg {

constexpr size_t kPixelLaneCount = 8;
constexpr size_t kMaxLogicalPixels = kMaxPixelsPerLane * kPixelLaneCount;
constexpr size_t kPwmChannelCount = 16;

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  Rgb() = default;
  constexpr Rgb(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
};

struct Rgbw {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;
};

struct LogicalFrame {
  std::array<Rgb, kMaxLogicalPixels> pixels{};
  std::array<uint16_t, kPwmChannelCount> pwm{};
  uint32_t sequence = 0;
  uint32_t timestamp_ms = 0;
  uint16_t pixel_count = 0;
};

struct PhysicalFrame {
  std::array<std::array<Rgb, kMaxPixelsPerLane>, kPixelLaneCount> lanes{};
  std::array<uint16_t, kPwmChannelCount> pwm{};
  uint32_t sequence = 0;
};

}  // namespace lg
