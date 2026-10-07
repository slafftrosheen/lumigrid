// components/output/Pca9685Driver.cpp
// PCA9685 16-channel, 12-bit PWM controller driver.
// Reference: NXP PCA9685 datasheet Rev.4; ESP-IDF legacy synchronous I2C driver.
#include "Pca9685Driver.hpp"
#include "lg_board_pins.hpp"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>

namespace lg {
namespace {

static const char* TAG = "pca9685";
constexpr uint8_t kRegMode1    = 0x00;
constexpr uint8_t kRegMode2    = 0x01;
constexpr uint8_t kRegLed0OnL  = 0x06;
constexpr uint8_t kRegPrescale = 0xFE;
constexpr uint8_t kMode1AutoInc = (1u << 5);
constexpr uint8_t kMode1Sleep   = (1u << 4);
constexpr uint8_t kMode1Restart = (1u << 7);
constexpr uint8_t kMode2OutDrv  = (1u << 2);
constexpr uint8_t kPrescaleFor1kHz = 5;

static_assert(kPrescaleFor1kHz >= 3, "PCA9685 prescale below datasheet minimum");

constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr uint32_t kI2cHz = 100000;
bool s_i2c_initialized = false;
SemaphoreHandle_t s_i2c_mutex = nullptr;
std::atomic<bool> s_outputs_enabled{false};

void disable_pwm_outputs() {
    // PCA9685 /OE is active-low. HIGH is the hardware fail-safe state.
    gpio_set_level(lg::board::kPcaOe, 1);
    s_outputs_enabled.store(false, std::memory_order_release);
}

void enable_pwm_outputs() {
    gpio_set_level(lg::board::kPcaOe, 0);
    s_outputs_enabled.store(true, std::memory_order_release);
}

esp_err_t init_i2c_once() {
    if (s_i2c_initialized) return ESP_OK;

    if (s_i2c_mutex == nullptr) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == nullptr) return ESP_ERR_NO_MEM;
    }

    i2c_config_t cfg{};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = lg::board::kI2cSda;
    cfg.scl_io_num = lg::board::kI2cScl;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = kI2cHz;
    cfg.clk_flags = 0;
    ESP_RETURN_ON_ERROR(i2c_param_config(kI2cPort, &cfg), TAG, "legacy I2C config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(kI2cPort, I2C_MODE_MASTER, 0, 0, 0),
                        TAG, "legacy I2C install");
    s_i2c_initialized = true;
    ESP_LOGI(TAG, "legacy I2C ready SDA=%d SCL=%d 100kHz",
             static_cast<int>(lg::board::kI2cSda), static_cast<int>(lg::board::kI2cScl));
    return ESP_OK;
}

