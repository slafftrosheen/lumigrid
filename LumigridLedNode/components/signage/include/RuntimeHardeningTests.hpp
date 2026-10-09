// components/signage/include/RuntimeHardeningTests.hpp
#pragma once
#include <cstdint>
#include "esp_err.h"

namespace lg {

enum class RuntimeHardeningResult : uint8_t {
    Passed,
    Failed,
    NotRun
};

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
/// Automated unit and integration test suite executing actual C++ runtime components
RuntimeHardeningResult run_runtime_hardening_tests();
#else
inline RuntimeHardeningResult run_runtime_hardening_tests() {
    return RuntimeHardeningResult::NotRun;
}
#endif

} // namespace lg
