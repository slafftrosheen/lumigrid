#pragma once
#include <lg_config.hpp>
#include <frame/lg_frame.hpp>
#include <cstddef>
#include <cstdint>

namespace lg {

class ArtnetMapper {
 public:
  explicit ArtnetMapper(uint16_t first_universe);

  void clear();
  void apply_universe(uint16_t universe, const uint8_t* dmx, size_t length);

  const LogicalFrame& staging_frame() const;
  LogicalFrame take_frame(uint32_t sequence, uint32_t timestamp_ms);

 private:
  uint16_t first_universe_;
  LogicalFrame frame_{};
};

}  // namespace lg
