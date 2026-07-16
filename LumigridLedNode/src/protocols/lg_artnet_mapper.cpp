#include "protocols/lg_artnet_mapper.hpp"
#include <algorithm>

namespace lg {

ArtnetMapper::ArtnetMapper(uint16_t first_universe) : first_universe_(first_universe) {}

void ArtnetMapper::clear() { frame_ = {}; }

void ArtnetMapper::apply_universe(uint16_t universe, const uint8_t* dmx, size_t length) {
  if (universe < first_universe_ || dmx == nullptr) {
    return;
  }

  const size_t universe_offset = static_cast<size_t>(universe - first_universe_) * 170U;
  const size_t usable = std::min(length, static_cast<size_t>(510));
  const size_t pixel_count = usable / 3U;

  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t target = universe_offset + pixel;
    if (target >= kMaxLogicalPixels) {
      break;
    }

    const size_t source = pixel * 3U;
    frame_.pixels[target] = {.r = dmx[source], .g = dmx[source + 1], .b = dmx[source + 2]};
    frame_.pixel_count = std::max(frame_.pixel_count, static_cast<uint16_t>(target + 1));
  }
}

const LogicalFrame& ArtnetMapper::staging_frame() const { return frame_; }

LogicalFrame ArtnetMapper::take_frame(uint32_t sequence, uint32_t timestamp_ms) {
  frame_.sequence = sequence;
  frame_.timestamp_ms = timestamp_ms;
  return frame_;
}

}  // namespace lg
