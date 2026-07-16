#include "OutputController.hpp"
#include "lg_board_pins.hpp"

#include "driver/gpio.h"
#include "esp_check.h"

namespace lg {

esp_err_t OutputController::init() {
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << board::kPcaOe,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_RETURN_ON_ERROR(gpio_config(&cfg), "output", "oe cfg");

    // PCA OE# disabled before any bus or LED initialization.
    disable();

    ESP_RETURN_ON_ERROR(pwm_.init(), "output", "pca init");
    ESP_RETURN_ON_ERROR(pixels_.init(), "output", "aled init");

    return blackout();
}

void OutputController::disable() {
    gpio_set_level(board::kPcaOe, 1);
    enabled_ = false;
}

void OutputController::enable() {
    gpio_set_level(board::kPcaOe, 0);
    enabled_ = true;
}

esp_err_t OutputController::blackout() {
    disable();
    ESP_RETURN_ON_ERROR(pwm_.clear(), "output", "pwm clear");
    return pixels_.blackout();
}

esp_err_t OutputController::commit() {
    ESP_RETURN_ON_ERROR(pwm_.commit(), "output", "pwm commit");
    ESP_RETURN_ON_ERROR(pixels_.show(), "output", "pixel show");
    return ESP_OK;
}

} // namespace lg
