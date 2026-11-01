#include "OutputController.hpp"
#include "ConfigStore.hpp"
#include "OutputCoreExecutor.hpp"
#include "RuntimeMemory.hpp"
#include "PowerConfigLimits.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lg_board_pins.hpp"

namespace lg {

// LG-HW-002: Output memory admission checking internal SRAM and generic heap
OutputMemoryAdmission check_output_memory_admission(
    const DeviceConfig& candidate,
    uint32_t generic_reserve,
    uint32_t internal_reserve)
{
    OutputMemoryAdmission adm;
    adm.generic_reserve = generic_reserve;
    adm.internal_reserve = internal_reserve;

    const AddressableAllocationEstimate est = estimate_addressable_allocation(candidate);
    adm.internal_required = est.total_internal_required();
    adm.generic_required = est.total_generic_required();

    const RuntimeMemorySnapshot snap = capture_runtime_memory();
    adm.free_8bit = snap.free_8bit;
    adm.free_internal = snap.free_internal;
    adm.largest_8bit = snap.largest_8bit;
    adm.largest_internal = snap.largest_internal;

    if (adm.free_internal < adm.internal_required + adm.internal_reserve) {
        adm.accepted = false;
        adm.rejection_reason = "internal_reserve";
    } else if (snap.largest_internal < est.largest_internal_allocation) {
        adm.accepted = false;
        adm.rejection_reason = "largest_internal";
    } else if (adm.free_8bit < adm.generic_required + adm.generic_reserve) {
        adm.accepted = false;
        adm.rejection_reason = "generic_reserve";
    } else if (snap.largest_8bit < est.largest_generic_allocation) {
        adm.accepted = false;
        adm.rejection_reason = "largest_8bit";
    } else {
        adm.accepted = true;
        adm.rejection_reason = nullptr;
    }

    if (!adm.accepted) {
        ESP_LOGE("output",
                 "OUTPUT_ADMISSION reject reason=%s free8=%lu freeInternal=%lu largestInternal=%lu requiredInternal=%lu requiredGeneric=%lu",
                 adm.rejection_reason ? adm.rejection_reason : "unknown",
                 (unsigned long)adm.free_8bit,
                 (unsigned long)adm.free_internal,
                 (unsigned long)adm.largest_internal,
                 (unsigned long)adm.internal_required,
                 (unsigned long)adm.generic_required);
    }

    return adm;
}

// LG-038.1 / LG-HW-001: Heap telemetry helper with internal-memory reporting
void log_output_heap_stage(const char *stage) {
  ESP_LOGI(
      "lumigrid", "HEAP stage=%s free8=%lu largest8=%lu internal8=%lu largestInternal8=%lu", stage,
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
      (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

OutputController::OutputController() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

OutputController::~OutputController() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void OutputController::lock() {
    const int64_t t0 = esp_timer_get_time();
    if (xSemaphoreTakeRecursive(mutex_, 0) != pdTRUE) {
        output_lock_contentions_.fetch_add(1, std::memory_order_relaxed);
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
        const uint32_t wait_us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        uint32_t prev = output_lock_max_wait_us_.load(std::memory_order_relaxed);
        while (wait_us > prev && !output_lock_max_wait_us_.compare_exchange_weak(prev, wait_us, std::memory_order_relaxed)) {}
    }
}

void OutputController::unlock() {
    xSemaphoreGiveRecursive(mutex_);
}

esp_err_t OutputController::init() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
  if (test_mode_.load(std::memory_order_acquire)) return ESP_OK;
#endif
  return OutputCoreExecutor::instance().execute_init(this);
}

esp_err_t OutputController::init_on_output_core() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
  if (test_mode_.load(std::memory_order_acquire)) return ESP_OK;
#endif
  const int64_t t_init_start = esp_timer_get_time();
  const int core = xPortGetCoreID();
  ESP_LOGI("output", "Hardware lifecycle executing on core=%d", core);
  if (core != 1) {
    ESP_LOGE("output", "Hardware lifecycle called on invalid core=%d (CPU1 required)", core);
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI("output", "Initializing OutputController on CPU1...");

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

  const uint32_t init_dur_us = static_cast<uint32_t>(esp_timer_get_time() - t_init_start);
  uint32_t prev_life = lifecycle_max_us_.load(std::memory_order_relaxed);
  while (init_dur_us > prev_life && !lifecycle_max_us_.compare_exchange_weak(prev_life, init_dur_us, std::memory_order_relaxed)) {}

  return ESP_OK;
}

esp_err_t OutputController::reconfigure(const DeviceConfig& cfg) {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
  if (test_mode_.load(std::memory_order_acquire)) {
    topology_reconfigure_attempts_.fetch_add(1, std::memory_order_relaxed);
    ConfigStore::ConfigValidationResult validation{};
    const esp_err_t validation_err = ConfigStore::validate_config(cfg, &validation);
    if (validation_err != ESP_OK) {
      topology_reconfigure_failures_.fetch_add(1, std::memory_order_relaxed);
      return validation_err;
    }
    for (uint8_t ch = 8; ch < 16; ++ch) {
      if (cfg.channels[ch].enabled &&
          !pwm_available_.load(std::memory_order_acquire)) {
        topology_reconfigure_failures_.fetch_add(1, std::memory_order_relaxed);
        return ESP_ERR_NOT_FOUND;
      }
    }
    topology_generation_.fetch_add(1, std::memory_order_relaxed);
    topology_valid_.store(true, std::memory_order_release);
    last_known_good_snapshot_.valid = true;
    last_known_good_snapshot_.generation = topology_generation_.load();
    power_cfg_ = cfg;
    if (!estop_latched_.load(std::memory_order_acquire)) {
      output_enabled_.store(true, std::memory_order_release);
    }
    return ESP_OK;
  }
#endif
  return OutputCoreExecutor::instance().execute_reconfigure(this, cfg);
}

esp_err_t OutputController::rollback_to_last_good_topology() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) {
        if (!last_known_good_snapshot_.valid) return ESP_ERR_NOT_FOUND;
        topology_rollbacks_.fetch_add(1, std::memory_order_relaxed);
        topology_valid_.store(true, std::memory_order_release);
        return ESP_OK;
    }
#endif
    const esp_err_t inhibit_err = Pca9685Driver::force_disable_outputs();
    if (inhibit_err != ESP_OK) {
        pixels_.cancel_candidate();
        topology_rollback_failures_++;
        topology_valid_.store(false, std::memory_order_release);
        output_enabled_.store(false, std::memory_order_release);
        last_fault_domain_.store(OutputFaultDomain::Safety, std::memory_order_release);
        return inhibit_err;
    }

    if (!last_known_good_snapshot_.valid) {
        ESP_LOGE("output", "OUTPUT_RECONFIG rollback_failed: no valid last-known-good topology snapshot available");
        topology_rollback_failures_++;
        topology_valid_.store(false, std::memory_order_release);
        output_enabled_.store(false, std::memory_order_release);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGW("output", "OUTPUT_RECONFIG attempting rollback to generation %lu",
             (unsigned long)last_known_good_snapshot_.generation);

    DeviceConfig rb_cfg{};
    for (uint8_t ch = 0; ch < 8; ++ch) {
        rb_cfg.channels[ch].enabled = last_known_good_snapshot_.addressable[ch].enabled;
        rb_cfg.channels[ch].pixel_count = last_known_good_snapshot_.addressable[ch].pixels;
        rb_cfg.channels[ch].addressable_type = last_known_good_snapshot_.addressable[ch].type;
        rb_cfg.channels[ch].pixel_order = last_known_good_snapshot_.addressable[ch].order;
        rb_cfg.channels[ch + 8].enabled = last_known_good_snapshot_.pwm_enabled[ch];
    }

    // Rebuild addressable topology:
    esp_err_t err = pixels_.reconfigure(rb_cfg, kPostTopologyHeapReserveBytes, kPostTopologyInternalReserveBytes);
    if (err != ESP_OK) {
        ESP_LOGE("output", "OUTPUT_RECONFIG rollback_failed: addressable rebuild failed: %s", output_err_name(err));
        topology_rollback_failures_++;
        topology_valid_.store(false, std::memory_order_release);
        output_enabled_.store(false, std::memory_order_release);
        Pca9685Driver::force_disable_outputs();
        return err;
    }

    // Send known-zero frame
    AddressableCommitResult bo = pixels_.blackout();
    if (bo.result != ESP_OK) {
        ESP_LOGE("output", "OUTPUT_RECONFIG rollback_failed: addressable blackout failed: %s", output_err_name(bo.result));
        topology_rollback_failures_++;
        topology_valid_.store(false, std::memory_order_release);
        output_enabled_.store(false, std::memory_order_release);
        Pca9685Driver::force_disable_outputs();
        return bo.result;
    }

    // Clear PCA
    if (pwm_available_.load(std::memory_order_acquire)) {
        err = pwm_.clear();
        if (err != ESP_OK) {
            ESP_LOGE("output", "OUTPUT_RECONFIG rollback_failed: pwm clear failed: %s", esp_err_to_name(err));
            topology_rollback_failures_++;
            topology_valid_.store(false, std::memory_order_release);
            output_enabled_.store(false, std::memory_order_release);
            Pca9685Driver::force_disable_outputs();
            return err;
        }
    }

    // Success: restore generation to snapshot generation
    topology_generation_ = last_known_good_snapshot_.generation;
    topology_rollbacks_++;
    topology_valid_.store(true, std::memory_order_release);
    if (!estop_latched_.load(std::memory_order_acquire)) {
        output_enabled_.store(true, std::memory_order_release);
    }
    ESP_LOGI("output", "OUTPUT_RECONFIG rollback_ok: restored topology generation %lu",
             (unsigned long)topology_generation_);
    return ESP_OK;
}

esp_err_t OutputController::reconfigure_on_output_core(const DeviceConfig& cfg) {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) return reconfigure(cfg);
#endif
    const int64_t t_start = esp_timer_get_time();
    topology_reconfigure_attempts_++;

    const int core = xPortGetCoreID();
    ESP_LOGI("output", "Hardware lifecycle executing on core=%d", core);
    if (core != 1) {
        ESP_LOGE("output", "Hardware lifecycle called on invalid core=%d (CPU1 required)", core);
        topology_reconfigure_failures_++;
        return ESP_ERR_INVALID_STATE;
    }

    lock();
    ESP_LOGI("output", "Reconfiguring OutputController drivers based on DeviceConfig on CPU1...");

    const bool has_last_good = last_known_good_snapshot_.valid;
    const bool previous_topology_valid = has_last_good && topology_valid_.load(std::memory_order_acquire);
    const bool previous_output_enabled = has_last_good ? output_enabled_.load(std::memory_order_acquire) : false;

    auto abort_preswap = [&](esp_err_t err, const char* reason, bool inhibit = false) -> esp_err_t {
        pixels_.cancel_candidate();
        topology_valid_.store(previous_topology_valid, std::memory_order_release);
        output_enabled_.store(inhibit ? false : previous_output_enabled, std::memory_order_release);
        last_fault_domain_.store(inhibit ? OutputFaultDomain::Safety : OutputFaultDomain::Topology, std::memory_order_release);
        // Note: Do not change /OE here; next normal frame may resume old topology
        topology_reconfigure_failures_++;
        ESP_LOGE("output", "OUTPUT_RECONFIG candidate_failed_preswap (%s): %s", reason, output_err_name(err));
        unlock();
        return err;
    };

    // Step 1: Validate config
    const bool pwm_ok = pwm_available_.load(std::memory_order_acquire);
    if (!pwm_ok) {
        for (uint8_t ch = 8; ch < 16; ++ch) {
            if (cfg.channels[ch].enabled) {
                ESP_LOGE("output", "Reconfigure validation: channel[%u] (P%u) is enabled, but PCA9685 driver is unavailable", ch, ch - 7);
                return abort_preswap(ESP_ERR_NOT_FOUND, "pca9685_unavailable");
            }
        }
    }

    for (uint8_t ch = 0; ch < 8; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (ccfg.enabled) {
            if (ccfg.pixel_count > MAX_PIXELS) {
                ESP_LOGE("output", "Reconfigure validation: channel[%u] pixel_count (%u) > MAX_PIXELS", ch, ccfg.pixel_count);
                return abort_preswap(ESP_ERR_INVALID_ARG, "pixel_count_exceeded");
            }
            if (!pixel_order_valid(ccfg.addressable_type, ccfg.pixel_order)) {
                ESP_LOGE("output", "Reconfigure validation: channel[%u] invalid pixel order for type", ch);
                return abort_preswap(ESP_ERR_INVALID_ARG, "invalid_pixel_order");
            }
        }
    }

    // Step 2: Memory admission
    const OutputMemoryAdmission admission = check_output_memory_admission(cfg);
    if (!admission.accepted) {
        note_rmt_allocation_failure();
        return abort_preswap(kErrInsufficientRuntimeMemory, admission.rejection_reason ? admission.rejection_reason : "admission_rejected");
    }

    // Step 3: Prepare all candidate allocations/encoders possible without disturbing old topology
    const esp_err_t prep_err = pixels_.prepare_candidate(cfg, kPostTopologyHeapReserveBytes, kPostTopologyInternalReserveBytes);
    if (prep_err != ESP_OK) {
        note_rmt_allocation_failure();
        return abort_preswap(prep_err, "candidate_prepare_failed");
    }

    // Step 4: Gate PCA /OE
    power_diagnostics_.command_confirmed = false;
    const esp_err_t inhibit_err = Pca9685Driver::disable_outputs();
    if (inhibit_err != ESP_OK) {
        // Preserve the old topology and stop accepting light. An emergency
        // zero write is still useful when the GPIO operation was rejected.
        last_blackout_result_ = blackout_locked();
        return abort_preswap(inhibit_err, "pwm_inhibit_failed", true);
    }

    // Step 5: Blackout old addressable topology
    AddressableCommitResult old_blackout = pixels_.blackout();
    if (old_blackout.result != ESP_OK) {
        ESP_LOGE("output", "Old topology blackout failed: %s", output_err_name(old_blackout.result));
        last_blackout_result_ = old_blackout;
        return abort_preswap(old_blackout.result, "old_blackout_failed");
    }

    // Step 6: Commit/swap candidate (point of hardware mutation)
    const esp_err_t commit_err = pixels_.commit_candidate();
    if (commit_err != ESP_OK) {
        ESP_LOGE("output", "OUTPUT_RECONFIG candidate_failed_postswap: commit_candidate failed: %s", output_err_name(commit_err));
        topology_reconfigure_failures_++;
        last_fault_domain_.store(OutputFaultDomain::Topology, std::memory_order_release);
        esp_err_t rb_err = rollback_to_last_good_topology();
        unlock();
        return (rb_err == ESP_OK) ? commit_err : rb_err;
    }

    log_output_heap_stage("addressable_topology");

    // Step 7: Blackout candidate
    AddressableCommitResult new_blackout = pixels_.blackout();
    if (new_blackout.result != ESP_OK) {
        ESP_LOGE("output", "OUTPUT_RECONFIG candidate_failed_postswap: new blackout failed: %s", output_err_name(new_blackout.result));
        last_blackout_result_ = new_blackout;
        topology_reconfigure_failures_++;
        esp_err_t rb_err = rollback_to_last_good_topology();
        unlock();
        return (rb_err == ESP_OK) ? new_blackout.result : rb_err;
    }

    // Step 8: PCA clear
    if (pwm_ok) {
        const esp_err_t pwm_clr = pwm_.clear();
        if (pwm_clr != ESP_OK) {
            Pca9685Driver::force_disable_outputs();
            ESP_LOGE("output", "OUTPUT_RECONFIG candidate_failed_postswap: PWM clear failed: %s", esp_err_to_name(pwm_clr));
            topology_reconfigure_failures_++;
            last_fault_domain_.store(OutputFaultDomain::Topology, std::memory_order_release);
            esp_err_t rb_err = rollback_to_last_good_topology();
            unlock();
            return (rb_err == ESP_OK) ? pwm_clr : rb_err;
        }
        for (uint8_t ch = 8; ch < 16; ++ch) {
            const ChannelConfig& ccfg = cfg.channels[ch];
            if (ccfg.enabled) {
                ESP_LOGI("output", "Channel[%u] (P%u) -> PCA9685 channel %u", ch, ch - 7, ch - 8);
            }
        }
    }

    // Step 9: Store last known-good snapshot and publish
    topology_generation_++;
    last_known_good_snapshot_.valid = true;
    last_known_good_snapshot_.generation = topology_generation_;
    for (uint8_t ch = 0; ch < 8; ++ch) {
        last_known_good_snapshot_.addressable[ch].enabled = cfg.channels[ch].enabled;
        last_known_good_snapshot_.addressable[ch].pixels = cfg.channels[ch].pixel_count;
        last_known_good_snapshot_.addressable[ch].type = cfg.channels[ch].addressable_type;
        last_known_good_snapshot_.addressable[ch].order = cfg.channels[ch].pixel_order;
        last_known_good_snapshot_.pwm_enabled[ch] = cfg.channels[ch + 8].enabled;
    }

    topology_valid_.store(true, std::memory_order_release);
    last_fault_domain_.store(OutputFaultDomain::None, std::memory_order_release);
    if (!estop_latched_.load(std::memory_order_acquire)) {
        output_enabled_.store(true, std::memory_order_release);
    }
    power_cfg_ = cfg;
    publish_power_locked(capture_power_locked(), true);

    const uint32_t reconfig_dur_us = static_cast<uint32_t>(esp_timer_get_time() - t_start);
    uint32_t prev_rec = reconfigure_max_us_.load(std::memory_order_relaxed);
    while (reconfig_dur_us > prev_rec && !reconfigure_max_us_.compare_exchange_weak(prev_rec, reconfig_dur_us, std::memory_order_relaxed)) {}

    ESP_LOGI("output", "OUTPUT_RECONFIG success generation=%lu duration=%lu us",
             (unsigned long)topology_generation_, (unsigned long)reconfig_dur_us);

    unlock();
    return ESP_OK;
}

BlackoutResult OutputController::blackout_locked() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) {
        BlackoutResult res{};
        res.addressable_result = ESP_OK;
        res.pwm_result = ESP_OK;
        res.addressable_confirmed = true;
        res.pwm_confirmed = true;
        last_blackout_result_ = res;
        return res;
    }
