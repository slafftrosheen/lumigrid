#include "BoardOutputSafety.hpp"
#include "lg_board_pins.hpp"
#include "driver/gpio.h"

namespace lg::board {

static BootOutputDiagnostics s_diag{};

esp_err_t force_outputs_safe_early() {
    // 1. Immediately drive PCA /OE HIGH (disable PWM channels)
    gpio_reset_pin(kPcaOe);
    gpio_set_direction(kPcaOe, GPIO_MODE_OUTPUT);
    gpio_set_level(kPcaOe, 1);
    s_diag.pca_oe_high_at_bootstrap = (gpio_get_level(kPcaOe) == 1);

    // 2. Drive all 8 ALED pins LOW so they do not float before RMT takes ownership
    s_diag.aled_low_mask = 0;
    for (uint8_t i = 0; i < kAledCount; ++i) {
        gpio_num_t pin = kAledPins[i];
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
        if (gpio_get_level(pin) == 0) {
            s_diag.aled_low_mask |= static_cast<uint8_t>(1u << i);
        }
    }

    s_diag.early_safety_applied = true;
    return ESP_OK;
}

BootOutputDiagnostics get_boot_output_diagnostics() {
    return s_diag;
}

void record_first_addressable_black() {
    s_diag.first_addressable_black_confirmed = true;
}

void record_first_pwm_zero() {
    s_diag.first_pwm_zero_confirmed = true;
}

void record_first_output_commit(uint32_t ms) {
    if (s_diag.first_output_commit_ms == 0) {
        s_diag.first_output_commit_ms = ms;
    }
}

uint8_t sample_addressable_pin_mask() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < kAledCount; ++i) {
        if (gpio_get_level(kAledPins[i]) != 0) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    return mask;
}

int sample_pca_oe_level() {
    return gpio_get_level(kPcaOe);
}

} // namespace lg::board
