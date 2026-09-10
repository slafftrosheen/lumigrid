#pragma once
#include <atomic>
#include "AddressableDriver.hpp"
#include "Pca9685Driver.hpp"
#include "SignageTypes.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

/// Controls all output hardware (addressable LEDs + PCA9685 PWM).
///
/// Thread-safety: All physical output commits MUST pass the try_commit() safety gate.
/// Mutex synchronization protects driver access.
class OutputController {
public:
    OutputController() {
        mutex_ = xSemaphoreCreateRecursiveMutex();
    }
    ~OutputController() {
        if (mutex_) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
    }

    esp_err_t init();
    
    // Reconfigures hardware driver layers based on logical configuration
    esp_err_t reconfigure(const DeviceConfig& cfg);
    
    esp_err_t blackout();

    /// Unified physical output safety gate
    esp_err_t try_commit();

    /// Commit while caller already holds lock()
    esp_err_t try_commit_locked();

    void set_output_enabled(bool enabled);
    bool output_enabled() const { return output_enabled_.load(std::memory_order_acquire); }

    void latch_estop();
    void clear_estop();
    bool is_estop_latched() const { return estop_latched_.load(std::memory_order_acquire); }
    uint32_t failed_commit_count() const { return failed_commit_count_.load(std::memory_order_relaxed); }
    esp_err_t last_commit_error() const { return last_commit_error_.load(std::memory_order_acquire); }
    uint32_t last_commit_time_ms() const { return last_commit_time_ms_.load(std::memory_order_acquire); }

    AddressableDriver& pixels() { return pixels_; }
    Pca9685Driver& pwm() { return pwm_; }

    /// Acquire the output mutex. Use when performing multi-step
    /// pixel/pwm operations that must be atomic.
    void lock()   { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    void unlock() { xSemaphoreGiveRecursive(mutex_); }

private:
    esp_err_t blackout_locked();

    AddressableDriver pixels_;
    Pca9685Driver pwm_;
    SemaphoreHandle_t mutex_ = nullptr;

    std::atomic<bool> output_enabled_{true};
    std::atomic<bool> estop_latched_{false};
    std::atomic<uint32_t> failed_commit_count_{0};
    std::atomic<esp_err_t> last_commit_error_{ESP_OK};
    std::atomic<uint32_t> last_commit_time_ms_{0};
};

} // namespace lg
