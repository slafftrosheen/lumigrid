// components/signage/include/RuntimeHardeningTests.hpp
#pragma once
#include "esp_err.h"

namespace lg {

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
/// Automated unit and integration test suite executing actual C++ runtime components
bool run_runtime_hardening_tests();
#else
inline bool run_runtime_hardening_tests() {
    return true;
}
#endif

} // namespace lg
