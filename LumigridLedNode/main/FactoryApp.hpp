#pragma once
#include <cstdint>

#define LG_FACTORY_BUILD 1

// Begin with short known-good strips only.
constexpr uint16_t LG_FACTORY_PIXELS_PER_LANE = 30;

// Never start a high-power board at 100% during initial bring-up.
constexpr uint8_t LG_FACTORY_DEFAULT_BRIGHTNESS = 32;

// A test effect automatically blackouts after this time unless loop mode is set.
constexpr uint32_t LG_FACTORY_MAX_TEST_MS = 60000;
