#include "OutputController.hpp"
#include "RuntimeMemory.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lg_board_pins.hpp"

namespace lg {

namespace {
// LG-038.3 §39: deterministic per-lane admission cost. The driver allocates
// one wire buffer (3 B/pixel RGB, 4 B/pixel RGBW) + one shadow buffer
// (4 B/pixel) per lane; RMT channel/encoder overhead is reserved separately.
// LG-038.3 §40/§41: explicit admission failure payload used by callers.
struct TopologyAdmission {
    bool ok = false;
    uint32_t required_bytes = 0;
    uint32_t free_bytes = 0;
    uint32_t reserve_bytes = kPostTopologyHeapReserveBytes;
};

TopologyAdmission check_topology_admission(const DeviceConfig& candidate) {
    TopologyAdmission res;
    res.required_bytes = addressable_candidate_bytes(candidate);
    const RuntimeMemorySnapshot snap = capture_runtime_memory();
    res.free_bytes = snap.free_8bit;
    res.ok = check_memory_admission_snapshot(snap, res.required_bytes, res.reserve_bytes).accepted;
    return res;
}
} // namespace

// LG-038.1: Heap telemetry helper for OutputController bring-up
void log_output_heap_stage(const char *stage) {
  ESP_LOGI(
      "lumigrid", "HEAP stage=%s free8=%lu largest8=%lu", stage,
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

esp_err_t OutputController::init() {
  ESP_LOGI("output", "Initializing OutputController...");

  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateRecursiveMutex();
    if (mutex_ == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }

  // LG-038.1: OutputController mutex created
  log_output_heap_stage("output_controller");

  lock();
  esp_err_t err = pixels_.init();
  if (err != ESP_OK) {
    unlock();
    return err;
  }

  // LG-038.1: Addressable/RMT topology initialized
  log_output_heap_stage("addressable_rmt");

  err = pwm_.init();
  if (err != ESP_OK) {
    pwm_available_.store(false, std::memory_order_release);
    ESP_LOGW("output",
             "Failed to init PCA9685 PWM driver (%s). Addressable LEDs will "
             "still work, but PWM configurations will be rejected.",
             esp_err_to_name(err));
  } else {
    pwm_available_.store(true, std::memory_order_release);
  }

  // LG-038.1: PCA9685 initialized
  log_output_heap_stage("pca9685");

  unlock();

  return ESP_OK;
}
esp_err_t OutputController::reconfigure(const DeviceConfig& cfg) {
    lock();
    ESP_LOGI("output", "Reconfiguring OutputController drivers based on DeviceConfig...");

    // LG-038.3 §39: refuse a topology that would violate the runtime heap
    // reserve BEFORE mutating any driver state (§70: reject before OOM).
    const TopologyAdmission admission = check_topology_admission(cfg);
    if (!admission.ok) {
        ESP_LOGE("output",
                 "Topology admission rejected: required=%lu free8=%lu reserve=%lu (insufficient_runtime_memory)",
                 (unsigned long)admission.required_bytes,
                 (unsigned long)admission.free_bytes,
                 (unsigned long)admission.reserve_bytes);
        note_rmt_allocation_failure();
        unlock();
        return kErrInsufficientRuntimeMemory;
    }

    const bool pwm_ok = pwm_available_.load(std::memory_order_acquire);

    // 0. PWM Availability Validation
    if (!pwm_ok) {
        for (uint8_t ch = 8; ch < 16; ++ch) {
            if (cfg.channels[ch].enabled) {
                ESP_LOGE("output", "Reconfigure validation: channel[%u] (P%u) is enabled, but PCA9685 driver is unavailable", ch, ch - 7);
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
            unlock();
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Prepare candidate buffers/encoders, then swap only after every allocation
    // succeeds. Failure leaves the live topology and its pixel order intact.
    const esp_err_t topology_err = pixels_.reconfigure(cfg, kPostTopologyHeapReserveBytes);
    if (topology_err != ESP_OK) {
        note_rmt_allocation_failure();
        unlock();
        return topology_err;
    }

    log_output_heap_stage("addressable_topology");

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
        if (pwm_ok) {
            Pca9685Driver::enable_outputs();
        }
    }
    unlock();
    return ESP_OK;
}

BlackoutResult OutputController::blackout_locked() {
    Pca9685Driver::disable_outputs();
    AddressableCommitResult a_res = pixels_.blackout();
    esp_err_t p_err = pwm_available_.load(std::memory_order_acquire) ? pwm_.clear() : ESP_OK;

    BlackoutResult res;
    res.addressable_result = a_res.result;
    res.pwm_result = p_err;
    res.addressable_confirmed = (a_res.result == ESP_OK);
    res.pwm_confirmed = (p_err == ESP_OK);
    return res;
}

BlackoutResult OutputController::blackout() {
    lock();
    BlackoutResult res = blackout_locked();
    unlock();
    return res;
}

void OutputController::latch_estop() {
    // 1. Atomically latch state
    estop_latched_.store(true, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);

    // 2. Assert PCA /OE HIGH immediately without waiting on output lock or RMT
    Pca9685Driver::force_disable_outputs();

    // 3. Acquire output lock and blackout
    lock();
    blackout_locked();
    unlock();
}

void OutputController::clear_estop() {
    // Clearing the physical latch only re-arms the controller with known-zero outputs.
    // It must NOT immediately restore previous or stale output.
    lock();
    Pca9685Driver::disable_outputs();

    esp_err_t p_err = pwm_available_.load(std::memory_order_acquire) ? pwm_.clear() : ESP_OK;
    pixels_.clear();
    AddressableCommitResult a_res = pixels_.blackout();

    if (p_err == ESP_OK && a_res.result == ESP_OK) {
        estop_latched_.store(false, std::memory_order_release);
        output_enabled_.store(true, std::memory_order_release);
        if (topology_valid_.load(std::memory_order_acquire)) {
            Pca9685Driver::enable_outputs();
        }
    } else {
        ESP_LOGE("output", "clear_estop failed to zero hardware cleanly (addr=%s, pwm=%s)",
                 output_err_name(a_res.result), esp_err_to_name(p_err));
    }
    unlock();
}

void OutputController::set_output_enabled(bool enabled) {
    lock();
    output_enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        Pca9685Driver::disable_outputs();
        blackout_locked();
    } else if (topology_valid_.load(std::memory_order_acquire) && !estop_latched_.load(std::memory_order_acquire)) {
        Pca9685Driver::enable_outputs();
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
    BlackoutResult b_res = controller_.blackout_locked();
    controller_.unlock();
    active_ = false;
    return b_res;
}

esp_err_t OutputController::try_commit_locked() {
    commit_metrics_.attempted++;

    if (estop_latched_.load(std::memory_order_acquire) ||
        !output_enabled_.load(std::memory_order_acquire) ||
        !topology_valid_.load(std::memory_order_acquire)) {
        Pca9685Driver::disable_outputs();
        BlackoutResult b_res = blackout_locked();
        esp_err_t err = (b_res.addressable_result != ESP_OK) ? b_res.addressable_result :
                        (b_res.pwm_result != ESP_OK) ? b_res.pwm_result : ESP_ERR_INVALID_STATE;
        last_commit_error_.store(err, std::memory_order_release);
        commit_metrics_.last_error = err;
        commit_metrics_.failed++;
        last_commit_time_ms_.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_release);
        if (commit_observer_) {
            commit_observer_(err);
        }
        return err;
    }

    const int64_t t_total_start = esp_timer_get_time();

    // 1. Parallel Addressable commit
    const int64_t t_addr_start = esp_timer_get_time();
    AddressableCommitResult a_res = pixels_.show();
    const int64_t t_addr_end = esp_timer_get_time();
    const uint32_t addr_us = static_cast<uint32_t>(t_addr_end - t_addr_start);

    // 2. PWM commit
    esp_err_t pwm_err = ESP_OK;
    uint32_t pwm_us = 0;
    if (pwm_available_.load(std::memory_order_acquire)) {
        const int64_t t_pwm_start = esp_timer_get_time();
        pwm_err = pwm_.commit();
        const int64_t t_pwm_end = esp_timer_get_time();
        pwm_us = static_cast<uint32_t>(t_pwm_end - t_pwm_start);
        if (pwm_err == ESP_OK) {
            Pca9685Driver::enable_outputs();
        } else {
            Pca9685Driver::disable_outputs();
        }
    }

    const int64_t t_total_end = esp_timer_get_time();
    const uint32_t total_us = static_cast<uint32_t>(t_total_end - t_total_start);

    esp_err_t err1 = a_res.result;
    esp_err_t err2 = pwm_err;

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

    commit_metrics_.last_addressable_us = addr_us;
    if (addr_us > commit_metrics_.max_addressable_us) commit_metrics_.max_addressable_us = addr_us;

    commit_metrics_.last_pwm_us = pwm_us;
    if (pwm_us > commit_metrics_.max_pwm_us) commit_metrics_.max_pwm_us = pwm_us;

    commit_metrics_.last_total_us = total_us;
    if (total_us > commit_metrics_.max_total_us) commit_metrics_.max_total_us = total_us;

    commit_metrics_.last_error = final_err;
    commit_metrics_.last_failed_lane = (err1 != ESP_OK) ? a_res.failed_lane : kNoFailedLane;

    last_commit_error_.store(final_err, std::memory_order_release);
    last_commit_time_ms_.store(static_cast<uint32_t>(t_total_end / 1000), std::memory_order_release);

    if (final_err != ESP_OK) {
        commit_metrics_.failed++;
        failed_commit_count_.fetch_add(1, std::memory_order_relaxed);
        Pca9685Driver::disable_outputs();
        blackout_locked(); // Deterministic blackout on failed commit!
    } else {
        commit_metrics_.successful++;
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
