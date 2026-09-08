#pragma once
#include "AddressableLeds.hpp"
#include "Pca9685.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

/// Controls all output hardware (addressable LEDs + PCA9685 PWM).
///
/// Thread-safety: All public methods are protected by an internal mutex.
/// If you need to call multiple pixel/pwm methods atomically (e.g. set
/// several lanes then commit), bracket with lock() / unlock().
class OutputController {
public:
    esp_err_t init();
    esp_err_t blackout();
    esp_err_t commit();

    AddressableLeds& pixels() { return pixels_; }
    Pca9685& pwm() { return pwm_; }

    void enable();
    void disable();
    bool enabled() const { return enabled_; }

    /// Acquire the output mutex. Use when performing multi-step
    /// pixel/pwm operations that must be atomic.
    void lock()   { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    void unlock() { xSemaphoreGiveRecursive(mutex_); }

private:
    AddressableLeds pixels_;
    Pca9685 pwm_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool enabled_ = false;
};

} // namespace lg
