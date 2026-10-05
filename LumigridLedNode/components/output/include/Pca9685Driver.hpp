#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"
#include "SignageTypes.hpp"

namespace lg {

struct PcaDiagnostics {
    bool initialized = false;
    bool outputs_enabled = false;

    uint8_t address = 0;
    uint32_t i2c_hz = 100000;
    uint32_t pwm_hz = 1017;

    uint8_t mode1 = 0;
    uint8_t mode2 = 0;
    uint8_t prescale = 0;

    uint32_t commit_failures = 0;
};

class Pca9685Driver {
public:
    // Note: The PCA9685 IC supports 16 channels, but only the first 8 are broken out 
    // on the Lumigrid PCB. Therefore, we limit the driver to 8 channels.
    static constexpr size_t kChannels = MAX_PWM_CHANNELS;

    esp_err_t init();
    esp_err_t set(uint8_t channel, uint16_t duty);
    esp_err_t set_all(uint16_t duty);
    esp_err_t clear();
    esp_err_t commit();

    // Fast fail-safe: immediately asserts /OE HIGH without requiring an I2C transaction
    static void force_disable_outputs();

    // Output enable control: owned by OutputController safety state
    static void enable_outputs();
    static void disable_outputs();
    static bool is_outputs_enabled();

    PcaDiagnostics diagnostics() const;

    const std::array<uint16_t, kChannels>& values() const {
        return values_;
    }

private:
    esp_err_t write_register(uint8_t reg, uint8_t value);
    esp_err_t write_bytes(uint8_t start_reg, const uint8_t* data, size_t len);

    std::array<uint16_t, kChannels> values_{};
    bool initialized_ = false;
    uint8_t mode1_ = 0;
    uint8_t mode2_ = 0;
    uint8_t prescale_ = 0;
    uint32_t commit_failures_ = 0;
};

} // namespace lg
