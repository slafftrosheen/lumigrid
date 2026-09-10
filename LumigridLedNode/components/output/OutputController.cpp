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

    // 0. Physical Output PWM Availability Validation
    for (uint8_t p = 0; p < 16; ++p) {
        const PhysicalOutput& phys = cfg.physical_outputs[p];
        if (phys.enabled && phys.type == OutputType::PWM) {
            if (!pwm_ok) {
                ESP_LOGE("output", "Reconfigure validation: physical_output[%u] is enabled PWM, but PCA9685 driver is unavailable", p);
                unlock();
                return ESP_ERR_NOT_FOUND;
            }
        }
    }

    // 1. Transactional Candidate Validation (Batch 7 Item 5)
    uint16_t rmt_lanes_claimed = 0;
    uint16_t pwm_channels_claimed = 0;

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fix = cfg.fixtures[f];
        const bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
        const bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                             fix.profile == FixtureProfile::PWM_RGB ||
                             fix.profile == FixtureProfile::PWM_RGBW);

        if (is_addressable && fix.num_pixels > 0) {
            if (fix.num_pixels > MAX_PIXELS) {
                ESP_LOGE("output", "Reconfigure validation: fixture[%u] num_pixels (%u) > MAX_PIXELS", f, fix.num_pixels);
                unlock();
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t p_idx = fix.physical_output_index[0];
            if (p_idx >= 16) {
                ESP_LOGE("output", "Reconfigure validation: fixture[%u] physical_output_index[0] (%u) >= 16", f, p_idx);
                unlock();
                return ESP_ERR_INVALID_ARG;
            }
            const PhysicalOutput& phys = cfg.physical_outputs[p_idx];
            if (!phys.enabled || phys.type != OutputType::RMT || phys.hardware_id >= AddressableDriver::kLanes) {
                ESP_LOGE("output", "Reconfigure validation: fixture[%u] invalid RMT physical output at slot %u", f, p_idx);
                unlock();
                return ESP_ERR_INVALID_ARG;
            }
            if (rmt_lanes_claimed & (1U << phys.hardware_id)) {
                ESP_LOGE("output", "Reconfigure validation: RMT lane %u aliased across fixtures", phys.hardware_id);
                unlock();
                return ESP_ERR_INVALID_ARG;
            }
            rmt_lanes_claimed |= (1U << phys.hardware_id);
        } else if (is_pwm) {
            if (!pwm_ok) {
                ESP_LOGE("output", "Reconfigure validation: fixture[%u] requires PWM, but PCA9685 driver is unavailable", f);
                unlock();
                return ESP_ERR_NOT_FOUND;
            }
            uint8_t pwm_chans = (fix.profile == FixtureProfile::PWM_Single) ? 1 :
                                (fix.profile == FixtureProfile::PWM_RGB ? 3 : 4);
            for (uint8_t ch = 0; ch < pwm_chans; ++ch) {
                uint8_t p_idx = fix.physical_output_index[ch];
                if (p_idx >= 16) {
                    ESP_LOGE("output", "Reconfigure validation: fixture[%u] physical_output_index[%u] (%u) >= 16", f, ch, p_idx);
                    unlock();
                    return ESP_ERR_INVALID_ARG;
                }
                const PhysicalOutput& phys = cfg.physical_outputs[p_idx];
                if (!phys.enabled || phys.type != OutputType::PWM || phys.hardware_id >= Pca9685Driver::kChannels) {
                    ESP_LOGE("output", "Reconfigure validation: fixture[%u] invalid PWM physical output at slot %u", f, p_idx);
                    unlock();
                    return ESP_ERR_INVALID_ARG;
                }
                if (pwm_channels_claimed & (1U << phys.hardware_id)) {
                    ESP_LOGE("output", "Reconfigure validation: PWM channel %u aliased across fixtures", phys.hardware_id);
                    unlock();
                    return ESP_ERR_INVALID_ARG;
                }
                pwm_channels_claimed |= (1U << phys.hardware_id);
            }
        }
    }

    // 2. Hardware Configuration: Option A topology (Addressable fixture uses physical_output_index[0])
    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fix = cfg.fixtures[f];
        if (fix.num_pixels == 0) continue;
        bool is_addressable = (fix.profile == FixtureProfile::WS2812 || fix.profile == FixtureProfile::SK6812);
        if (!is_addressable) continue;

        uint8_t p_idx = fix.physical_output_index[0];
        const PhysicalOutput& phys = cfg.physical_outputs[p_idx];
        uint8_t gpio = board::kAledPins[phys.hardware_id];

        esp_err_t err = pixels_.configure_lane(phys.hardware_id, gpio, fix.profile, fix.pixel_order, fix.num_pixels);
        if (err != ESP_OK) {
            ESP_LOGE("output", "Failed to configure lane %u: %s. Rolling back all lanes and entering safe blackout!", phys.hardware_id, esp_err_to_name(err));
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

    topology_valid_.store(true, std::memory_order_release);
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
