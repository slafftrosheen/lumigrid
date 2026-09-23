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
        pwm_available_.store(false, std::memory_order_release);
        ESP_LOGW("output", "Failed to init PCA9685 PWM driver (%s). Addressable LEDs will still work, but PWM configurations will be rejected.", esp_err_to_name(err));
    } else {
        pwm_available_.store(true, std::memory_order_release);
    }
    unlock();

    return ESP_OK;
}

esp_err_t OutputController::reconfigure(const DeviceConfig& cfg) {
    lock();
    ESP_LOGI("output", "Reconfiguring OutputController drivers based on DeviceConfig...");

    const bool pwm_ok = pwm_available_.load(std::memory_order_acquire);

    // 0. PWM Availability Validation
    if (!pwm_ok) {
        for (uint8_t ch = 8; ch < 16; ++ch) {
            if (cfg.channels[ch].enabled) {
                ESP_LOGE("output", "Reconfigure validation: channel[%u] (P%u) is enabled, but PCA9685 driver is unavailable", ch, ch - 7);
                topology_valid_.store(false, std::memory_order_release);
                output_enabled_.store(false, std::memory_order_release);
                unlock();
                return ESP_ERR_NOT_FOUND;
            }
        }
    }

    // 1. Validate addressable channels
    for (uint8_t ch = 0; ch < 8; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (ccfg.enabled && ccfg.pixel_count > MAX_PIXELS) {
            ESP_LOGE("output", "Reconfigure validation: channel[%u] pixel_count (%u) > MAX_PIXELS", ch, ccfg.pixel_count);
            topology_valid_.store(false, std::memory_order_release);
            output_enabled_.store(false, std::memory_order_release);
            unlock();
            return ESP_ERR_INVALID_ARG;
        }
    }

    // 2. Hardware Configuration: Addressable channels (RMT lanes A1..A8)
    for (uint8_t ch = 0; ch < 8; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (!ccfg.enabled || ccfg.pixel_count == 0) {
            pixels_.clear_lane(ch);
            continue;
        }

        uint8_t gpio = board::kAledPins[ch];
        FixtureProfile prof = (ccfg.addressable_type == AddressableType::SK6812) ? FixtureProfile::SK6812 : FixtureProfile::WS2812;

        esp_err_t err = pixels_.configure_lane(ch, gpio, prof, ccfg.pixel_order, ccfg.pixel_count);
        if (err != ESP_OK) {
            ESP_LOGE("output", "Failed to configure lane %u (GPIO %u): %s. Rolling back all lanes and entering safe blackout!",
                     ch, gpio, esp_err_to_name(err));
            topology_valid_.store(false, std::memory_order_release);
            output_enabled_.store(false, std::memory_order_release);
            for (uint8_t l = 0; l < OutputCapabilities::kRmtLanes; ++l) {
                pixels_.clear_lane(l);
            }
            last_commit_error_.store(err, std::memory_order_release);
            blackout_locked();
            unlock();
            return err;
        }
    }

    // 3. Hardware Configuration: PWM channels (PCA9685 channels P1..P8)
    if (pwm_ok) {
        esp_err_t pwm_clr = pwm_.clear();
        if (pwm_clr != ESP_OK) {
            ESP_LOGW("output", "PWM clear after reconfigure failed: %s (non-fatal, channels may retain stale state)", esp_err_to_name(pwm_clr));
        }
        for (uint8_t ch = 8; ch < 16; ++ch) {
            const ChannelConfig& ccfg = cfg.channels[ch];
            if (ccfg.enabled) {
                ESP_LOGI("output", "Channel[%u] (P%u) -> PCA9685 channel %u", ch, ch - 7, ch - 8);
            }
        }
    }

    topology_valid_.store(true, std::memory_order_release);
    if (!estop_latched_.load(std::memory_order_acquire)) {
        output_enabled_.store(true, std::memory_order_release);
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

// ── OutputTransaction Implementation (Batch 8 Item 8) ──
OutputTransaction::OutputTransaction(OutputController& controller)
    : controller_(controller), active_(true) {
    controller_.lock();
}

OutputTransaction::~OutputTransaction() {
    if (active_) {
        controller_.unlock();
        active_ = false;
    }
}

OutputTransaction::OutputTransaction(OutputTransaction&& other) noexcept
    : controller_(other.controller_), active_(other.active_) {
    other.active_ = false;
}

OutputTransaction& OutputTransaction::operator=(OutputTransaction&& other) noexcept {
    if (this != &other) {
        if (active_) {
            controller_.unlock();
        }
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

AddressableDriver& OutputTransaction::pixels() {
    return controller_.pixels();
}

Pca9685Driver& OutputTransaction::pwm() {
    return controller_.pwm();
}

esp_err_t OutputTransaction::commit() {
    if (!active_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = controller_.try_commit_locked();
    controller_.unlock();
    active_ = false;
    return err;
}

esp_err_t OutputTransaction::abort() {
    if (!active_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = controller_.blackout_locked();
    controller_.unlock();
    active_ = false;
    return err;
}

esp_err_t OutputController::try_commit_locked() {
    static uint32_t s_commit_count = 0;
    static uint32_t s_last_log_ms = 0;
    s_commit_count++;
    
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (now - s_last_log_ms >= 1000) {
        s_last_log_ms = now;
        uint32_t non_zero_pixels = 0;
        uint32_t non_zero_bytes = 0;
        uint32_t pixel_count = 0;
        auto& buf = pixels_.buffer();
        for (uint8_t l = 0; l < AddressableDriver::kLanes; ++l) {
            for (uint16_t p = 0; p < AddressableDriver::kPixels; ++p) {
                pixel_count++;
                if (buf[l][p].r > 0 || buf[l][p].g > 0 || buf[l][p].b > 0) {
                    non_zero_pixels++;
                    non_zero_bytes += (buf[l][p].r > 0 ? 1 : 0) + (buf[l][p].g > 0 ? 1 : 0) + (buf[l][p].b > 0 ? 1 : 0);
                }
            }
        }
        bool enabled = output_enabled_.load(std::memory_order_acquire);
        bool estop = estop_latched_.load(std::memory_order_acquire);
        bool valid = topology_valid_.load(std::memory_order_acquire);
        ESP_LOGI("output_diag", "OUTPUT frame=%lu enabled=%d lanes=8 pixels=%lu nonZeroPx=%lu nonZeroBytes=%lu estop=%d valid=%d",
                 s_commit_count, enabled, pixel_count, non_zero_pixels, non_zero_bytes, estop, valid);
    }


    if (estop_latched_.load(std::memory_order_acquire) ||
        !output_enabled_.load(std::memory_order_acquire) ||
        !topology_valid_.load(std::memory_order_acquire)) {
        esp_err_t b_err = blackout_locked();
        esp_err_t err = (b_err != ESP_OK) ? b_err : ESP_ERR_INVALID_STATE;
        last_commit_error_.store(err, std::memory_order_release);
        last_commit_time_ms_.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_release);
        if (commit_observer_) {
            commit_observer_(err);
        }
        return err;
    }

    esp_err_t err1 = pixels_.show();
    esp_err_t err2 = pwm_available_.load(std::memory_order_acquire) ? pwm_.commit() : ESP_OK;

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (mock_failure_injections_ > 0) {
        mock_failure_injections_--;
        err1 = ESP_FAIL;
    }
    if (mock_pixel_failure_.load(std::memory_order_relaxed)) {
        err1 = ESP_ERR_TIMEOUT;
    }
    if (mock_pwm_failure_.load(std::memory_order_relaxed)) {
        err2 = ESP_ERR_INVALID_RESPONSE;
    }
#endif

    esp_err_t final_err = (err1 != ESP_OK) ? err1 : err2;
    last_commit_error_.store(final_err, std::memory_order_release);
    last_commit_time_ms_.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_release);
    if (final_err != ESP_OK) {
        failed_commit_count_.fetch_add(1, std::memory_order_relaxed);
        blackout_locked(); // Deterministic blackout on failed commit!
    }

    if (commit_observer_) {
        commit_observer_(final_err);
    }

    return final_err;
}

esp_err_t OutputController::try_commit() {
    lock();
    esp_err_t err = try_commit_locked();
    unlock();
    return err;
}

} // namespace lg
