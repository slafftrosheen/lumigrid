#pragma once
#include <cstddef>
#include "driver/gpio.h"

namespace lg::board {

// PCA9685 hardware control. /OE is active-low and must be held HIGH until the
// PWM driver has completed initialization and written a known-safe frame.
constexpr gpio_num_t kPcaOe = GPIO_NUM_25;
constexpr gpio_num_t kI2cSda = GPIO_NUM_21;
constexpr gpio_num_t kI2cScl = GPIO_NUM_22;
constexpr uint8_t kPcaAddress = 0x55;

// Addressable outputs: one RMT TX channel per physical line.
constexpr gpio_num_t kAledPins[] = {
    GPIO_NUM_16,
    GPIO_NUM_4,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_23,
    GPIO_NUM_26,
    GPIO_NUM_27
};

constexpr uint8_t kAledCount = 8;
constexpr uint8_t kPwmCount = 8;

static_assert(std::size(kAledPins) == kAledCount,
              "Addressable output pin map must match advertised lane count");
static_assert(kPcaAddress >= 0x40 && kPcaAddress <= 0x7F,
              "Configured PCA9685 address is outside the valid 7-bit device range");
static_assert(kPcaOe != kI2cSda && kPcaOe != kI2cScl,
              "PCA9685 /OE must not alias the I2C bus");

} // namespace lg::board
