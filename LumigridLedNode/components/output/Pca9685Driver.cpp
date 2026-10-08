// components/output/Pca9685Driver.cpp
// PCA9685 16-channel, 12-bit PWM controller driver.
// Reference: NXP PCA9685 datasheet Rev.4; ESP-IDF legacy synchronous I2C driver.
#include "Pca9685Driver.hpp"
#include "lg_board_pins.hpp"
#include "BoardOutputSafety.hpp"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

class LegacyEspI2cTransport final : public IPcaTransport {
public:
    LegacyEspI2cTransport() = default;

    esp_err_t init_bus() override {
        if (initialized_) return ESP_OK;

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
        esp_err_t err = i2c_param_config(kI2cPort, &cfg);
        if (err != ESP_OK) return err;
        err = i2c_driver_install(kI2cPort, I2C_MODE_MASTER, 0, 0, 0);
        if (err != ESP_OK) return err;

        initialized_ = true;
        ESP_LOGI(TAG, "legacy I2C ready SDA=%d SCL=%d 100kHz",
                 static_cast<int>(lg::board::kI2cSda), static_cast<int>(lg::board::kI2cScl));
        return ESP_OK;
    }

    esp_err_t deinit_bus() override {
        if (!initialized_) return ESP_OK;
        if (s_i2c_mutex != nullptr) {
            xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        }
        esp_err_t err = i2c_driver_delete(kI2cPort);
        initialized_ = false;
        if (s_i2c_mutex != nullptr) {
            xSemaphoreGive(s_i2c_mutex);
        }
        return err;
    }

    bool is_bus_initialized() const override {
        return initialized_;
    }

    esp_err_t write(uint8_t addr, const uint8_t* data, size_t len, uint32_t timeout_ms) override {
        if (!initialized_ || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
        xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        const esp_err_t ret = i2c_master_write_to_device(
            kI2cPort, addr, data, len, pdMS_TO_TICKS(timeout_ms));
        xSemaphoreGive(s_i2c_mutex);
        return ret;
    }

    esp_err_t write_read(uint8_t addr, const uint8_t* write_data, size_t write_len,
                         uint8_t* read_data, size_t read_len, uint32_t timeout_ms) override {
        if (!initialized_ || s_i2c_mutex == nullptr) return ESP_ERR_INVALID_STATE;
        xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        const esp_err_t ret = i2c_master_write_read_device(
            kI2cPort, addr, write_data, write_len, read_data, read_len, pdMS_TO_TICKS(timeout_ms));
        xSemaphoreGive(s_i2c_mutex);
        return ret;
    }

    bool sda_high() const override {
        return gpio_get_level(lg::board::kI2cSda) != 0;
    }

    bool scl_high() const override {
        return gpio_get_level(lg::board::kI2cScl) != 0;
    }

    esp_err_t recover_bus(uint32_t& pulses_sent, bool& sda_released) override {
        pulses_sent = 0;
        sda_released = false;

        // 1. Assert /OE HIGH
        disable_pwm_outputs();

        // 2. Delete driver if active
        deinit_bus();

        // 3. Configure SDA & SCL as open-drain GPIO
        gpio_config_t conf{};
        conf.pin_bit_mask = (1ULL << lg::board::kI2cSda) | (1ULL << lg::board::kI2cScl);
        conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
        conf.pull_up_en = GPIO_PULLUP_ENABLE;
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&conf);

        // 4. Release both lines HIGH
        gpio_set_level(lg::board::kI2cSda, 1);
        gpio_set_level(lg::board::kI2cScl, 1);
        esp_rom_delay_us(10);

        // 5. Sample lines
        if (gpio_get_level(lg::board::kI2cScl) == 0) {
            ESP_LOGE(TAG, "PCA recovery aborted: SCL line physically held LOW");
            return ESP_FAIL;
        }

        // If SDA is low, send up to 9 pulses on SCL
        if (gpio_get_level(lg::board::kI2cSda) == 0) {
            for (unsigned p = 0; p < 9; ++p) {
                pulses_sent++;
                gpio_set_level(lg::board::kI2cScl, 0);
                esp_rom_delay_us(5);
                gpio_set_level(lg::board::kI2cScl, 1);
                esp_rom_delay_us(5);
                if (gpio_get_level(lg::board::kI2cSda) != 0) {
                    break;
                }
            }
        }

        // 6. Check if SDA released
        if (gpio_get_level(lg::board::kI2cSda) != 0) {
            // Generate I2C STOP condition: SDA low -> SCL high -> SDA high
            gpio_set_level(lg::board::kI2cSda, 0);
            esp_rom_delay_us(5);
            gpio_set_level(lg::board::kI2cScl, 1);
            esp_rom_delay_us(5);
            gpio_set_level(lg::board::kI2cSda, 1);
            esp_rom_delay_us(5);
            sda_released = true;
        } else {
            ESP_LOGE(TAG, "PCA recovery failed: SDA still held LOW after %lu clock pulses", (unsigned long)pulses_sent);
            return ESP_FAIL;
        }

        // 7. Reinstall I2C master driver
        return init_bus();
    }

private:
    bool initialized_ = false;
};

