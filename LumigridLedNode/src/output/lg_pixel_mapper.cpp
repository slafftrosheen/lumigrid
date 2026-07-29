#include "output/lg_pixel_mapper.hpp"
#include "frame/lg_gamma_lut.hpp"
#include <algorithm>

namespace lg {

PixelMapper::PixelMapper(const NodeConfig& config) : config_(config) {}

void PixelMapper::update_config(const NodeConfig& config) { config_ = config; }

Rgb PixelMapper::scale(Rgb c, uint8_t brightness) {
  return Rgb{
      static_cast<uint8_t>((static_cast<uint16_t>(c.r) * brightness + 127) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.g) * brightness + 127) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.b) * brightness + 127) / 255)};
}

Rgb PixelMapper::reorder(Rgb c, PixelOrder order) {
  switch (order) {
    case PixelOrder::RGB:
      return c;
    case PixelOrder::GRB:
      return Rgb{c.g, c.r, c.b};
    case PixelOrder::RGBW:
    case PixelOrder::GRBW:
      return c;
  }
  return c;
}

uint8_t PixelMapper::gamma8(uint8_t value) {
  return kGamma22[value];
}

void PixelMapper::map(const LogicalFrame& source, PhysicalFrame& target) const {
  target.sequence = source.sequence;
  target.pwm = source.pwm;

  for (size_t lane_index = 0; lane_index < kPixelLaneCount; ++lane_index) {
    const auto& lane = config_.lanes[lane_index];
    if (!lane.enabled || lane.pixel_count == 0) {
      // Ensure disabled lanes are black.
      target.lanes[lane_index].fill(Rgb{0, 0, 0});
      continue;
    }

    for (uint16_t physical = 0; physical < lane.pixel_count; ++physical) {
      const uint16_t source_index =
          lane.logical_start + (lane.reversed ? lane.pixel_count - 1 - physical : physical);

      if (source_index >= source.pixel_count) {
        target.lanes[lane_index][physical] = Rgb{0, 0, 0};
        continue;
      }

      Rgb pixel = scale(source.pixels[source_index], lane.brightness);
      pixel.r = gamma8(pixel.r);
      pixel.g = gamma8(pixel.g);
      pixel.b = gamma8(pixel.b);
      target.lanes[lane_index][physical] = reorder(pixel, lane.order);
    }
  }
}

}  // namespace lg
