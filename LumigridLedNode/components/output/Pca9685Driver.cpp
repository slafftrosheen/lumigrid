#include "Pca9685Driver.hpp"
#include "lg_board_pins.hpp"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

namespace lg {
namespace {
constexpr uint8_t kMode1 = 0x00;
constexpr uint8_t kMode2 = 0x01;
constexpr uint8_t kPrescale = 0xFE;
constexpr uint8_t kLed0OnL = 0x06;

constexpr uint8_t kMode1Ai = 0x20;
constexpr uint8_t kMode1Sleep = 0x10;
constexpr uint8_t kMode1Restart = 0x80;
constexpr uint8_t kMode2OutDrv = 0x04;

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_dev = nullptr;
SemaphoreHandle_t s_i2c_mutex = nullptr;

esp_err_t init_i2c_once() {
    if (s_bus != nullptr) return ESP_OK;

    // Create mutex to protect I2C bus from concurrent access.
    if (s_i2c_mutex == nullptr) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == nullptr) return ESP_ERR_NO_MEM;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = lg::board::kI2cSda,
        .scl_io_num = lg::board::kI2cScl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 4,
        .flags = {
            .enable_internal_pullup = true,
            // .allow_pd = false  // Only required for newer IDF, leaving out for compat
        }
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), "pca", "bus");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = lg::board::kPcaAddress,
        .scl_speed_hz = 100000
    };

    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}
}

esp_err_t Pca9685Driver::write_register(uint8_t reg, uint8_t value) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const uint8_t packet[] = {reg, value};
    esp_err_t ret = i2c_master_transmit(s_dev, packet, sizeof(packet), 100);
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}

esp_err_t Pca9685Driver::write_bytes(uint8_t start_reg,
                               const uint8_t* data,
                               size_t len) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    uint8_t packet[1 + kChannels * 4] = {};
    packet[0] = start_reg;

    if (len > sizeof(packet) - 1) return ESP_ERR_INVALID_SIZE;

    memcpy(packet + 1, data, len);
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t ret = i2c_master_transmit(s_dev, packet, len + 1, 100);
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}

esp_err_t Pca9685Driver::init() {
    // Configure the /OE (Output Enable) pin: active-low.
    // Must be driven LOW for PCA9685 PWM outputs to be enabled.
    gpio_config_t oe_conf = {
        .pin_bit_mask = (1ULL << lg::board::kPcaOe),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_RETURN_ON_ERROR(gpio_config(&oe_conf), "pca", "oe gpio cfg");
    ESP_RETURN_ON_ERROR(gpio_set_level(lg::board::kPcaOe, 0), "pca", "oe low");

    ESP_RETURN_ON_ERROR(init_i2c_once(), "pca", "i2c init");

    ESP_LOGI("pca", "Initializing PCA9685 at 0x%02X...", lg::board::kPcaAddress);
    
    // Sleep before changing prescale.
    esp_err_t err = write_register(kMode1, kMode1Sleep | kMode1Ai);
    if (err != ESP_OK) {
        ESP_LOGE("pca", "Failed to communicate with PCA9685! Error: %s", esp_err_to_name(err));
        return err;
    }

    // Prescale 5 ~= 1017 Hz with 25 MHz nominal oscillator.
    // These writes are critical — propagate any failure immediately.
    ESP_RETURN_ON_ERROR(write_register(kPrescale, 5), "pca", "prescale");
    ESP_RETURN_ON_ERROR(write_register(kMode2, kMode2OutDrv), "pca", "mode2");
    
    // Clear SLEEP bit (keep Ai) to wake up the oscillator
    ESP_RETURN_ON_ERROR(write_register(kMode1, kMode1Ai), "pca", "wake");
    
    // Datasheet requires 500us max to stabilize.
    vTaskDelay(pdMS_TO_TICKS(1)); 
    
    // Set RESTART bit
    ESP_RETURN_ON_ERROR(write_register(kMode1, kMode1Ai | kMode1Restart), "pca", "restart");

    // Verify chip communication with a read-back of MODE1.
    uint8_t reg = kMode1;
    uint8_t val = 0;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &val, 1, 100);
    xSemaphoreGive(s_i2c_mutex);

    if (err == ESP_OK) {
        ESP_LOGI("pca", "PCA9685 I2C communication verified at 0x%02X (MODE1: 0x%02X)", lg::board::kPcaAddress, val);
    } else {
        ESP_LOGW("pca", "Read-back failed: %s (non-fatal)", esp_err_to_name(err));
    }

    ESP_LOGD("pca", "Clearing values...");
    return clear();
}

esp_err_t Pca9685Driver::set(uint8_t channel, uint16_t duty) {
    if (channel >= kChannels || duty > 4095) return ESP_ERR_INVALID_ARG;
    values_[channel] = duty;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_all(uint16_t duty) {
    if (duty > 4095) return ESP_ERR_INVALID_ARG;
    values_.fill(duty);
    return ESP_OK;
}

esp_err_t Pca9685Driver::clear() {
    values_.fill(0);
    return commit();
}

esp_err_t Pca9685Driver::commit() {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) {
        return ESP_OK;
    }
    uint8_t bytes[kChannels * 4] = {};

    for (uint8_t channel = 0; channel < kChannels; ++channel) {
        const uint16_t off = values_[channel];
        const size_t offset = channel * 4;

        bytes[offset + 0] = 0x00;              // LEDn_ON_L
        bytes[offset + 1] = 0x00;              // LEDn_ON_H
        bytes[offset + 2] = off & 0xFF;        // LEDn_OFF_L
        bytes[offset + 3] = (off >> 8) & 0x0F; // LEDn_OFF_H
    }

    return write_bytes(kLed0OnL, bytes, sizeof(bytes));
}

} // namespace lg