LegacyEspI2cTransport s_default_transport;

} // namespace

Pca9685Driver::Pca9685Driver(IPcaTransport* transport)
    : transport_(transport) {}

IPcaTransport& Pca9685Driver::transport() {
    return transport_ ? *transport_ : s_default_transport;
}

void Pca9685Driver::force_disable_outputs() {
    disable_pwm_outputs();
}

esp_err_t Pca9685Driver::enable_outputs() {
    if (!initialized_ || !valid_frame_confirmed_ || !zero_confirmed_) {
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

void Pca9685Driver::classify_and_record_failure(esp_err_t err) {
    transaction_failures_++;
    consecutive_failures_++;
    last_error_ = err;
    recovery_required_ = true;
    disable_pwm_outputs();
    zero_confirmed_ = false;
    valid_frame_confirmed_ = false;

    last_sda_high_ = transport().sda_high();
    last_scl_high_ = transport().scl_high();

    if (!last_scl_high_) {
        last_fault_kind_ = PcaFaultKind::SclStuckLow;
    } else if (!last_sda_high_) {
        last_fault_kind_ = PcaFaultKind::SdaStuckLow;
    } else if (err == ESP_ERR_TIMEOUT) {
        last_fault_kind_ = PcaFaultKind::Timeout;
    } else if (err == ESP_ERR_NOT_FOUND) {
        last_fault_kind_ = PcaFaultKind::DeviceNotResponding;
    } else {
        last_fault_kind_ = PcaFaultKind::TransportError;
    }
}

esp_err_t Pca9685Driver::read_register(uint8_t reg, uint8_t& value) {
    if (!transport().is_bus_initialized()) return ESP_ERR_INVALID_STATE;
    transactions_++;
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = transport().write_read(
        lg::board::kPcaAddress, &reg, 1, &value, 1, kPcaTransactionTimeoutMs);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    last_transaction_us_ = us;
    if (us > max_transaction_us_) max_transaction_us_ = us;
    total_transaction_us_ += us;

    if (err != ESP_OK) {
        classify_and_record_failure(err);
    } else {
        consecutive_failures_ = 0;
        last_error_ = ESP_OK;
    }
    return err;
}

esp_err_t Pca9685Driver::write_register(uint8_t reg, uint8_t value) {
    if (!transport().is_bus_initialized()) return ESP_ERR_INVALID_STATE;
    transactions_++;
    const uint8_t packet[] = {reg, value};
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t ret = transport().write(
        lg::board::kPcaAddress, packet, sizeof(packet), kPcaTransactionTimeoutMs);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    last_transaction_us_ = us;
    if (us > max_transaction_us_) max_transaction_us_ = us;
    total_transaction_us_ += us;

    if (ret != ESP_OK) {
        classify_and_record_failure(ret);
    } else {
        consecutive_failures_ = 0;
        last_error_ = ESP_OK;
    }
    return ret;
}

esp_err_t Pca9685Driver::write_bytes(uint8_t start_reg, const uint8_t* data, size_t len) {
    if (!transport().is_bus_initialized()) return ESP_ERR_INVALID_STATE;
    constexpr size_t kMaxPayload = kChannels * 4;
    if (data == nullptr || len == 0 || len > kMaxPayload) return ESP_ERR_INVALID_ARG;

    transactions_++;
    uint8_t packet[1 + kMaxPayload];
    packet[0] = start_reg;
    memcpy(packet + 1, data, len);

    const int64_t t0 = esp_timer_get_time();
    const esp_err_t ret = transport().write(
        lg::board::kPcaAddress, packet, len + 1, kPcaTransactionTimeoutMs);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    last_transaction_us_ = us;
    if (us > max_transaction_us_) max_transaction_us_ = us;
    total_transaction_us_ += us;

    if (ret != ESP_OK) {
        classify_and_record_failure(ret);
    } else {
        consecutive_failures_ = 0;
        last_error_ = ESP_OK;
    }
    return ret;
}

esp_err_t Pca9685Driver::initialise_bus() {
    return transport().init_bus();
}

esp_err_t Pca9685Driver::initialise_chip() {
    uint8_t initial_mode1 = 0;
    esp_err_t err = read_register(kRegMode1, initial_mode1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 MODE1 read at 0x%02X failed: %s",
                 lg::board::kPcaAddress, esp_err_to_name(err));
        return err;
    }

    // PRE_SCALE may only be changed while SLEEP=1.
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1Sleep | kMode1AutoInc), TAG, "sleep");
    ESP_RETURN_ON_ERROR(write_register(kRegPrescale, kPrescaleFor1kHz), TAG, "prescale");
    ESP_RETURN_ON_ERROR(write_register(kRegMode2, kMode2OutDrv), TAG, "mode2");
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(2)); // datasheet oscillator wake-up is <= 500 us
    ESP_RETURN_ON_ERROR(write_register(kRegMode1, kMode1AutoInc | kMode1Restart), TAG, "restart");
    return ESP_OK;
}

