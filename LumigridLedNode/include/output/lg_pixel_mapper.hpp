#pragma once
#include <config/lg_config.hpp>
#include <frame/lg_frame.hpp>
#include <stdint.h>

namespace lg {

class PixelMapper {
 public:
  explicit PixelMapper(const NodeConfig& config);
  void update_config(const NodeConfig& config);
  void map(const LogicalFrame& source, PhysicalFrame& target) const;

 private:
  static uint8_t gamma8(uint8_t value);
  static Rgb scale(Rgb color, uint8_t brightness);
  static Rgb reorder(Rgb color, PixelOrder order);

  NodeConfig config_;
};

}  // namespace lg
