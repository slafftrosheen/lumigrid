// components/board/include/BoardOutputSafety.hpp
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace lg::board {

/// Establishes safe electrical states for all physical outputs at the earliest
/// possible point in app_main(). No heap allocation, no logging, no external dependencies.
esp_err_t force_outputs_safe_early();

struct BootOutputDiagnostics {
    bool     early_safety_applied = false;
    bool     pca_oe_high_at_bootstrap = false;
    uint8_t  aled_low_mask = 0;
    bool     first_addressable_black_confirmed = false;
    bool     first_pwm_zero_confirmed = false;
    uint32_t first_output_commit_ms = 0;
};

BootOutputDiagnostics get_boot_output_diagnostics();
void record_first_addressable_black();
void record_first_pwm_zero();
void record_first_output_commit(uint32_t ms);

uint8_t sample_addressable_pin_mask();
int sample_pca_oe_level();

} // namespace lg::board
