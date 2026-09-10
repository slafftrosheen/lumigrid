#include "OutputController.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "lg_board_pins.hpp"

namespace lg {

esp_err_t OutputController::init() {
    ESP_LOGI("output", "Initializing OutputController...");

    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateRecursiveMutex();
        if (mutex_ == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    lock();
    esp_err_t err = pixels_.init();
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    err = pwm_.init();
    if (err != ESP_OK) {
        ESP_LOGW("output", "Failed to init PCA9685 PWM driver (%s). Addressable LEDs will still work.", esp_err_to_name(err));
    }
    unlock();

    return ESP_OK;
}

esp_err_t OutputController::reconfigure(const DeviceConfig& cfg) {
    lock();
    ESP_LOGI("output", "Reconfiguring OutputController drivers based on DeviceConfig...");
    
    // We iterate over the fixtures, map them to physical RMT outputs, and 
    // configure the addressable driver lane appropriately.
    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fix = cfg.fixtures[f];
        if (fix.num_pixels == 0) continue;

        for (uint8_t i = 0; i < 4; ++i) {
            uint8_t p_idx = fix.physical_output_index[i];
            if (p_idx >= 16) continue;

            const PhysicalOutput& phys = cfg.physical_outputs[p_idx];
            if (!phys.enabled) continue;

            if (phys.type == OutputType::RMT && phys.hardware_id < AddressableDriver::kLanes) {
                // Determine GPIO based on hardware_id
                uint8_t gpio = board::kAledPins[phys.hardware_id];
                
                // Optional: override with schema GPIO if dynamic mapping is desired
                // if (phys.gpio_pin != 0) gpio = phys.gpio_pin;

                esp_err_t err = pixels_.configure_lane(phys.hardware_id, gpio, fix.profile, fix.pixel_order, fix.num_pixels);
                if (err != ESP_OK) {
                    ESP_LOGE("output", "Failed to configure lane %u: %s", phys.hardware_id, esp_err_to_name(err));
                }
            }
            // PWM channels don't need explicit reconfiguration, Pca9685 driver handles 0-4095 globally
        }
    }
    
    unlock();
    return ESP_OK;
}

esp_err_t OutputController::blackout_locked() {
    esp_err_t err1 = pixels_.blackout();
    esp_err_t err2 = pwm_.clear();
    return (err1 != ESP_OK) ? err1 : err2;
}

esp_err_t OutputController::blackout() {
    lock();
    esp_err_t err = blackout_locked();
    unlock();
    return err;
}

void OutputController::latch_estop() {
    lock();
    estop_latched_.store(true, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);
    blackout_locked();
    unlock();
}

void OutputController::clear_estop() {
    // Clearing the physical latch only re-arms the controller.
    // It must NOT immediately restore previous output.
    lock();
    estop_latched_.store(false, std::memory_order_release);
    output_enabled_.store(true, std::memory_order_release);
    blackout_locked();
    unlock();
}

void OutputController::set_output_enabled(bool enabled) {
    lock();
    output_enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        blackout_locked();
    }
    unlock();
}

esp_err_t OutputController::try_commit() {
    lock();

    if (estop_latched_.load(std::memory_order_acquire) ||
        !output_enabled_.load(std::memory_order_acquire)) {
        esp_err_t b_err = blackout_locked();
        esp_err_t err = (b_err != ESP_OK) ? b_err : ESP_ERR_INVALID_STATE;
        last_commit_error_.store(err, std::memory_order_release);
        last_commit_time_ms_.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_release);
        unlock();
        return err;
    }

    esp_err_t err1 = pixels_.show();
    esp_err_t err2 = pwm_.commit();

    esp_err_t final_err = (err1 != ESP_OK) ? err1 : err2;
    last_commit_error_.store(final_err, std::memory_order_release);
    last_commit_time_ms_.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_release);
    if (final_err != ESP_OK) {
        commit_fault_count_.fetch_add(1, std::memory_order_relaxed);
    }

    unlock();
    return final_err;
}

} // namespace lg
