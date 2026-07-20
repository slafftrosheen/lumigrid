#pragma once
#include "driver/gpio.h"

namespace lg::board {

constexpr gpio_num_t kPcaOe = GPIO_NUM_25;
constexpr gpio_num_t kI2cSda = GPIO_NUM_21;
constexpr gpio_num_t kI2cScl = GPIO_NUM_22;
constexpr uint8_t kPcaAddress = 0x55;

// Addressable outputs: one RMT channel per line during factory validation.
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

} // namespace lg::board
