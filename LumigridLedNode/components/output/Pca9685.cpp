#include "Pca9685.hpp"
#include "lg_board_pins.hpp"

#include "driver/i2c_master.h"
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

esp_err_t init_i2c_once() {
    if (s_bus != nullptr) return ESP_OK;

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
        .scl_speed_hz = 400000
    };

    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}
}

esp_err_t Pca9685::write_register(uint8_t reg, uint8_t value) {
    const uint8_t packet[] = {reg, value};
    return i2c_master_transmit(s_dev, packet, sizeof(packet), 100);
}

esp_err_t Pca9685::write_bytes(uint8_t start_reg,
                               const uint8_t* data,
                               size_t len) {
    uint8_t packet[1 + kChannels * 4] = {};
    packet[0] = start_reg;

    if (len > sizeof(packet) - 1) return ESP_ERR_INVALID_SIZE;

    memcpy(packet + 1, data, len);
    return i2c_master_transmit(s_dev, packet, len + 1, 100);
}

esp_err_t Pca9685::init() {
    ESP_RETURN_ON_ERROR(init_i2c_once(), "pca", "i2c init");

    // Initialize the PCA9685 directly without scanning
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = lg::board::kPcaAddress,
        .scl_speed_hz = 100000
    };
    if (s_dev == nullptr) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), "pca", "add dev");
    }

    ESP_LOGI("pca", "Initializing PCA9685 at 0x%02X...", lg::board::kPcaAddress);
    
    // Sleep before changing prescale.
    esp_err_t err = write_register(kMode1, kMode1Sleep | kMode1Ai);
    if (err != ESP_OK) {
        ESP_LOGE("pca", "Failed to communicate with PCA9685! Error: %s", esp_err_to_name(err));
        return err;
    }

    // Prescale 5 ~= 1017 Hz with 25 MHz nominal oscillator
    write_register(kPrescale, 5);
    write_register(kMode2, kMode2OutDrv);
    write_register(kMode1, kMode1Ai | kMode1Restart);

    ESP_LOGW("pca", "Testing read-back from PCA9685...");
    
    uint8_t reg = kMode1;
    uint8_t val = 0;
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &val, 1, 100);
    
    if (err == ESP_OK) {
        ESP_LOGW("pca", ">>> READ MODE1 REGISTER: 0x%02X <<<", val);
        if (val == 0x00 || val == 0xFF) {
            ESP_LOGE("pca", "WARNING: Read 0x%02X. The chip might be dead or bus is stuck!", val);
        } else {
            ESP_LOGW("pca", "Valid data read! The I2C bus is genuinely working!");
        }
    } else {
        ESP_LOGE("pca", "Read failed! Error: %s", esp_err_to_name(err));
    }

    ESP_LOGW("pca", "Test complete.");


    // Sleep before changing prescale.
    ESP_RETURN_ON_ERROR(write_register(kMode1, kMode1Sleep | kMode1Ai),
                        "pca", "sleep");

    // Prescale 5 ~= 1017 Hz with 25 MHz nominal oscillator:
    // 25,000,000 / (4096 * (5 + 1)) ~= 1017 Hz.
    ESP_RETURN_ON_ERROR(write_register(kPrescale, 5), "pca", "prescale");

    ESP_RETURN_ON_ERROR(write_register(kMode2, kMode2OutDrv),
                        "pca", "mode2");

    ESP_RETURN_ON_ERROR(write_register(kMode1, kMode1Ai | kMode1Restart),
                        "pca", "wake");

    return clear();
}

esp_err_t Pca9685::set(uint8_t channel, uint16_t duty) {
    if (channel >= kChannels || duty > 4095) return ESP_ERR_INVALID_ARG;
    values_[channel] = duty;
    return ESP_OK;
}

esp_err_t Pca9685::set_all(uint16_t duty) {
    if (duty > 4095) return ESP_ERR_INVALID_ARG;
    values_.fill(duty);
    return ESP_OK;
}

esp_err_t Pca9685::clear() {
    values_.fill(0);
    return commit();
}

esp_err_t Pca9685::commit() {
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
