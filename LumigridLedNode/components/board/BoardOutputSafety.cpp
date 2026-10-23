#include "BoardOutputSafety.hpp"
#include "lg_board_pins.hpp"
#include "driver/gpio.h"

namespace lg::board {

static BootOutputDiagnostics s_diag{};

esp_err_t force_outputs_safe_early() {
    s_diag = BootOutputDiagnostics{};
    esp_err_t first_error = ESP_OK;
    const auto make_safe_pin = [](gpio_num_t pin, uint32_t level) -> esp_err_t {
        esp_err_t err = gpio_reset_pin(pin);
        if (err != ESP_OK) return err;
        // Preload the output latch before connecting it to the pad. In
        // particular, /OE must not expose the reset latch's LOW level first.
        err = gpio_set_level(pin, level);
        if (err != ESP_OK) return err;
        // gpio_get_level() only observes a pad with input enabled. Keep that
        // path enabled for real bootstrap readback instead of a constant zero.
        return gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
    };
    // 1. Immediately drive PCA /OE HIGH (disable PWM channels)
    first_error = make_safe_pin(kPcaOe, 1);
    s_diag.pca_oe_high_at_bootstrap = first_error == ESP_OK && gpio_get_level(kPcaOe) == 1;
    if (first_error == ESP_OK && !s_diag.pca_oe_high_at_bootstrap) first_error = ESP_FAIL;

    // 2. Drive all 8 ALED pins LOW so they do not float before RMT takes ownership
    s_diag.aled_low_mask = 0;
    for (uint8_t i = 0; i < kAledCount; ++i) {
        gpio_num_t pin = kAledPins[i];
        const esp_err_t err = make_safe_pin(pin, 0);
        if (first_error == ESP_OK && err != ESP_OK) first_error = err;
        if (err == ESP_OK && gpio_get_level(pin) == 0) {
            s_diag.aled_low_mask |= static_cast<uint8_t>(1u << i);
        } else if (first_error == ESP_OK) {
            first_error = ESP_FAIL;
        }
    }

    s_diag.early_safety_applied = first_error == ESP_OK;
    return first_error;
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
