// components/output/Pca9685Driver.cpp
// PCA9685 16-channel, 12-bit PWM controller driver.
// Reference: NXP PCA9685 datasheet Rev.4; ESP-IDF 5.x i2c_master API.
#include "Pca9685Driver.hpp"
#include "lg_board_pins.hpp"

#include "driver/i2c_master.h"
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
constexpr uint8_t kMode2OutDrv  = (1u << 2);
constexpr uint8_t kPrescaleFor1kHz = 5;

static_assert(kPrescaleFor1kHz >= 3, "PCA9685 prescale below datasheet minimum");

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_dev = nullptr;
SemaphoreHandle_t s_i2c_mutex = nullptr;
uint8_t s_detected_addr = lg::board::kPcaAddress;
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
    if (s_bus != nullptr && s_dev != nullptr) return ESP_OK;

    if (s_i2c_mutex == nullptr) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == nullptr) return ESP_ERR_NO_MEM;
    }

    if (s_bus == nullptr) {
        const i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = lg::board::kI2cSda,
            .scl_io_num = lg::board::kI2cScl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            // IDF 5.2.1 enables async transfers for any nonzero queue depth.
            // Register readback and writes use stack buffers and require completion
            // before returning and releasing s_i2c_mutex.
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = true },
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus create");
        ESP_LOGI(TAG, "I2C bus ready at 100 kHz; external SDA/SCL pull-ups remain recommended");
    }

    // Do not scan arbitrary addresses: another I2C peripheral can ACK and be
    // mistaken for a PCA9685. Only accept the board-configured address or the
    // PCA9685 power-on default address.
    s_detected_addr = lg::board::kPcaAddress;
    if (i2c_master_probe(s_bus, s_detected_addr, 50) != ESP_OK) {
        constexpr uint8_t kDefaultAddress = 0x40;
        if (s_detected_addr == kDefaultAddress ||
            i2c_master_probe(s_bus, kDefaultAddress, 50) != ESP_OK) {
            ESP_LOGE(TAG, "PCA9685 not found at configured 0x%02X or default 0x40",
                     lg::board::kPcaAddress);
            return ESP_ERR_NOT_FOUND;
        }
        s_detected_addr = kDefaultAddress;
        ESP_LOGW(TAG, "PCA9685 using default address 0x40 instead of configured 0x%02X",
                 lg::board::kPcaAddress);
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_detected_addr,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "add PCA9685");
    return ESP_OK;
}

} // namespace

void Pca9685Driver::force_disable_outputs() {
    disable_pwm_outputs();
}

void Pca9685Driver::enable_outputs() {
    enable_pwm_outputs();
}

void Pca9685Driver::disable_outputs() {
    disable_pwm_outputs();
}

bool Pca9685Driver::is_outputs_enabled() {
    return s_outputs_enabled.load(std::memory_order_acquire);
}

esp_err_t Pca9685Driver::write_register(uint8_t reg, uint8_t value) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const uint8_t packet[] = {reg, value};
    const esp_err_t ret = i2c_master_transmit(s_dev, packet, sizeof(packet), 50);
    xSemaphoreGive(s_i2c_mutex);
    if (ret != ESP_OK) disable_pwm_outputs();
    return ret;
}

esp_err_t Pca9685Driver::write_bytes(uint8_t start_reg, const uint8_t* data, size_t len) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    constexpr size_t kMaxPayload = kChannels * 4;
    if (data == nullptr || len == 0 || len > kMaxPayload) return ESP_ERR_INVALID_ARG;

    uint8_t packet[1 + kMaxPayload];
    packet[0] = start_reg;
    memcpy(packet + 1, data, len);

    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const esp_err_t ret = i2c_master_transmit(s_dev, packet, len + 1, 50);
    xSemaphoreGive(s_i2c_mutex);
    if (ret != ESP_OK) disable_pwm_outputs();
    return ret;
}

esp_err_t Pca9685Driver::init() {
    initialized_ = false;
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
    ESP_LOGI(TAG, "Initialising PCA9685 at 0x%02X, target PWM ~1017 Hz", s_detected_addr);

    // PRE_SCALE may only be changed while SLEEP=1.
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1Sleep | kMode1AutoInc), TAG, "sleep");
    ESP_RETURN_ON_ERROR(write_register(kRegPrescale, kPrescaleFor1kHz), TAG, "prescale");
    ESP_RETURN_ON_ERROR(write_register(kRegMode2, kMode2OutDrv), TAG, "mode2");
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(2)); // datasheet oscillator wake-up is <= 500 us

    // Read back MODE1, MODE2, PRE_SCALE to verify correct hardware state
    uint8_t reg_m1 = kRegMode1;
    uint8_t reg_m2 = kRegMode2;
    uint8_t reg_pre = kRegPrescale;
    uint8_t m1 = 0, m2 = 0, pre = 0;

    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t r1 = i2c_master_transmit_receive(s_dev, &reg_m1, 1, &m1, 1, 50);
    esp_err_t r2 = (r1 == ESP_OK) ? i2c_master_transmit_receive(s_dev, &reg_m2, 1, &m2, 1, 50) : r1;
    esp_err_t r3 = (r2 == ESP_OK) ? i2c_master_transmit_receive(s_dev, &reg_pre, 1, &pre, 1, 50) : r2;
    xSemaphoreGive(s_i2c_mutex);

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
    ESP_LOGI(TAG, "PCA9685 verified (MODE1=0x%02X, MODE2=0x%02X, PRE_SCALE=%u) and initialized to zero",
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
    return commit();
}

esp_err_t Pca9685Driver::commit() {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) {
        disable_pwm_outputs();
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
        ESP_LOGE(TAG, "PWM commit failed; /OE latched HIGH (%s)", esp_err_to_name(err));
        return err;
    }

    // Note: Do NOT automatically lower /OE here.
    // Physical OE enable is controlled exclusively by OutputController safety state.
    return ESP_OK;
}

PcaDiagnostics Pca9685Driver::diagnostics() const {
    PcaDiagnostics diag;
    diag.initialized = initialized_;
    diag.outputs_enabled = is_outputs_enabled();
    diag.address = s_detected_addr;
    diag.i2c_hz = 100000;
    diag.pwm_hz = 1017;
    diag.mode1 = mode1_;
    diag.mode2 = mode2_;
    diag.prescale = prescale_;
    diag.commit_failures = commit_failures_;
    return diag;
}

} // namespace lg