esp_err_t Pca9685Driver::verify_chip() {
    uint8_t m1 = 0, m2 = 0, pre = 0;
    const esp_err_t r1 = read_register(kRegMode1, m1);
    const esp_err_t r2 = (r1 == ESP_OK) ? read_register(kRegMode2, m2) : r1;
    const esp_err_t r3 = (r2 == ESP_OK) ? read_register(kRegPrescale, pre) : r2;

    if (r1 != ESP_OK || r2 != ESP_OK || r3 != ESP_OK ||
        (m1 & kMode1Sleep) != 0 ||
        (m1 & kMode1AutoInc) == 0 ||
        (m2 & kMode2OutDrv) == 0 ||
        pre != kPrescaleFor1kHz) {
        ESP_LOGE(TAG, "PCA9685 verification failed (m1=0x%02X sleep_clr=%d autoinc=%d, m2=0x%02X outdrv=%d, pre=%u exp=%u)",
                 m1, (m1 & kMode1Sleep) == 0, (m1 & kMode1AutoInc) != 0,
                 m2, (m2 & kMode2OutDrv) != 0, pre, kPrescaleFor1kHz);
        return ESP_ERR_INVALID_RESPONSE;
    }

    mode1_ = m1;
    mode2_ = m2;
    prescale_ = pre;
    return ESP_OK;
}

esp_err_t Pca9685Driver::write_zero_frame() {
    values_.fill(0);
    return commit();
}

esp_err_t Pca9685Driver::init() {
    initialized_ = false;
    zero_confirmed_ = false;
    valid_frame_confirmed_ = false;
    recovery_required_ = false;
    init_core_ = xPortGetCoreID();

    ESP_LOGI(TAG, "PCA_INIT core=%d port=%d sda=%d scl=%d addr=0x%02X hz=%lu timeout=%lums",
             init_core_, kI2cPort,
             static_cast<int>(lg::board::kI2cSda),
             static_cast<int>(lg::board::kI2cScl),
             lg::board::kPcaAddress,
             (unsigned long)kI2cHz,
             (unsigned long)kPcaTransactionTimeoutMs);

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

    ESP_RETURN_ON_ERROR(initialise_bus(), TAG, "I2C init");
    ESP_RETURN_ON_ERROR(initialise_chip(), TAG, "chip init");
    ESP_RETURN_ON_ERROR(verify_chip(), TAG, "chip verify");
    ESP_RETURN_ON_ERROR(write_zero_frame(), TAG, "zero frame");

    initialized_ = true;
    zero_confirmed_ = true;
    valid_frame_confirmed_ = true;
    board::record_first_pwm_zero();
    recovery_required_ = false;
    last_fault_kind_ = PcaFaultKind::None;
    ESP_LOGI(TAG, "verified MODE1=0x%02X MODE2=0x%02X PRE_SCALE=%u; initialized to zero",
             mode1_, mode2_, prescale_);
    return ESP_OK;
}

