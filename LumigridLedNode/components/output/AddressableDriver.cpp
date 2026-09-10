#include "AddressableDriver.hpp"
#include "lg_board_pins.hpp"

#include "led_strip.h"
#include "esp_check.h"
#include <cstring>

namespace lg {

esp_err_t AddressableDriver::init() {
    ESP_LOGI("aled", "Initializing AddressableDriver...");
    // Just initialize the array to nullptrs, wait for configure_lane
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        strips_[lane] = nullptr;
        has_white_[lane] = false;
        num_pixels_[lane] = 0;
    }
    return ESP_OK;
}

esp_err_t AddressableDriver::configure_lane(uint8_t lane, uint8_t gpio, FixtureProfile profile, PixelOrder order, uint16_t num_pixels) {
    if (lane >= kLanes) return ESP_ERR_INVALID_ARG;

    // Tear down existing strip if any
    if (strips_[lane] != nullptr) {
        led_strip_del(strips_[lane]);
        strips_[lane] = nullptr;
    }
    num_pixels_[lane] = num_pixels;

    if (num_pixels == 0) return ESP_OK;

    led_model_t model = LED_MODEL_WS2812;
    if (profile == FixtureProfile::SK6812) {
        model = LED_MODEL_SK6812;
        has_white_[lane] = true;
    } else {
        has_white_[lane] = false;
    }

    led_color_component_format_t format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    if (order == PixelOrder::RGB) format = LED_STRIP_COLOR_COMPONENT_FMT_RGB;
    else if (order == PixelOrder::GRB) format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    else if (order == PixelOrder::RGBW) format = LED_STRIP_COLOR_COMPONENT_FMT_RGBW;
    else if (order == PixelOrder::GRBW) format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW;

    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = static_cast<uint32_t>(num_pixels),
        .led_model = model,
        .color_component_format = format,
        .flags = {
            .invert_out = false
        }
    };

    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64, 
        .flags = {
            .with_dma = false
        }
    };

    ESP_LOGI("aled", "Configuring RMT lane %u (GPIO %d, pixels %u, model %d)", lane, gpio, num_pixels, (int)model);
    return led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strips_[lane]);
}

void AddressableDriver::clear() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (strips_[lane]) {
            led_strip_clear(strips_[lane]);
            for (uint16_t p = 0; p < kPixels; ++p) {
                buffer_[lane][p] = {0, 0, 0};
            }
        }
    }
}

void AddressableDriver::clear_buffer() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        for (uint16_t p = 0; p < kPixels; ++p) {
            buffer_[lane][p] = {0, 0, 0};
        }
    }
}

void AddressableDriver::set(uint8_t lane, uint16_t pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || strips_[lane] == nullptr) return;
    
    if (has_white_[lane]) {
        led_strip_set_pixel_rgbw(strips_[lane], pixel, r, g, b, w);
    } else {
        led_strip_set_pixel(strips_[lane], pixel, r, g, b);
    }
    
    if (pixel < kPixels) {
        buffer_[lane][pixel] = {r, g, b};
    }
}

void AddressableDriver::fill(uint8_t lane, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (lane >= kLanes || strips_[lane] == nullptr) return;
    for (uint16_t i = 0; i < num_pixels_[lane]; ++i) {
        set(lane, i, r, g, b, w);
    }
}

esp_err_t AddressableDriver::show() {
    for (uint8_t lane = 0; lane < kLanes; ++lane) {
        if (strips_[lane]) {
            ESP_RETURN_ON_ERROR(
                led_strip_refresh(strips_[lane]),
                "aled",
                "refresh lane=%u", lane
            );
        }
    }
    return ESP_OK;
}

esp_err_t AddressableDriver::blackout() {
    clear();
    return show();
}

} // namespace lg
