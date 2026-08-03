#pragma once
#include <cstdint>

#define LG_FACTORY_BUILD 1

namespace lg {

/// Pixels per addressable LED lane during factory validation.
/// Set conservatively for short known-good strips.
constexpr uint16_t kFactoryPixelsPerLane = 30;

/// Default brightness — never start a high-power board at 100% during bring-up.
constexpr uint8_t kFactoryDefaultBrightness = 32;

/// Maximum test duration before automatic blackout (unless loop mode is set).
constexpr uint32_t kFactoryMaxTestMs = 60000;

} // namespace lg

// Legacy aliases for existing code. Prefer lg::kFactory* in new code.
constexpr uint16_t LG_FACTORY_PIXELS_PER_LANE = lg::kFactoryPixelsPerLane;
constexpr uint8_t  LG_FACTORY_DEFAULT_BRIGHTNESS = lg::kFactoryDefaultBrightness;
constexpr uint32_t LG_FACTORY_MAX_TEST_MS = lg::kFactoryMaxTestMs;
