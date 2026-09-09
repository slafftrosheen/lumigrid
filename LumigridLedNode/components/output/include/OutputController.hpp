#pragma once
#include "AddressableDriver.hpp"
#include "Pca9685Driver.hpp"
#include "SignageTypes.hpp"

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
    
    // Reconfigures hardware driver layers based on logical configuration
    esp_err_t reconfigure(const DeviceConfig& cfg);
    
    esp_err_t blackout();
    esp_err_t commit();

    AddressableDriver& pixels() { return pixels_; }
    Pca9685Driver& pwm() { return pwm_; }

    void enable();
    void disable();
    bool enabled() const { return enabled_; }

    /// Acquire the output mutex. Use when performing multi-step
    /// pixel/pwm operations that must be atomic.
    void lock()   { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    void unlock() { xSemaphoreGiveRecursive(mutex_); }

private:
    AddressableDriver pixels_;
    Pca9685Driver pwm_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool enabled_ = false;
};

} // namespace lg
