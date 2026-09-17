// components/output/Pca9685Driver.cpp
//
// PCA9685 16-channel, 12-bit PWM controller driver.
// Reference: NXP PCA9685 datasheet Rev.4 (2015-01-08).
//
// Init sequence follows §7.3.1 "Software reset" and §7.3.5 "Restart mode".
// Register map: §7.4 (p.10-16).
// All I2C transactions use ESP-IDF i2c_master (v5.x new driver API).
//
#include "Pca9685Driver.hpp"
#include "lg_board_pins.hpp"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace lg {
namespace {

static const char* TAG = "pca9685";

// ── Register addresses (PCA9685 datasheet §7.4) ───────────────────────────────
constexpr uint8_t kRegMode1    = 0x00; // Mode register 1
constexpr uint8_t kRegMode2    = 0x01; // Mode register 2
constexpr uint8_t kRegPrescale = 0xFE; // PWM frequency prescale
constexpr uint8_t kRegLed0OnL  = 0x06; // Start of LED0 ON/OFF registers (auto-increments)

// ── MODE1 register bits (§7.4.1) ─────────────────────────────────────────────
constexpr uint8_t kMode1Restart  = (1u << 7); // Bit 7: RESTART – restarts PWM channels
constexpr uint8_t kMode1ExtClk   = (1u << 6); // Bit 6: EXTCLK  – use external clock
constexpr uint8_t kMode1AutoInc  = (1u << 5); // Bit 5: AI      – register auto-increment
constexpr uint8_t kMode1Sleep    = (1u << 4); // Bit 4: SLEEP   – low-power mode
constexpr uint8_t kMode1Sub1     = (1u << 3); // Bit 3: SUB1    – I2C sub-address 1
constexpr uint8_t kMode1Sub2     = (1u << 2); // Bit 2: SUB2    – I2C sub-address 2
constexpr uint8_t kMode1Sub3     = (1u << 1); // Bit 1: SUB3    – I2C sub-address 3
constexpr uint8_t kMode1AllCall  = (1u << 0); // Bit 0: ALLCALL – respond to 0x70 broadcast

// ── MODE2 register bits (§7.4.2) ─────────────────────────────────────────────
constexpr uint8_t kMode2OutDrv   = (1u << 2); // Bit 2: OUTDRV  – totem-pole (vs open-drain)
constexpr uint8_t kMode2Invrt    = (1u << 4); // Bit 4: INVRT   – invert output logic

// ── Prescale value for target frequency ──────────────────────────────────────
// Formula: prescale = round(f_osc / (4096 * f_pwm)) - 1
// With f_osc = 25 MHz (nominal), f_pwm = 1000 Hz:
//   prescale = round(25_000_000 / (4096 * 1000)) - 1 = round(6.1) - 1 = 5
// Resulting frequency: 25_000_000 / (4096 * (5+1)) ≈ 1017 Hz
//
// Datasheet §7.3.5: Valid prescale range is [3, 255].
// Prescale 3 → ~1526 Hz (maximum), 255 → ~24 Hz (minimum).
constexpr uint8_t kPrescaleFor1kHz = 5;
constexpr uint8_t kPrescaleMin     = 3;   // Datasheet minimum
constexpr uint8_t kPrescaleMax     = 255; // Datasheet maximum
static_assert(kPrescaleFor1kHz >= kPrescaleMin && kPrescaleFor1kHz <= kPrescaleMax,
              "PCA9685 prescale value out of datasheet-allowed range [3, 255]");

// ── Expected MODE1 value after successful restart ─────────────────────────────
// After the RESTART sequence, the chip should show:
//   RESTART=0 (auto-cleared), EXTCLK=0, AI=1, SLEEP=0, SUB1=0, SUB2=0, SUB3=0, ALLCALL=0
// We write AI=1, clear ALLCALL; all others default to 0.
constexpr uint8_t kMode1ExpectedAfterRestart = kMode1AutoInc; // 0x20

// ── Module-level I2C handles (initialised at most once per power cycle) ───────
i2c_master_bus_handle_t s_bus           = nullptr;
i2c_master_dev_handle_t s_dev           = nullptr;
SemaphoreHandle_t       s_i2c_mutex     = nullptr;
uint8_t                 s_detected_addr = lg::board::kPcaAddress;

esp_err_t init_i2c_once() {
    if (s_bus != nullptr) return ESP_OK;

    if (s_i2c_mutex == nullptr) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == nullptr) return ESP_ERR_NO_MEM;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_0,
        .sda_io_num          = lg::board::kI2cSda,
        .scl_io_num          = lg::board::kI2cScl,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .intr_priority       = 0,
        .trans_queue_depth   = 4,
        .flags               = { .enable_internal_pullup = true },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus create");

