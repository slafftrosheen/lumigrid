// components/signage/include/RuntimeHardeningTests.hpp
#pragma once
#include "esp_err.h"

namespace lg {

/// Automated unit and integration test suite executing actual C++ runtime components
/// covering Batch 2 requirements:
/// - Items 21: ColorPipeline (gamma 0, gamma 22, gamma reload, PWM 12-bit, RGBW preservation, SK6812 subtractive, max brightness, white balance)
/// - Item 2: DmxRouter sequence wrap handling (0->1, 254->255->0, duplicate rejection, stale rejection)
/// - Item 16: ConfigStore schema v1 -> v2 deterministic decode and migration
/// - Item 22: Multi-source DMX handoff, fixture vs zone scoping, latched E-stop blackout
bool run_runtime_hardening_tests();

} // namespace lg