#endif
    const esp_err_t inhibit_err = Pca9685Driver::disable_outputs();
    AddressableCommitResult a_res = pixels_.blackout();
    const esp_err_t zero_err = pwm_available_.load(std::memory_order_acquire) ? pwm_.clear() : ESP_OK;
    const esp_err_t p_err = inhibit_err != ESP_OK ? inhibit_err : zero_err;

    BlackoutResult res;
    res.addressable_result = a_res.result;
    res.pwm_result = p_err;
    res.addressable_confirmed = (a_res.result == ESP_OK);
    res.pwm_confirmed = (p_err == ESP_OK);
    last_blackout_result_ = res;
    publish_power_locked(capture_power_locked(), res.addressable_confirmed && res.pwm_confirmed);
    return res;
}

BlackoutResult OutputController::blackout() {
    lock();
    BlackoutResult res = blackout_locked();
    unlock();
    return res;
}

void OutputController::latch_estop() {
    // Serialize the latch/GPIO write against the commit's final enable. A frame
    // already inside I2C/RMT must not lower /OE after this stop was observed.
    portENTER_CRITICAL(&safety_gate_mux_);
    estop_generation_.fetch_add(1, std::memory_order_relaxed);
    estop_latched_.store(true, std::memory_order_release);
    output_enabled_.store(false, std::memory_order_release);
    last_fault_domain_.store(OutputFaultDomain::Safety, std::memory_order_release);
    Pca9685Driver::force_disable_outputs();
    portEXIT_CRITICAL(&safety_gate_mux_);

    // 3. Acquire output lock and blackout
    lock();
    blackout_locked();
    unlock();
}