    // Probe configured address (default 0x55) or standard unjumpered PCA9685 address (0x40)
    s_detected_addr = lg::board::kPcaAddress;
    if (i2c_master_probe(s_bus, s_detected_addr, 50) == ESP_OK) {
        ESP_LOGI(TAG, "PCA9685 ACKed at configured address 0x%02X", s_detected_addr);
    } else if (i2c_master_probe(s_bus, 0x40, 50) == ESP_OK) {
        s_detected_addr = 0x40;
        ESP_LOGI(TAG, "PCA9685 detected at standard address 0x40 (fallback from 0x%02X)", lg::board::kPcaAddress);
    } else {
        // Fallback scan across common PCA9685 addresses [0x41 .. 0x5F]
        for (uint8_t a = 0x41; a <= 0x5F; ++a) {
            if (a == s_detected_addr) continue;
            if (i2c_master_probe(s_bus, a, 20) == ESP_OK) {
                s_detected_addr = a;
                ESP_LOGI(TAG, "PCA9685 detected via I2C bus scan at 0x%02X", s_detected_addr);
                break;
            }
        }
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = s_detected_addr,
        .scl_speed_hz    = 100000, // 100 kHz Standard Mode for reliable edges with internal pullups
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}

} // anonymous namespace

// ── Low-level I2C helpers ─────────────────────────────────────────────────────

esp_err_t Pca9685Driver::write_register(uint8_t reg, uint8_t value) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    const uint8_t packet[] = { reg, value };
    esp_err_t ret = i2c_master_transmit(s_dev, packet, sizeof(packet), /*timeout_ms=*/50);
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}

esp_err_t Pca9685Driver::write_bytes(uint8_t start_reg, const uint8_t* data, size_t len) {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;

    // Dynamically sized buffer: register address byte + payload.
    // Maximum possible payload is kChannels * 4 bytes (all 16 LED registers).
    constexpr size_t kMaxPayload = kChannels * 4;
    if (len > kMaxPayload) return ESP_ERR_INVALID_SIZE;

    // Stack-allocate only what is needed (len + 1 for the register prefix byte).
    uint8_t packet[1 + kMaxPayload];
    packet[0] = start_reg;
    memcpy(packet + 1, data, len);

    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t ret = i2c_master_transmit(s_dev, packet, len + 1, /*timeout_ms=*/50);
    xSemaphoreGive(s_i2c_mutex);
    return ret;
}

// ── Initialisation ────────────────────────────────────────────────────────────