esp_err_t read_register(uint8_t reg, uint8_t& value) {
    if (!s_i2c_initialized || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const esp_err_t err = i2c_master_write_read_device(
        kI2cPort, lg::board::kPcaAddress, &reg, 1, &value, 1, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

} // namespace

void Pca9685Driver::force_disable_outputs() {
    disable_pwm_outputs();
}

esp_err_t Pca9685Driver::enable_outputs() {
    if (!initialized_ || !valid_frame_confirmed_) {
        disable_pwm_outputs();
        return ESP_ERR_INVALID_STATE;
    }
    enable_pwm_outputs();
    return ESP_OK;
}

void Pca9685Driver::disable_outputs() {
    disable_pwm_outputs();
}

bool Pca9685Driver::is_outputs_enabled() {
    return s_outputs_enabled.load(std::memory_order_acquire);
}

esp_err_t Pca9685Driver::write_register(uint8_t reg, uint8_t value) {
    if (!s_i2c_initialized || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const uint8_t packet[] = {reg, value};
    const esp_err_t ret = i2c_master_write_to_device(
        kI2cPort, lg::board::kPcaAddress, packet, sizeof(packet), pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    if (ret != ESP_OK) {
        disable_pwm_outputs();
        zero_confirmed_ = false;
        valid_frame_confirmed_ = false;
    }
    return ret;
}

esp_err_t Pca9685Driver::write_bytes(uint8_t start_reg, const uint8_t* data, size_t len) {
    if (!s_i2c_initialized || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    constexpr size_t kMaxPayload = kChannels * 4;
    if (data == nullptr || len == 0 || len > kMaxPayload) return ESP_ERR_INVALID_ARG;

    uint8_t packet[1 + kMaxPayload];
    packet[0] = start_reg;
    memcpy(packet + 1, data, len);

    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const esp_err_t ret = i2c_master_write_to_device(
        kI2cPort, lg::board::kPcaAddress, packet, len + 1, pdMS_TO_TICKS(100));
    xSemaphoreGive(s_i2c_mutex);
    if (ret != ESP_OK) {
        disable_pwm_outputs();
        zero_confirmed_ = false;
        valid_frame_confirmed_ = false;
    }
    return ret;
}

esp_err_t Pca9685Driver::init() {
    initialized_ = false;
    zero_confirmed_ = false;
    valid_frame_confirmed_ = false;
    // Keep hardware disabled until the controller is configured, verified and a
    // known-safe frame has been written. This prevents boot-time PWM flashes.
    const gpio_config_t oe_conf = {
        .pin_bit_mask = (1ULL << lg::board::kPcaOe),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&oe_conf), TAG, "/OE gpio config");
    disable_pwm_outputs();

    ESP_RETURN_ON_ERROR(init_i2c_once(), TAG, "I2C init");

    uint8_t initial_mode1 = 0;
    const esp_err_t err = read_register(kRegMode1, initial_mode1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 MODE1 read at 0x55 failed: %s", esp_err_to_name(err));
        disable_pwm_outputs();
        return err;
    }
    ESP_LOGI(TAG, "PCA9685 detected at 0x55, MODE1=0x%02X", initial_mode1);

    ESP_LOGI(TAG, "configuring PCA9685 at 0x%02X, target PWM ~1017 Hz", lg::board::kPcaAddress);

    // PRE_SCALE may only be changed while SLEEP=1.
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1Sleep | kMode1AutoInc), TAG, "sleep");
    ESP_RETURN_ON_ERROR(write_register(kRegPrescale, kPrescaleFor1kHz), TAG, "prescale");
    ESP_RETURN_ON_ERROR(write_register(kRegMode2, kMode2OutDrv), TAG, "mode2");
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(2)); // datasheet oscillator wake-up is <= 500 us
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc | kMode1Restart), TAG, "restart");

    // Read back MODE1, MODE2, PRE_SCALE to verify correct hardware state
    uint8_t m1 = 0, m2 = 0, pre = 0;
    const esp_err_t r1 = read_register(kRegMode1, m1);
    const esp_err_t r2 = (r1 == ESP_OK) ? read_register(kRegMode2, m2) : r1;
    const esp_err_t r3 = (r2 == ESP_OK) ? read_register(kRegPrescale, pre) : r2;

    if (r1 != ESP_OK || r2 != ESP_OK || r3 != ESP_OK ||
        (m1 & kMode1Sleep) != 0 ||
        (m1 & kMode1AutoInc) == 0 ||
        (m2 & kMode2OutDrv) == 0 ||
        pre != kPrescaleFor1kHz) {
        disable_pwm_outputs();
        ESP_LOGE(TAG, "PCA9685 verification failed (m1=0x%02X sleep_clr=%d autoinc=%d, m2=0x%02X outdrv=%d, pre=%u exp=%u)",
                 m1, (m1 & kMode1Sleep) == 0, (m1 & kMode1AutoInc) != 0,
                 m2, (m2 & kMode2OutDrv) != 0, pre, kPrescaleFor1kHz);
        return ESP_ERR_INVALID_RESPONSE;
    }

    mode1_ = m1;
    mode2_ = m2;
    prescale_ = pre;

    values_.fill(0);
    const esp_err_t clear_err = commit();
    if (clear_err != ESP_OK) {
        disable_pwm_outputs();
        return clear_err;
    }

    initialized_ = true;
    zero_confirmed_ = true;
    valid_frame_confirmed_ = true;
    ESP_LOGI(TAG, "verified MODE1=0x%02X MODE2=0x%02X PRE_SCALE=%u; initialized to zero",
             m1, m2, pre);
    return ESP_OK;
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
    const esp_err_t err = commit();
    if (err == ESP_OK) {
        zero_confirmed_ = true;
        valid_frame_confirmed_ = true;
    } else {
        zero_confirmed_ = false;
        valid_frame_confirmed_ = false;
    }
    return err;
}

esp_err_t Pca9685Driver::commit() {
    if (!s_i2c_initialized || s_i2c_mutex == nullptr) {
        disable_pwm_outputs();
        zero_confirmed_ = false;
        valid_frame_confirmed_ = false;
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t bytes[kChannels * 4]{};
    for (uint8_t ch = 0; ch < kChannels; ++ch) {
        const uint16_t duty = values_[ch];
        const size_t offset = ch * 4u;
        if (duty == 0) {
            bytes[offset + 3] = 0x10; // full OFF
        } else if (duty >= 4095) {
            bytes[offset + 1] = 0x10; // full ON
        } else {
            bytes[offset + 2] = static_cast<uint8_t>(duty & 0xFF);
            bytes[offset + 3] = static_cast<uint8_t>((duty >> 8) & 0x0F);
        }
    }

    const esp_err_t err = write_bytes(kRegLed0OnL, bytes, sizeof(bytes));
    if (err != ESP_OK) {
        commit_failures_++;
        disable_pwm_outputs();
        zero_confirmed_ = false;
        valid_frame_confirmed_ = false;
        ESP_LOGE(TAG, "PWM commit failed; /OE latched HIGH (%s)", esp_err_to_name(err));
        return err;
    }

    valid_frame_confirmed_ = true;
    bool all_zero = true;
    for (uint8_t ch = 0; ch < kChannels; ++ch) {
        if (values_[ch] != 0) {
            all_zero = false;
            break;
        }
    }
    zero_confirmed_ = all_zero;

    // Note: Do NOT automatically lower /OE here.
    // Physical OE enable is controlled exclusively by OutputController safety state.
    return ESP_OK;
}

PcaDiagnostics Pca9685Driver::diagnostics() const {
    PcaDiagnostics diag;
    diag.initialized = initialized_;
    diag.outputs_enabled = is_outputs_enabled();
    diag.address = lg::board::kPcaAddress;
    diag.i2c_hz = 100000;
    diag.pwm_hz = 1017;
    diag.mode1 = mode1_;
    diag.mode2 = mode2_;
    diag.prescale = prescale_;
    diag.commit_failures = commit_failures_;
    return diag;
}

} // namespace lg