esp_err_t OutputController::clear_estop() {
    return clear_estop(estop_generation_.load(std::memory_order_acquire));
}

esp_err_t OutputController::clear_estop(uint32_t clear_generation) {
    // Clearing the physical latch only re-arms the controller with known-zero outputs.
    // It must NOT immediately restore previous or stale output.
    lock();
    if (estop_generation_.load(std::memory_order_acquire) != clear_generation) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    const BlackoutResult b_res = blackout_locked();
    esp_err_t err = b_res;

    if (err == ESP_OK) {
        portENTER_CRITICAL(&safety_gate_mux_);
        if (estop_generation_.load(std::memory_order_relaxed) != clear_generation) {
            // A newer stop arrived while zeroing hardware. Only a later explicit
            // clear may acknowledge that stop, even though this zero succeeded.
            err = ESP_ERR_INVALID_STATE;
        } else {
            estop_latched_.store(false, std::memory_order_release);
            output_enabled_.store(true, std::memory_order_release);
            last_fault_domain_.store(OutputFaultDomain::None, std::memory_order_release);
        }
        portEXIT_CRITICAL(&safety_gate_mux_);
        // Leave /OE HIGH! The next successful commit exposes PWM.
    } else {
        ESP_LOGE("output", "clear_estop failed to zero hardware cleanly (addr=%s, pwm=%s)",
                 output_err_name(b_res.addressable_result), esp_err_to_name(b_res.pwm_result));
    }
    unlock();
    return err;
}

