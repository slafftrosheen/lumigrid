#include "OutputController.hpp"
#include "lg_board_pins.hpp"

#include "driver/gpio.h"
#include "esp_check.h"

namespace lg {

esp_err_t OutputController::init() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

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
    xSemaphoreTake(mutex_, portMAX_DELAY);
    gpio_set_level(board::kPcaOe, 1);
    enabled_ = false;
    xSemaphoreGive(mutex_);
}

void OutputController::enable() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    gpio_set_level(board::kPcaOe, 0);
    enabled_ = true;
    xSemaphoreGive(mutex_);
}

esp_err_t OutputController::blackout() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    gpio_set_level(board::kPcaOe, 1);
    enabled_ = false;
    esp_err_t err = pwm_.clear();
    if (err == ESP_OK) {
        err = pixels_.blackout();
    }
    xSemaphoreGive(mutex_);
    return err;
}

esp_err_t OutputController::commit() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_err_t err = pwm_.commit();
    if (err == ESP_OK) {
        err = pixels_.show();
    }
    if (err != ESP_OK) {
        gpio_set_level(board::kPcaOe, 1);
        enabled_ = false;
        ESP_LOGE("output", "Hardware commit failed (%s), disabling output safety interlock", esp_err_to_name(err));
    }
    xSemaphoreGive(mutex_);
    return err;
}

} // namespace lg
