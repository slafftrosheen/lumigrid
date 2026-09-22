#pragma once
#include <atomic>
#include <functional>
#include "AddressableDriver.hpp"
#include "Pca9685Driver.hpp"
#include "SignageTypes.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

class OutputController;

/// Controlled output transaction RAII object (Batch 8 Item 8).
/// Enforces single-lock acquisition, staging method scoping, safety gate pass on commit,
/// and automatic deterministic unlock upon destructor exit.
class OutputTransaction {
public:
    explicit OutputTransaction(OutputController& controller);
    ~OutputTransaction();

    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;

    OutputTransaction(OutputTransaction&& other) noexcept;
    OutputTransaction& operator=(OutputTransaction&& other) noexcept;

    AddressableDriver& pixels();
    Pca9685Driver& pwm();

    esp_err_t commit();
    esp_err_t abort();
    bool is_active() const { return active_; }

private:
    OutputController& controller_;
    bool active_ = false;
};

/// Controls all output hardware (addressable LEDs + PCA9685 PWM).
///
/// Thread-safety: All physical output commits MUST pass the try_commit() safety gate.
/// Mutex synchronization protects driver access. Controlled transaction API ensures RAII locking.
class OutputController {
public:
    using CommitObserver = std::function<void(esp_err_t commit_result)>;

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

    /// Begin a controlled RAII transaction (Batch 8 Item 8)
    OutputTransaction begin_transaction() {
        return OutputTransaction(*this);
    }

    void set_commit_observer(CommitObserver observer) {
        commit_observer_ = std::move(observer);
    }

    void set_output_enabled(bool enabled);
    bool output_enabled() const { return output_enabled_.load(std::memory_order_acquire); }

    bool is_topology_valid() const { return topology_valid_.load(std::memory_order_acquire); }

    bool is_pwm_available() const { return pwm_available_.load(std::memory_order_acquire); }
    void set_pwm_available(bool available) { pwm_available_.store(available, std::memory_order_release); }

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

    friend class OutputTransaction;

private:
    esp_err_t blackout_locked();

    AddressableDriver pixels_;
    Pca9685Driver pwm_;
    SemaphoreHandle_t mutex_ = nullptr;

    CommitObserver commit_observer_{};

    std::atomic<bool> output_enabled_{true};
    std::atomic<bool> estop_latched_{false};
    std::atomic<bool> topology_valid_{false};  // Set to true only after a successful reconfigure()
    std::atomic<bool> pwm_available_{false};
    std::atomic<uint32_t> failed_commit_count_{0};
    std::atomic<esp_err_t> last_commit_error_{ESP_OK};
    std::atomic<uint32_t> last_commit_time_ms_{0};

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
public:
    void set_mock_failure_injections(uint32_t count) {
        mock_failure_injections_ = count;
    }
    void set_mock_pixel_failure(bool fail) { mock_pixel_failure_.store(fail, std::memory_order_release); }
    void set_mock_pwm_failure(bool fail) { mock_pwm_failure_.store(fail, std::memory_order_release); }
private:
    std::atomic<uint32_t> mock_failure_injections_{0};
    std::atomic<bool> mock_pixel_failure_{false};
    std::atomic<bool> mock_pwm_failure_{false};
#endif
};

} // namespace lg