esp_err_t Pca9685Driver::init() {
    // ── /OE pin: active-low output-enable. Pull LOW to enable PWM outputs. ──────
    // Pull-down keeps outputs enabled even if the GPIO floats briefly during boot.
    gpio_config_t oe_conf = {
        .pin_bit_mask  = (1ULL << lg::board::kPcaOe),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&oe_conf),                       TAG, "/OE gpio config");
    ESP_RETURN_ON_ERROR(gpio_set_level(lg::board::kPcaOe, 0),       TAG, "/OE drive low");

    ESP_RETURN_ON_ERROR(init_i2c_once(), TAG, "I2C init");

    ESP_LOGI(TAG, "Initialising PCA9685 at 0x%02X, target PWM ~1017 Hz (prescale=%u)",
             s_detected_addr, kPrescaleFor1kHz);

    // ── Configuration sequence (datasheet §7.3.1 & §7.3.5) ───────────────────
    //
    // Step 1: Assert SLEEP to disable oscillator before changing prescale.
    //   AI=1 (auto-increment), SLEEP=1, ALLCALL=0 (disable 0x70 general call).
    esp_err_t err = write_register(kRegMode1, kMode1Sleep | kMode1AutoInc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 not responding on I2C — is the chip present? (%s)", esp_err_to_name(err));
        return err;
    }

    // Step 2: Write prescale (only allowed while SLEEP=1).
    ESP_RETURN_ON_ERROR(write_register(kRegPrescale, kPrescaleFor1kHz), TAG, "prescale write");

    // Step 3: Configure outputs — totem-pole drive (OUTDRV=1), not inverted.
    //   INVRT=0: logic high → LED on (common-cathode).
    //   OUTDRV=1: push-pull (totem-pole), not open-drain.
    ESP_RETURN_ON_ERROR(write_register(kRegMode2, kMode2OutDrv), TAG, "MODE2 write");

    // Step 4: Wake oscillator — clear SLEEP (bit 4=0), enable Auto-Increment (bit 5=1), clear ALLCALL.
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc), TAG, "oscillator wake");

    // Step 5: Wait ≥500 µs for oscillator to stabilise (§7.3.5, t_osc = 500 µs max).
    vTaskDelay(pdMS_TO_TICKS(2)); // 2 ms >> 500 µs minimum

    // Step 6: Explicitly lock in Auto-Increment (AI=1) and confirm SLEEP=0.
    // Note: Writing kMode1Restart on a freshly powered chip resets MODE1 to 0x00
    // on various PCA9685 revisions. Setting kMode1AutoInc guarantees bit 5 (AI) is set.
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc), TAG, "MODE1 AI set");

    // ── Verify communication with MODE1 readback ──────────────────────────────
    {
        uint8_t reg_addr = kRegMode1;
        uint8_t mode1_val = 0;
        xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        err = i2c_master_transmit_receive(s_dev, &reg_addr, 1, &mode1_val, 1, /*timeout_ms=*/50);
        xSemaphoreGive(s_i2c_mutex);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MODE1 readback failed (%s) — proceeding but communication uncertain",
                     esp_err_to_name(err));
        } else if ((mode1_val & kMode1Sleep) != 0) {
            ESP_LOGE(TAG, "PCA9685 still in SLEEP mode (MODE1: 0x%02X)", mode1_val);
            return ESP_FAIL;
        } else {
            if ((mode1_val & kMode1AutoInc) == 0) {
                // Ensure Auto-Increment is asserted
                write_register(kRegMode1, kMode1AutoInc);
            }
            ESP_LOGI(TAG, "PCA9685 verified at 0x%02X (MODE1: 0x%02X, oscillator active)",
                     s_detected_addr, mode1_val);
        }
    }

    // ── Zero all PWM channels ─────────────────────────────────────────────────
    ESP_LOGD(TAG, "Zeroing all PWM channels...");
    return clear();
}

// ── Per-channel staging ───────────────────────────────────────────────────────

esp_err_t Pca9685Driver::set(uint8_t channel, uint16_t duty) {
    if (channel >= kChannels) return ESP_ERR_INVALID_ARG;
    if (duty > 4095)          return ESP_ERR_INVALID_ARG;
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

// ── Commit staged values to hardware ─────────────────────────────────────────
//
// Each LED channel occupies 4 bytes (ON_L, ON_H, OFF_L, OFF_H), §7.4.6.
//
// To produce a duty cycle D (0–4095):
//   LEDn_ON_L  = 0x00   (PWM pulse starts at count 0)
//   LEDn_ON_H  = 0x00
//   LEDn_OFF_L = D & 0xFF
//   LEDn_OFF_H = (D >> 8) & 0x0F
//
// Full-on / full-off per §7.3.3:
//   duty == 0:    LEDn_OFF_H |= 0x10 (bit 4 = full-off, zero glitch)
//   duty == 4095: LEDn_ON_H  |= 0x10 (bit 4 = full-on)
//
// With AI=1, writing from register kRegLed0OnL (0x06) sequentially fills
// all kChannels * 4 registers in one burst, saving kChannels-1 I2C transactions.
esp_err_t Pca9685Driver::commit() {
    if (s_dev == nullptr || s_i2c_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t bytes[kChannels * 4];

    for (uint8_t ch = 0; ch < kChannels; ++ch) {
        const uint16_t duty   = values_[ch];
        const size_t   offset = ch * 4u;
        if (duty == 0) {
            bytes[offset + 0] = 0x00;
            bytes[offset + 1] = 0x00;
            bytes[offset + 2] = 0x00;
            bytes[offset + 3] = 0x10; // Full OFF bit
        } else if (duty >= 4095) {
            bytes[offset + 0] = 0x00;
            bytes[offset + 1] = 0x10; // Full ON bit
            bytes[offset + 2] = 0x00;
            bytes[offset + 3] = 0x00;
        } else {
            bytes[offset + 0] = 0x00;
            bytes[offset + 1] = 0x00;
            bytes[offset + 2] = static_cast<uint8_t>(duty & 0xFF);
            bytes[offset + 3] = static_cast<uint8_t>((duty >> 8) & 0x0F);
        }
    }

    return write_bytes(kRegLed0OnL, bytes, sizeof(bytes));
}

} // namespace lg