esp_err_t OutputController::recover_pca() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) return ESP_OK;
#endif
    return OutputCoreExecutor::instance().execute_pca_recovery(this);
}

esp_err_t OutputController::recover_pca_on_output_core() {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) return ESP_OK;
#endif
    const int core = xPortGetCoreID();
    if (core != 1) {
        ESP_LOGE("output", "PCA recovery called on invalid core=%d (CPU1 required)", core);
        return ESP_ERR_INVALID_STATE;
    }
    lock();
    const bool output_was_confirmed = power_diagnostics_.command_confirmed;
    power_diagnostics_.command_confirmed = false;
    Pca9685Driver::disable_outputs();
    esp_err_t err = pwm_.recover();
    if (err != ESP_OK) {
        last_fault_domain_.store(OutputFaultDomain::PcaI2c, std::memory_order_release);
        Pca9685Driver::disable_outputs();
    } else {
        // init may have left PWM unavailable. Verified recovery restores it,
        // while OE and the controller's emergency/output gates stay unchanged.
        pwm_available_.store(true, std::memory_order_release);
        if (last_fault_domain_.load(std::memory_order_relaxed) == OutputFaultDomain::PcaI2c) {
            last_fault_domain_.store(OutputFaultDomain::None, std::memory_order_release);
        }
        // Recovery confirms zero PWM. It cannot independently confirm a
        // previously uncertain addressable frame, so retain that uncertainty.
        if (output_was_confirmed) publish_power_locked(capture_power_locked(), true);
    }
    unlock();
    return err;
}

