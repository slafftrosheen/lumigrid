#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"

namespace lg {

class Pca9685 {
public:
    // Note: The PCA9685 IC supports 16 channels, but only the first 8 are broken out 
    // on the Lumigrid PCB. Therefore, we limit the driver to 8 channels.
    static constexpr size_t kChannels = 8;

    esp_err_t init();
    esp_err_t set(uint8_t channel, uint16_t duty);
    esp_err_t set_all(uint16_t duty);
    esp_err_t clear();
    esp_err_t commit();

    const std::array<uint16_t, kChannels>& values() const {
        return values_;
    }

private:
    esp_err_t write_register(uint8_t reg, uint8_t value);
    esp_err_t write_bytes(uint8_t start_reg, const uint8_t* data, size_t len);

    std::array<uint16_t, kChannels> values_{};
};

} // namespace lg
