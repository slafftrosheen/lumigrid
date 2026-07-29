#include "FactoryApp.hpp"
#include "AddressableLeds.hpp"
#include "lg_board_pins.hpp"

#include "led_strip.h"
#include "esp_check.h"
#include <cstring>

namespace lg {

esp_err_t AddressableLeds::init() {
    ESP_LOGI("aled", "Initializing AddressableLeds...");
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        ESP_LOGI("aled", "Configuring RMT for lane %u (GPIO %d)...", lane, board::kAledPins[lane]);
        const led_strip_config_t strip_cfg = {
            .strip_gpio_num = board::kAledPins[lane],
            .max_leds = kPixels,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = {
                .invert_out = false
            }
        };

        const led_strip_rmt_config_t rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 64, // explicitly set this to avoid 0 default crash
            .flags = {
                .with_dma = false
            }
        };

        const esp_err_t err = led_strip_new_rmt_device(
            &strip_cfg,
            &rmt_cfg,
            &strips_[lane]
        );

        if (err != ESP_OK) {
            ESP_LOGE("aled", "Failed to init RMT for lane %u", lane);
            return err;
        }
        ESP_LOGI("aled", "Lane %u initialized OK.", lane);
    }

    ESP_LOGI("aled", "All lanes initialized, blacking out...");
    return blackout();
}

void AddressableLeds::clear() {
    for (auto& lane : buffer_) {
        lane.fill({0, 0, 0});
    }
}

void AddressableLeds::set(uint8_t lane, uint16_t pixel, Rgb color) {
    if (lane >= kLanes || pixel >= kPixels) return;
    buffer_[lane][pixel] = color;
}

void AddressableLeds::fill(uint8_t lane, Rgb color) {
    if (lane >= kLanes) return;
    buffer_[lane].fill(color);
}

void AddressableLeds::fill_all(Rgb color) {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        fill(lane, color);
    }
}

esp_err_t AddressableLeds::show() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        for (uint16_t pixel = 0; pixel < kPixels; ++pixel) {
            const Rgb c = buffer_[lane][pixel];
            ESP_RETURN_ON_ERROR(
                led_strip_set_pixel(strips_[lane], pixel, c.r, c.g, c.b),
                "aled",
                "set lane=%u pixel=%u", lane, pixel
            );
        }

        ESP_RETURN_ON_ERROR(
            led_strip_refresh(strips_[lane]),
            "aled",
            "refresh lane=%u", lane
        );
    }

    return ESP_OK;
}

esp_err_t AddressableLeds::blackout() {
    clear();
    return show();
}

} // namespace lg