void OutputController::set_output_enabled(bool enabled) {
    lock();
    output_enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        Pca9685Driver::disable_outputs();
        blackout_locked();
    }
    // When enabling: arm controller, KEEP /OE HIGH.
    // Next successful commit will expose PWM.
    unlock();
}

// ── OutputTransaction Implementation (Batch 8 Item 8) ──
OutputTransaction::OutputTransaction(OutputController& controller)
    : controller_(&controller), active_(true) {
    controller_->lock();
}

OutputTransaction::~OutputTransaction() {
    if (active_) {
        controller_->unlock();
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
            controller_->unlock();
        }
        controller_ = other.controller_;
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

AddressableDriver& OutputTransaction::pixels() {
    return controller_->pixels();
}

Pca9685Driver& OutputTransaction::pwm() {
    return controller_->pwm();
}

esp_err_t OutputTransaction::commit(const OutputPowerRequest* power_request) {
    if (!active_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = controller_->try_commit_locked(power_request);
    controller_->unlock();
    active_ = false;
    return err;
}

esp_err_t OutputTransaction::abort() {
    if (!active_) return ESP_ERR_INVALID_STATE;
    BlackoutResult b_res = controller_->blackout_locked();
    controller_->unlock();
    active_ = false;
    return b_res;
}

esp_err_t OutputController::validate_frame_topology_locked(const DeviceConfig& cfg) const {
#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    // Existing runtime mock mode has no physical RMT/PCA allocation to compare.
    if (test_mode_.load(std::memory_order_acquire)) return ESP_OK;
#endif
    if (!topology_valid_.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    bool stale = false;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const auto& requested = cfg.channels[ch];
        const auto& current = power_cfg_.channels[ch];
        stale |= requested.enabled != current.enabled;
        if (ch >= AddressableDriver::kLanes) {
            if (current.enabled && !pwm_available_.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
            continue;
        }
        if (current.enabled != pixels_.configured(ch)) return ESP_ERR_INVALID_STATE;
        if (!current.enabled) continue;
        const bool rgbw = current.addressable_type == AddressableType::SK6812;
        const bool grb = current.pixel_order == PixelOrder::GRB || current.pixel_order == PixelOrder::GRBW;
        if (!pixel_order_valid(current.addressable_type, current.pixel_order) ||
            current.pixel_count != pixels_.configured_pixels(ch) ||
            rgbw != pixels_.has_white(ch) || pixels_.rmt().pos_r(ch) != (grb ? 1 : 0) ||
            pixels_.rmt().pos_g(ch) != (grb ? 0 : 1)) return ESP_ERR_INVALID_STATE;
        if (requested.enabled) {
            stale |= requested.pixel_count != current.pixel_count ||
                     requested.addressable_type != current.addressable_type ||
                     requested.pixel_order != current.pixel_order;
        }
    }
    return stale ? kErrStaleOutputFrame : ESP_OK;
}

esp_err_t OutputController::try_commit_locked(const OutputPowerRequest* power_request) {
    commit_metrics_.attempted++;

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    if (test_mode_.load(std::memory_order_acquire)) {
        esp_err_t result = ESP_OK;
        if (estop_latched_.load(std::memory_order_acquire) ||
            !output_enabled_.load(std::memory_order_acquire) ||
            !topology_valid_.load(std::memory_order_acquire)) {
            result = ESP_ERR_INVALID_STATE;
        } else if (mock_failure_injections_ > 0) {
            --mock_failure_injections_;
            result = ESP_FAIL;
        } else if (mock_pixel_failure_.load(std::memory_order_relaxed)) {
            result = ESP_ERR_TIMEOUT;
        } else if (mock_pwm_failure_.load(std::memory_order_relaxed)) {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        if (result == ESP_OK) commit_metrics_.successful++;
        else commit_metrics_.failed++;
        last_commit_error_.store(result, std::memory_order_release);
        commit_metrics_.last_error = result;
        if (commit_observer_) commit_observer_(result);
        return result;
    }
#endif

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

    // HW-005.11: Common last-stage power guard before physical commit
    OutputPowerDiagnostics staged_power = enforce_power_guard_locked(power_request);

    const int64_t t_total_start = esp_timer_get_time();

    // 1. Parallel Addressable commit
    const int64_t t_addr_start = esp_timer_get_time();
    AddressableCommitResult a_res = pixels_.show();
    const int64_t t_addr_end = esp_timer_get_time();
    const uint32_t addr_us = static_cast<uint32_t>(t_addr_end - t_addr_start);

    // 2. PWM commit. A rejected addressable frame must never expose the new PWM
    // command while /OE is still enabled from the previous successful frame.
    // Failure recovery below writes zeros across both output families.
    esp_err_t pwm_err = ESP_OK;
    uint32_t pwm_us = 0;
    if (a_res.result == ESP_OK && pwm_available_.load(std::memory_order_acquire)) {
        const int64_t t_pwm_start = esp_timer_get_time();
        pwm_err = pwm_.commit();
        const int64_t t_pwm_end = esp_timer_get_time();
        pwm_us = static_cast<uint32_t>(t_pwm_end - t_pwm_start);
    }

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

    // OE exposure is part of a successful physical commit. A driver that still
    // requires recovery must fail the transaction even if its zero write worked.
    if (err1 == ESP_OK && err2 == ESP_OK) {
        portENTER_CRITICAL(&safety_gate_mux_);
        if (estop_latched_.load(std::memory_order_acquire) ||
            !output_enabled_.load(std::memory_order_acquire) ||
            !topology_valid_.load(std::memory_order_acquire)) {
            Pca9685Driver::force_disable_outputs();
            err2 = ESP_ERR_INVALID_STATE;
        } else if (pwm_available_.load(std::memory_order_acquire)) {
            err2 = pwm_.enable_outputs();
        }
        portEXIT_CRITICAL(&safety_gate_mux_);
    }
    esp_err_t final_err = (err1 != ESP_OK) ? err1 : err2;
    const int64_t t_total_end = esp_timer_get_time();
    const uint32_t total_us = static_cast<uint32_t>(t_total_end - t_total_start);

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
        if (estop_latched_.load(std::memory_order_acquire)) {
            last_fault_domain_.store(OutputFaultDomain::Safety, std::memory_order_release);
        } else if (err1 != ESP_OK) {
            last_fault_domain_.store(OutputFaultDomain::AddressableRmt, std::memory_order_release);
        } else if (err2 != ESP_OK) {
            last_fault_domain_.store(OutputFaultDomain::PcaI2c, std::memory_order_release);
        }
        Pca9685Driver::disable_outputs();
        blackout_locked(); // Deterministic blackout on failed commit!
    } else {
        if (last_fault_domain_.load(std::memory_order_relaxed) != OutputFaultDomain::Safety) {
            last_fault_domain_.store(OutputFaultDomain::None, std::memory_order_release);
        }
        commit_metrics_.successful++;
        publish_power_locked(staged_power, true);
    }

    if (commit_observer_) {
        commit_observer_(final_err);
    }

    return final_err;
}

power_model::Load OutputController::channel_power_locked(uint8_t ch) const {
    power_model::Load load;
    if (ch < AddressableDriver::kLanes) {
        if (!pixels_.configured(ch)) return load;
        const uint16_t count = pixels_.configured_pixels(ch);
        const uint8_t* wire = pixels_.wire_data(ch);
        if (!wire || count == 0) return load;
        load.idle = power_model::units(uint64_t(count) * power_model::kPixel.idle_mw);
        const bool rgbw = pixels_.has_white(ch);
        const uint8_t r = pixels_.rmt().pos_r(ch), g = pixels_.rmt().pos_g(ch);
        const uint8_t b = pixels_.rmt().pos_b(ch), w = pixels_.rmt().pos_w(ch);
        const size_t stride = rgbw ? 4 : 3;
        for (uint16_t pixel = 0; pixel < count; ++pixel) {
            const uint8_t* value = wire + pixel * stride;
            load.active += power_model::pixel_active(value[r], value[g], value[b], rgbw ? value[w] : 0);
        }
    } else if (power_cfg_.channels[ch].enabled && pwm_available_.load(std::memory_order_acquire)) {
        load.active = power_model::pwm_active(pwm_.values()[ch - 8], power_model::pwm_rating(power_cfg_, ch));
    }
    return load;
}

OutputPowerDiagnostics OutputController::capture_power_locked() const {
    OutputPowerDiagnostics snapshot;
    snapshot.limit_mw = power_cfg_.max_power_mw;
    snapshot.throttling_events = power_diagnostics_.throttling_events;
    snapshot.channel_throttle_events = power_diagnostics_.channel_throttle_events;
    power_model::Load channels[kChannelCount]{};
    power_model::Load total;
    uint16_t enabled_mask = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        channels[ch] = channel_power_locked(ch);
        total += channels[ch];
        if ((ch < 8 && pixels_.configured(ch)) || (ch >= 8 && power_cfg_.channels[ch].enabled)) {
            enabled_mask |= static_cast<uint16_t>(1u << ch);
            if (ch >= 8 && (power_model::pwm_rating(power_cfg_, ch) == 0 || !is_pwm_available())) {
                snapshot.unmodeled_channel_mask |= static_cast<uint16_t>(1u << ch);
            }
        }
    }
    snapshot.requested_power_mw = snapshot.limited_power_mw = power_model::reported_mw(total.total());
    snapshot.idle_power_mw = power_model::reported_mw(total.idle);
    snapshot.estimate_complete = snapshot.unmodeled_channel_mask == 0;
    snapshot.model_state = power_model::model_state(enabled_mask, snapshot.unmodeled_channel_mask);
    for (uint8_t supply = 0; supply < kMaxPowerSupplies; ++supply) {
        const auto& psu = power_cfg_.power_supplies[supply];
        auto& domain = snapshot.supply_domains[supply];
        domain.id = supply;
        domain.enabled = psu.enabled;
        domain.voltage_mv = psu.voltage_mv;
        domain.rated_current_ma = psu.rated_current_ma;
        domain.usable_power_mw = psu.usable_power_mw();
        domain.channel_mask = psu.channel_mask;
        domain.throttle_events = power_diagnostics_.supply_domains[supply].throttle_events;
        power_model::Load load;
        if (psu.enabled) {
            for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
                if ((psu.channel_mask & (1u << ch)) != 0) load += channels[ch];
            }
        }
        domain.estimated_load_mw = power_model::milliwatts(load.total());
    }
    return snapshot;
}

void OutputController::publish_power_locked(OutputPowerDiagnostics snapshot, bool confirmed) {
    if (!confirmed) {
        // Partial I/O and an unconfirmed blackout cannot establish a new output
        // estimate. Preserve the last known command and mark its status unknown.
        power_diagnostics_.command_confirmed = false;
        return;
    }
    snapshot.command_confirmed = true;
    snapshot.previous_limited_power_mw = power_diagnostics_.limited_power_mw;
    const PowerMilliwatts step = snapshot.limited_power_mw > snapshot.previous_limited_power_mw
        ? snapshot.limited_power_mw - snapshot.previous_limited_power_mw : 0;
    snapshot.max_observed_step_mw = std::max(power_diagnostics_.max_observed_step_mw, step);
    power_diagnostics_ = snapshot;
}

OutputPowerDiagnostics OutputController::power_diagnostics() const {
    auto* self = const_cast<OutputController*>(this);
    self->lock();
    OutputPowerDiagnostics snapshot = power_diagnostics_;
    self->unlock();
    return snapshot;
}

OutputPowerDiagnostics OutputController::enforce_power_guard_locked(const OutputPowerRequest* request) {
    power_model::Load channels[kChannelCount]{};
    power_model::Load requested;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        if (ch >= 8 && !power_cfg_.channels[ch].enabled) pwm_.set(ch - 8, 0);
        channels[ch] = channel_power_locked(ch);
        requested += channels[ch];
    }
    auto scale_channel = [&](uint8_t ch, uint16_t scale) {
        if (scale == power_model::kFullScale) return;
        if (ch < 8) {
            pixels_.scale_lane(ch, scale);
        } else if (is_pwm_available()) {
            pwm_.set(ch - 8, static_cast<uint16_t>((uint32_t(pwm_.values()[ch - 8]) * scale) /
                                                  power_model::kFullScale));
        }
        channels[ch] = channel_power_locked(ch);
    };
    auto domain_load = [&](uint16_t mask) {
        power_model::Load load;
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
            if ((mask & (1u << ch)) != 0) load += channels[ch];
        }
        return load;
    };

    uint16_t channel_throttles = 0;
    uint16_t supply_throttles = 0;
    bool throttled = false;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const uint16_t scale = power_model::scale_for_limit(channels[ch], power_model::channel_limit(power_cfg_, ch));
        if (scale < power_model::kFullScale) {
            throttled = true;
            channel_throttles |= static_cast<uint16_t>(1u << ch);
            scale_channel(ch, scale);
        }
    }
    for (uint8_t supply = 0; supply < kMaxPowerSupplies; ++supply) {
        const auto& psu = power_cfg_.power_supplies[supply];
        if (!psu.enabled || psu.channel_mask == 0) continue;
        const uint64_t capacity = psu.usable_power_mw();
        const uint16_t scale = capacity == 0 ? 0 : power_model::scale_for_units(
            domain_load(psu.channel_mask), power_model::units(capacity));
        if (scale < power_model::kFullScale) {
            throttled = true;
            supply_throttles |= static_cast<uint16_t>(1u << supply);
            for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
                if ((psu.channel_mask & (1u << ch)) != 0) scale_channel(ch, scale);
            }
        }
    }
    const uint16_t global_scale = power_model::scale_for_limit(domain_load(UINT16_MAX), power_cfg_.max_power_mw);
    if (global_scale < power_model::kFullScale) {
        throttled = true;
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) scale_channel(ch, global_scale);
    }

    OutputPowerDiagnostics snapshot = capture_power_locked();
    snapshot.requested_power_mw = power_model::reported_mw(requested.total());
    if (request) {
        snapshot.requested_power_mw = std::max(snapshot.requested_power_mw, request->requested_mw);
        throttled = throttled || request->throttled;
        channel_throttles |= request->limited_channel_mask;
        supply_throttles |= request->limited_supply_mask;
    }
    if (throttled) ++snapshot.throttling_events;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        if ((channel_throttles & (1u << ch)) != 0) ++snapshot.channel_throttle_events;
    }
    for (uint8_t supply = 0; supply < kMaxPowerSupplies; ++supply) {
        if ((supply_throttles & (1u << supply)) != 0) ++snapshot.supply_domains[supply].throttle_events;
    }
    return snapshot;
}

esp_err_t OutputController::try_commit() {
    lock();
    esp_err_t err = try_commit_locked();
    unlock();
    return err;
}

} // namespace lg