esp_err_t Pca9685Driver::recover() {
    disable_pwm_outputs();
    recovery_attempts_++;

    ESP_LOGW(TAG, "Starting PCA bus recovery (attempt #%lu)...", (unsigned long)recovery_attempts_);
    bus_clear_attempts_++;

    uint32_t pulses = 0;
    bool sda_rel = false;
    esp_err_t bus_res = transport().recover_bus(pulses, sda_rel);
    if (bus_res != ESP_OK) {
        bus_clear_failures_++;
        recovery_failures_++;
        last_fault_kind_ = PcaFaultKind::RecoveryFailed;
        disable_pwm_outputs();
        return bus_res;
    }
    bus_clear_successes_++;
    driver_reinstalls_++;

    // Verify chip state; if verification fails, attempt full re-initialization
    if (verify_chip() != ESP_OK) {
        esp_err_t chip_err = initialise_chip();
        if (chip_err != ESP_OK || verify_chip() != ESP_OK) {
            recovery_failures_++;
            last_fault_kind_ = PcaFaultKind::RegisterVerificationFailed;
            disable_pwm_outputs();
            return ESP_FAIL;
        }
    }

    esp_err_t z_err = write_zero_frame();
    if (z_err != ESP_OK) {
        recovery_failures_++;
        last_fault_kind_ = PcaFaultKind::RecoveryFailed;
        disable_pwm_outputs();
        return z_err;
    }

    // Recovery succeeded! /OE remains HIGH. OutputController will enable after next valid frame.
    disable_pwm_outputs();
    recovery_successes_++;
    recovery_required_ = false;
    initialized_ = true;
    zero_confirmed_ = true;
    valid_frame_confirmed_ = true;
    board::record_first_pwm_zero();
    last_fault_kind_ = PcaFaultKind::None;
    consecutive_failures_ = 0;
    ESP_LOGI(TAG, "PCA recovery succeeded (pulses=%lu, zero_frame=ok, /OE latched HIGH)", (unsigned long)pulses);
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
    if (!transport().is_bus_initialized()) {
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
    diag.healthy = (last_fault_kind_ == PcaFaultKind::None && !recovery_required_);
    diag.recovery_required = recovery_required_;
    diag.address = lg::board::kPcaAddress;
    diag.i2c_hz = kI2cHz;
    diag.pwm_hz = 1017;
    diag.mode1 = mode1_;
    diag.mode2 = mode2_;
    diag.prescale = prescale_;
    diag.commit_failures = commit_failures_;
    diag.transactions = transactions_;
    diag.transaction_failures = transaction_failures_;
    diag.consecutive_failures = consecutive_failures_;
    diag.last_error = last_error_;
    diag.init_core = init_core_;

    diag.last_fault_kind = last_fault_kind_;
    diag.last_sda_high = last_sda_high_;
    diag.last_scl_high = last_scl_high_;

    diag.last_transaction_us = last_transaction_us_;
    diag.max_transaction_us = max_transaction_us_;
    diag.total_transaction_us = total_transaction_us_;

    diag.recovery_attempts = recovery_attempts_;
    diag.recovery_successes = recovery_successes_;
    diag.recovery_failures = recovery_failures_;

    diag.bus_clear_attempts = bus_clear_attempts_;
    diag.bus_clear_successes = bus_clear_successes_;
    diag.bus_clear_failures = bus_clear_failures_;

    diag.driver_reinstalls = driver_reinstalls_;
    return diag;
}

} // namespace lg
