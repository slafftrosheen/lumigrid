#include "LumigridRuntime.hpp"
#include "ApiAuth.hpp"
#include "ConfigApplyPlan.hpp"
#include "DiagnosticLog.hpp"
#include "NetworkSettings.hpp"
#include "NodeIdentity.hpp"
#include "OtaBootVerifier.hpp"
#include "StorageContract.hpp"
#include "esp_rom_uart.h"
#include "RecoveryTypes.hpp"
#include "RuntimeHardeningTests.hpp"
#include "BoardOutputSafety.hpp"
#include "ResetForensics.hpp"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "NvsRuntime.hpp"
#include <cinttypes>
#include <cstring>
#include <memory>
#include <new>

namespace {
// LG-038.3 В§2/В§59: heap telemetry uses the canonical 8-bit snapshot; the
// internal figure is logged once per stage for bring-up diagnostics only.
class ServiceGuard {
public:
  explicit ServiceGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
    if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
  ~ServiceGuard() { if (mutex_) xSemaphoreGiveRecursive(mutex_); }
private:
  SemaphoreHandle_t mutex_;
};
void log_heap_stage(const char *stage) {
  const lg::RuntimeMemorySnapshot mem = lg::capture_runtime_memory();
  ESP_LOGI(
      "lumigrid", "HEAP stage=%s free8=%lu largest8=%lu internal=%lu minFree=%lu", stage,
      (unsigned long)mem.free_8bit,
      (unsigned long)mem.largest_8bit,
      (unsigned long)mem.free_internal,
      (unsigned long)mem.minimum_free_heap);
}
} // namespace

namespace lg {

LumigridRuntime::LumigridRuntime()
    : source_manager_(), factory_runner_(source_manager_, output_),
      api_(factory_runner_), lighting_(source_manager_, output_, config_),
      cue_runner_(config_, lighting_), dmx_router_(lighting_), clock_(),
      scheduler_(config_, lighting_, clock_, &cue_runner_),
      node_sync_(lighting_, config_, clock_) {
  lighting_.set_config_reload_callback([this](const DeviceConfig &cfg) {
    dmx_router_.update_config(cfg);
    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    uint8_t port_count = (u_count > 0 && u_count <= 4)
                             ? static_cast<uint8_t>(u_count)
                             : (u_count > 4 ? 4 : 0);
    sacn_.set_subscribed_universes(active_unis, u_count);
    artnet_.set_output_universes(active_unis, port_count);
  });
  lighting_.set_maintenance_callback([this](uint32_t now_ms) {
    dmx_router_.tick(now_ms);
    cert_service_.tick(now_ms);
  });
}

void LumigridRuntime::write_serial_diagnostics() {
  const auto memory = capture_runtime_memory();
  const auto storage = capture_storage();
  const bool main_active = xTaskGetHandle("main") != nullptr;
  const auto& active = started_subsystems_;
  ESP_LOGI("serial_diag", "state=%s free8=%lu largest8=%lu internal=%lu dma=%lu minimum=%lu mainActive=%u",
      startup_state_str(), (unsigned long)memory.free_8bit, (unsigned long)memory.largest_8bit,
      (unsigned long)memory.free_internal, (unsigned long)memory.free_dma,
      (unsigned long)memory.minimum_free_heap, (unsigned)main_active);
  ESP_LOGI("serial_diag", "flash physical=%lu configured=%lu layout=%u health=%s running=%s next=%s image=%lu slot=%lu",
      (unsigned long)storage.physical_bytes, (unsigned long)storage.configured_bytes, kPartitionLayoutVersion,
      storage_health_name(storage.health), storage.running ? storage.running->label : "unknown",
      storage.next ? storage.next->label : "unknown", (unsigned long)storage.image_bytes,
      (unsigned long)(storage.running ? storage.running->size : 0));
  ESP_LOGI("serial_diag", "services api=%u mdns=%u lighting=%u scheduler=%u fleet=%u artnet=%u sacn=%u sync=%u mqtt=%u factory=%u netApply=%u",
      (unsigned)active.api.load(), (unsigned)active.mdns.load(), (unsigned)active.lighting.load(),
      (unsigned)active.scheduler.load(), (unsigned)active.fleet_discovery.load(),
      (unsigned)active.artnet.load(), (unsigned)active.sacn.load(), (unsigned)active.node_sync.load(),
      (unsigned)active.mqtt.load(), (unsigned)(factory_runner_.lifecycle_state() == FactoryRunner::LifecycleState::Running),
      (unsigned)apply_in_progress_.load());
  task_stack_registry().sample_all();
  TaskStackInfo tasks[kMaxTrackedTasks]{};
  const size_t count = task_stack_registry().copy(tasks, kMaxTrackedTasks);
  for (size_t i = 0; i < count; ++i)
    ESP_LOGI("serial_diag", "task=%s configured=%lu minimumFree=%lu bytes", tasks[i].name,
        (unsigned long)tasks[i].configured_bytes, (unsigned long)tasks[i].minimum_free_bytes);

  const auto rst_diag = board::get_reset_diagnostics();
  const auto boot_diag = board::get_boot_output_diagnostics();
  ESP_LOGI("serial_diag", "reset reason=%s abnormal=%d consecutive=%lu total=%lu confirmed=%d sessionId=0x%08lX",
      rst_diag.reason_str, rst_diag.last_reset_abnormal ? 1 : 0,
      (unsigned long)rst_diag.consecutive_abnormal_boots, (unsigned long)rst_diag.total_boots,
      rst_diag.current_boot_confirmed ? 1 : 0, (unsigned long)rst_diag.boot_session_id);
  ESP_LOGI("serial_diag", "boot_output safeEarly=%d oeHigh=%d aledMask=0x%02X blackConf=%d zeroConf=%d firstCommitMs=%lu",
      boot_diag.early_safety_applied ? 1 : 0, boot_diag.pca_oe_high_at_bootstrap ? 1 : 0,
      boot_diag.aled_low_mask, boot_diag.first_addressable_black_confirmed ? 1 : 0,
      boot_diag.first_pwm_zero_confirmed ? 1 : 0, (unsigned long)boot_diag.first_output_commit_ms);
}

void LumigridRuntime::recovery_task(void *arg) {
  LumigridRuntime *self = static_cast<LumigridRuntime *>(arg);
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << GPIO_NUM_0),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);

  // Startup grace period (3 seconds) to let power stabilize and avoid strapping
  // false-positives
  vTaskDelay(pdMS_TO_TICKS(3000));

  // If GPIO0 is already LOW at the end of grace period, warn and wait for
  // release to prevent stuck reboot loop
  if (gpio_get_level(GPIO_NUM_0) == 0) {
    ESP_LOGW("lumigrid", "GPIO0 is LOW on boot; waiting for button release "
                         "before arming factory reset");
    while (gpio_get_level(GPIO_NUM_0) == 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  int stable_press_count = 0;
  constexpr int kRequiredHoldTicks = 250; // 250 * 20ms = 5000ms
  bool reset_triggered = false;
  uint32_t last_verify_ms = 0;
  bool boot_diagnostics_sent = false;
  char serial_command[32]{};
  size_t serial_length = 0;
  bool serial_overflow = false;

  while (!reset_triggered) {
    vTaskDelay(pdMS_TO_TICKS(20)); // 20ms debounce sampling interval
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (self && now_ms - last_verify_ms >= 1000 && OtaBootVerifier::instance().is_pending_verify()) {
      last_verify_ms = now_ms;
      auto cfg = std::unique_ptr<DeviceConfig>(new (std::nothrow) DeviceConfig());
      const bool config_valid = cfg && self->config_.load_config(*cfg) == ESP_OK && self->config_.cached_validation_result().valid;
      const RuntimeServicePlan wanted = compute_service_plan({self->is_safe_mode(), self->commissioning_plan_state(), self->network_plan_state_.load(), cfg.get()});
      const auto& active = self->started_subsystems_;
      const bool required = (!wanted.mdns || active.mdns) && (!wanted.lighting || active.lighting) && (!wanted.scheduler || active.scheduler) &&
          (!wanted.artnet || active.artnet) && (!wanted.sacn || active.sacn) &&
          (!wanted.fleet_discovery || active.fleet_discovery) && (!wanted.node_sync || active.node_sync) && (!wanted.mqtt || active.mqtt);
      const bool deliberate_recovery = !self->is_safe_mode() && self->auth_manager_.commissioning_state() == auth::CommissioningState::PhysicalRecovery;
      const bool healthy = active.api && config_valid && required &&
          (self->startup_state_ == StartupState::Ready || (deliberate_recovery && self->services_started_));
      const bool fatal = self->startup_state_ == StartupState::Faulted || (self->is_safe_mode() && !deliberate_recovery);
      const bool critical = evaluate_memory_health(capture_runtime_memory()) == MemoryHealth::Critical;
#ifdef CONFIG_LUMIGRID_OTA_ROLLBACK_TEST
      OtaBootVerifier::instance().rollback_candidate("explicit_test_failure");
#else
      OtaBootVerifier::instance().observe_runtime(healthy, fatal, critical, now_ms);
#endif
    }

    // LG-HW-004 §13: Confirm healthy boot after 30 seconds continuous Ready state
    static uint32_t s_ready_since_ms = 0;
    static bool s_boot_confirmed = false;
    if (self && !s_boot_confirmed) {
      if (self->startup_state_ == StartupState::Ready) {
        if (s_ready_since_ms == 0) {
          s_ready_since_ms = now_ms;
        } else if (now_ms - s_ready_since_ms >= board::kHealthyBootConfirmMs) {
          board::confirm_healthy_boot();
          s_boot_confirmed = true;
          ESP_LOGI("lumigrid", "Healthy boot confirmed after %lu ms in Ready state",
                   (unsigned long)board::kHealthyBootConfirmMs);
        }
      } else if (self->startup_state_ == StartupState::Faulted) {
        s_ready_since_ms = 0;
      }
    }

    if (self && !boot_diagnostics_sent && now_ms >= 10000) {
      boot_diagnostics_sent = true;
      self->write_serial_diagnostics();
    }
#if CONFIG_ESP_CONSOLE_UART_DEFAULT && CONFIG_ESP_CONSOLE_UART_NUM == 0
    // Read-only local diagnostics: bounded FIFO polling, no UART driver/buffer/task allocation.
    for (unsigned i = 0; i < 16; ++i) {
      uint8_t ch = 0;
      if (esp_rom_uart_rx_one_char(&ch) != 0) break;
      if (ch == '\n' || ch == '\r') {
        serial_command[serial_length] = 0;
        if (self && !serial_overflow && (!strcmp(serial_command, "diagnostics") || !strcmp(serial_command, "memory") || !strcmp(serial_command, "storage") || !strcmp(serial_command, "bootsafe") || !strcmp(serial_command, "boot_verify")))
          self->write_serial_diagnostics();
        serial_length = 0; serial_overflow = false;
      } else if (ch >= 32 && ch <= 126) {
        if (serial_length + 1 < sizeof(serial_command)) serial_command[serial_length++] = ch;
        else serial_overflow = true;
      }
    }
#endif
    if (gpio_get_level(GPIO_NUM_0) == 0) {
      stable_press_count++;
      if (stable_press_count >= kRequiredHoldTicks) {
        reset_triggered = true;
        ESP_LOGE("lumigrid",
                 "FACTORY RESET INITIATED VIA HARDWARE BUTTON (5s hold)");

        // Blackout outputs before erase
        if (self) {
          self->output_.blackout();
          self->output_.set_output_enabled(false);
        }
        vTaskDelay(pdMS_TO_TICKS(100));

        if (self) {
          self->auth_manager_.factory_reset();
        }
        nvs_handle_t handle;
        if (nvs_open("lumigrid", NVS_READWRITE, &handle) == ESP_OK) {
          nvs_erase_all(handle);
          nvs_commit(handle);
          nvs_close(handle);
        }
        if (nvs_open("node_auth", NVS_READWRITE, &handle) == ESP_OK) {
          nvs_erase_all(handle);
          nvs_commit(handle);
          nvs_close(handle);
        }
        if (nvs_open("network", NVS_READWRITE, &handle) == ESP_OK) {
          nvs_erase_all(handle);
          nvs_commit(handle);
          nvs_close(handle);
        }

        // Prevent immediate re-trigger on reboot if button remains held
        ESP_LOGW("lumigrid",
                 "Factory reset complete. Release button to reboot...");
        while (gpio_get_level(GPIO_NUM_0) == 0) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
      }
    } else {
      stable_press_count = 0;
    }
  }
}

esp_err_t LumigridRuntime::init() {
  network_apply_stopping_.store(false);
  if (!services_mutex_) services_mutex_ = xSemaphoreCreateRecursiveMutex();
  if (!services_mutex_) return ESP_ERR_NO_MEM;
  DiagnosticLog::instance().init();

  const auto reset_diag = board::get_reset_diagnostics();
  boot_count_ = static_cast<int>(reset_diag.total_boots);

  const auto storage = capture_storage();
  ESP_LOGI("lumigrid", "STORAGE physical=%lu configured=%lu layout=v%u health=%s",
      (unsigned long)storage.physical_bytes, (unsigned long)storage.configured_bytes,
      kPartitionLayoutVersion, storage_health_name(storage.health));
  // LG-HW-004 §10/§12: Safe mode requires 3 consecutive abnormal boots, not merely warm reboots
  safe_mode_ = (board::is_safe_mode_mandated() || storage.health != StorageHealth::Healthy);
  startup_state_ = safe_mode_ ? StartupState::SafeMode : StartupState::Booting;
  startup_stage_ = StartupStage::None;
  failing_stage_ = StartupStage::None;
  failing_error_ = ESP_OK;

  if (xTaskCreate(recovery_task, "recovery", 4096, this, 5, NULL) != pdPASS) {
    startup_state_ = StartupState::Faulted;
    failing_error_ = ESP_ERR_NO_MEM;
    return ESP_ERR_NO_MEM;
  }
  // LG-038.3 В§29: register persistent task stacks for high-water telemetry.
  task_stack_registry().register_task("recovery", 4096);

  api_.set_diagnostics_provider([this]() -> StartupDiagnostics {
    return StartupDiagnostics{startup_state_str(), startup_stage_str(),
                              failing_stage_str(),
                              static_cast<int>(failing_error_)};
  });

  ESP_LOGI("lumigrid", "=== BOOT START ===");
  vTaskDelay(pdMS_TO_TICKS(1000));

  ESP_LOGI("lumigrid", "Step 1: Hardware output init...");
  esp_err_t err = output_.init();
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::OutputInitialized;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid",
             "HARD FAILURE: output_.init() failed: %s. Outputs blacked out and "
             "runtime faulted!",
             esp_err_to_name(err));
    return err;
  }
  startup_stage_ = StartupStage::OutputInitialized;
  log_heap_stage("output_controller");

  ESP_LOGI("lumigrid", "Step 1.1: source_manager.init()...");
  err = source_manager_.init();
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::SourceManagerInitialized;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid", "source_manager_.init() FAILED: %s",
             esp_err_to_name(err));
    return err;
  }
  startup_stage_ = StartupStage::SourceManagerInitialized;
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI("lumigrid", "Step 1.9: NVS initialization...");
  err = NvsRuntime::initialise();
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::ConfigStoreInitialized;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid", "NVS initialization FAILED: %s. Safe/recovery mode required.", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI("lumigrid", "Step 2: config.init()...");
  err = config_.init();
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::ConfigStoreInitialized;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid", "config_.init() FAILED: %s", esp_err_to_name(err));
    return err;
  }
  startup_stage_ = StartupStage::ConfigStoreInitialized;
  log_heap_stage("config_store");
  vTaskDelay(pdMS_TO_TICKS(500));

  // Power-loss-safe restore marker check (LG-036 В§41)
  uint32_t restore_chk = 0;
  if (config_.is_restore_in_progress(&restore_chk)) {
    safe_mode_ = true;
    startup_state_ = StartupState::SafeMode;
    failing_stage_ = StartupStage::ConfigLoaded;
    failing_error_ = ESP_ERR_INVALID_STATE;
    restore_state_ = RestoreState::Interrupted;
    configuration_valid_ = false;
    std::strncpy(configuration_first_error_,
                 "Previous restore was interrupted before completion",
                 sizeof(configuration_first_error_) - 1);
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid",
             "RESTORE INTERRUPTED DETECTED ON BOOT (checksum %08" PRIx32
             ")! Entering SAFE MODE!",
             restore_chk);
  }

  // OTA boot verification initialization (LG-036 В§56)
  OtaBootVerifier::instance().init();

  // Wire recovery and transactional config apply hooks
  api_.set_recovery_status_provider([this]() { return get_recovery_status(); });
  api_.set_runtime_apply_func([this](const DeviceConfig &old_cfg,
                                     const DeviceConfig &new_cfg,
                                     const ConfigApplyPlan &plan) {
    return apply_device_config(old_cfg, new_cfg, plan);
  });
  api_.set_maintenance_mode_func([this]() { return maintenance_mode(); });
  // LG-038.3/LG-038.7: observable service state for RuntimeSnapshot/diagnostics.
  api_.set_service_reconcile_callback([this]() {
    request_service_reconcile();
  });
  api_.set_service_state_provider([this]() -> ServiceStateTruth {
    const RuntimeServicePlan actual = service_plan();
    ServiceStateTruth truth{};
    truth.lighting = actual.lighting;
    truth.scheduler = actual.scheduler;
    truth.api = actual.api;
    truth.mdns = actual.mdns;
    truth.fleet_discovery = actual.fleet_discovery;
    truth.artnet = actual.artnet;
    truth.sacn = actual.sacn;
    truth.node_sync = actual.node_sync;
    truth.mqtt = actual.mqtt;
    truth.factory_worker = actual.factory_worker;
    truth.network_apply_worker = actual.network_apply_worker;
    return truth;
  });

  // Initialize AuthStore and AuthManager
  err = auth_store_.init();
  if (err == ESP_OK) err = auth_manager_.init();
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::ConfigStoreInitialized;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    return err;
  }
  api::set_auth_manager(&auth_manager_);

  std::unique_ptr<DeviceConfig> dev_cfg_storage(new (std::nothrow) DeviceConfig{});
  if (!dev_cfg_storage) {
    safe_mode_ = true;
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::ConfigLoaded;
    failing_error_ = ESP_ERR_NO_MEM;
    output_.blackout();
    output_.set_output_enabled(false);
    return ESP_ERR_NO_MEM;
  }
  DeviceConfig &dev_cfg = *dev_cfg_storage;
  if (config_.load_config(dev_cfg) == ESP_OK) {
    startup_stage_ = StartupStage::ConfigLoaded;
    // Migrate legacy credentials into AuthStore if needed (Case A: universal
    // defaults -> unprovisioned; Case B: custom -> migrated)
    auth_manager_.migrate_legacy_tokens(
        dev_cfg.admin_token, dev_cfg.installer_token, dev_cfg.operator_token);

    if (auth_manager_.is_provisioned()) {
      auth_manager_.set_commissioning_state(auth::CommissioningState::None);
    } else {
      NetworkSettings net_settings{};
      esp_err_t net_err = config_.load_network_settings(net_settings);
      bool has_configured_sta =
          (net_err == ESP_OK && net_settings.mode != NetworkMode::ApOnly &&
           net_settings.sta_ssid[0] != '\0');
      if (has_configured_sta) {
        auth_manager_.set_commissioning_state(auth::CommissioningState::None);
        ESP_LOGW("lumigrid", "Node unprovisioned with configured STA network: "
                             "bootstrap disabled until physical recovery");
      } else if (safe_mode_) {
        auth_manager_.set_commissioning_state(
            auth::CommissioningState::PhysicalRecovery);
      } else {
        auth_manager_.set_commissioning_state(
            auth::CommissioningState::FactoryFirstBoot);
      }
    }

    ConfigStore::ConfigValidationResult val_res;
    esp_err_t val_err = config_.validate_config(dev_cfg, &val_res);
    if (val_err != ESP_OK) {
      safe_mode_ = true;
      startup_state_ = StartupState::SafeMode;
      failing_stage_ = StartupStage::ConfigValidated;
      failing_error_ = val_err;
      configuration_valid_ = false;
      configuration_error_count_ = val_res.error_count;
      std::strncpy(configuration_first_error_, val_res.first_error,
                   sizeof(configuration_first_error_) - 1);
      configuration_first_error_[sizeof(configuration_first_error_) - 1] = '\0';
      output_.blackout();
      output_.set_output_enabled(false);
      ESP_LOGE("lumigrid",
               "STARTUP CONFIG VALIDATION FAILED (%u errors): %s. Entering "
               "SAFE/RECOVERY MODE!",
               (unsigned)val_res.error_count, val_res.first_error);
    } else {
      startup_stage_ = StartupStage::ConfigValidated;
      configuration_valid_ = true;
      configuration_error_count_ = 0;
      configuration_first_error_[0] = '\0';
      esp_err_t reconf_err = output_.reconfigure(dev_cfg);
      if (reconf_err != ESP_OK) {
        safe_mode_ = true;
        startup_state_ = StartupState::Faulted;
        failing_stage_ = StartupStage::OutputsConfigured;
        failing_error_ = reconf_err;
        configuration_valid_ = false;
        configuration_error_count_ = 1;
        snprintf(configuration_first_error_, sizeof(configuration_first_error_),
                 "output.reconfigure failed: %s", esp_err_to_name(reconf_err));
        output_.blackout();
        output_.set_output_enabled(false);
        ESP_LOGE("lumigrid",
                 "Output reconfigure failed: %s. Entering SAFE MODE!",
                 esp_err_to_name(reconf_err));
      } else {
        startup_stage_ = StartupStage::OutputsConfigured;
      }
    }
  } else {
    safe_mode_ = true;
    startup_state_ = StartupState::SafeMode;
    failing_stage_ = StartupStage::ConfigLoaded;
    failing_error_ = ESP_FAIL;
    configuration_valid_ = false;
    configuration_error_count_ = 1;
    std::strncpy(configuration_first_error_,
                 "Failed to load device config from NVS",
                 sizeof(configuration_first_error_) - 1);
    configuration_first_error_[sizeof(configuration_first_error_) - 1] = '\0';
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid", "Failed to load device config! Entering SAFE MODE!");
  }

#if !defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) ||                           \
    !CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
  ESP_LOGI("lumigrid", "Runtime hardening tests: DISABLED (PRODUCTION IMAGE)");
#endif

  dev_cfg_storage.reset();

  if (!safe_mode_ && commissioning_plan_state() == CommissioningPlanState::Provisioned) {
    ESP_LOGI("lumigrid", "Step 3: lighting.start()...");
    err = lighting_.start();
    if (err != ESP_OK) {
      startup_state_ = StartupState::Faulted;
      failing_stage_ = StartupStage::LightingStarted;
      failing_error_ = err;
      safe_mode_ = true;
      output_.blackout();
      output_.set_output_enabled(false);
      ESP_LOGE("lumigrid", "lighting.start() FAILED: %s", esp_err_to_name(err));
    } else {
      startup_stage_ = StartupStage::LightingStarted;
      started_subsystems_.lighting = true;
      task_stack_registry().register_task("lighting_rt", 10240);
      log_heap_stage("lighting_runtime");
    }
  } else {
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGW("lumigrid", "SAFE MODE ACTIVE: Keeping outputs blacked out, "
                         "skipping lighting runtime start");
  }

  // LG-038.3 В§17/В§18: the Factory worker is NOT started at boot anymore.
  // The FactoryRunner object stays initialized and idle (В§20: Certification
  // keeps only a pointer); the 8 KB lg_factory task is allocated on the first
  // real Factory/Certification command via ensure_worker_started().
  startup_stage_ = StartupStage::FactoryStarted;
  ESP_LOGI("lumigrid", "Step 4: factory worker lazy (not started; 8 KB reserved on first use)");

  // Step 5: Initialize CertificationService (LG-037 / LG-038)
  err = cert_service_.init(
      &config_, &factory_runner_, &api_.job_manager(), api_.identity(),
      [this]() -> CertificationProbeSnapshot {
        CertificationProbeSnapshot probe{};
        probe.runtime_ready = (startup_state_ == StartupState::Ready);
        probe.api_running = api_.is_running();
        probe.authenticated_ws_clients = api_.active_ws_clients();
        auto wifi_stat = wifi_.status();
        probe.ap_active = wifi_stat.ap_active;
        probe.sta_connected = wifi_stat.sta_connected;
        probe.backup_serializer_healthy = true;
        probe.reset_reason = static_cast<uint32_t>(esp_reset_reason());
        return probe;
      });
  if (err != ESP_OK) {
    startup_state_ = StartupState::Faulted;
    failing_stage_ = StartupStage::FactoryStarted;
    failing_error_ = err;
    safe_mode_ = true;
    output_.blackout();
    output_.set_output_enabled(false);
    ESP_LOGE("lumigrid", "Certification initialization failed: %s", esp_err_to_name(err));
    return err;
  }
  api_.set_certification_service(&cert_service_);
  log_heap_stage("certification");

  vTaskDelay(pdMS_TO_TICKS(500));

  // LG-038.3 В§7/В§8: no permanent net_apply task or queue at boot. The 8 KB
  // worker is created on demand by queue_network_apply() and deletes itself
  // after the apply finishes.

  return (startup_state_ == StartupState::Faulted) ? failing_error_ : ESP_OK;
}

void LumigridRuntime::rollback_services(StartupStage failed_stage,
                                        esp_err_t error) {
  ServiceGuard guard(services_mutex_);
  ESP_LOGW("lumigrid",
           "Rolling back runtime services due to failure at stage %s: %s",
           startup_stage_to_name(failed_stage), esp_err_to_name(error));

  // Reverse-order teardown: only stop services that actually started
  if (started_subsystems_.mqtt) {
    DiagnosticLog::instance().set_stream_callback(nullptr);
    mqtt_.stop();
    started_subsystems_.mqtt = false;
  }
  if (started_subsystems_.node_sync) {
    node_sync_.stop();
    started_subsystems_.node_sync = false;
  }
  if (started_subsystems_.scheduler) {
    scheduler_.stop();
    started_subsystems_.scheduler = false;
  }
  if (started_subsystems_.sacn) {
    sacn_.stop();
    started_subsystems_.sacn = false;
  }
  if (started_subsystems_.artnet) {
    artnet_.stop();
    started_subsystems_.artnet = false;
  }
  if (started_subsystems_.mdns) {
    mdns_.stop();
    started_subsystems_.mdns = false;
  }
  if (started_subsystems_.fleet_discovery) {
    fleet_discovery_.stop();
    started_subsystems_.fleet_discovery = false;
  }
  // NOTE: Wi-Fi and Web API are intentionally NOT stopped during rollback
  // to preserve recovery access and telemetry at 192.168.4.1 even during
  // failures.
  if (factory_runner_.lifecycle_state() == FactoryRunner::LifecycleState::Running) {
    factory_runner_.stop();
    started_subsystems_.factory = false;
  }
  if (started_subsystems_.lighting) {
    lighting_.stop();
    task_stack_registry().unregister_task("lighting_rt");
    started_subsystems_.lighting = false;
  }

  source_manager_.release(ControlSource::LiveDmx);
  source_manager_.release(ControlSource::Factory);
  output_.blackout();
  output_.set_output_enabled(false);
  services_started_ = false;

  if (error != ESP_OK) {
    safe_mode_ = true;
    startup_state_ = StartupState::Faulted;
    lifecycle_state_ = RuntimeLifecycle::Faulted;
    failing_stage_ = failed_stage;
    failing_error_ = error;
  }
}

esp_err_t LumigridRuntime::stop() {
  if (lifecycle_state_ == RuntimeLifecycle::Stopped ||
      lifecycle_state_ == RuntimeLifecycle::Constructed) {
    return ESP_OK;
  }
  lifecycle_state_ = RuntimeLifecycle::Stopping;
  network_apply_stopping_.store(true);
  // Keep Wi-Fi, journal and services alive until the owned apply finishes.
  // The worker has bounded network waits and performs its own cleanup.
  while (apply_in_progress_.load()) vTaskDelay(pdMS_TO_TICKS(10));
  ESP_LOGI("lumigrid", "Stopping LumigridRuntime...");

  rollback_services(StartupStage::None, ESP_OK);
  if (started_subsystems_.api) {
    api_.stop();
    started_subsystems_.api = false;
  }
  if (started_subsystems_.wifi) {
    wifi_.stop();
    started_subsystems_.wifi = false;
  }

  apply_in_progress_.store(false);
  apply_state_.store(NetworkApplyState::Idle);

  services_started_ = false;
  lifecycle_state_ = RuntimeLifecycle::Stopped;
  ESP_LOGI("lumigrid", "LumigridRuntime stopped cleanly");
  return ESP_OK;
}

esp_err_t LumigridRuntime::start() {
  if (lifecycle_state_ == RuntimeLifecycle::Running ||
      lifecycle_state_ == RuntimeLifecycle::Initializing ||
      lifecycle_state_ == RuntimeLifecycle::NetworkReady ||
      lifecycle_state_ == RuntimeLifecycle::ServicesStarting) {
    ESP_LOGW("lumigrid", "start() rejected: runtime is already %s",
             lifecycle_state_str());
    return ESP_ERR_INVALID_STATE;
  }
  lifecycle_state_ = RuntimeLifecycle::Initializing;
  services_started_ = false;

  // Keep large configuration snapshots off the nested startup stack.
  std::unique_ptr<DeviceConfig> dev_cfg_storage(new (std::nothrow) DeviceConfig{});
  if (!dev_cfg_storage) {
    rollback_services(StartupStage::ConfigLoaded, ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  DeviceConfig &dev_cfg = *dev_cfg_storage;
  esp_err_t config_err = config_.load_config(dev_cfg);
  if (config_err != ESP_OK) {
    rollback_services(StartupStage::ConfigLoaded, config_err);
    return config_err;
  }

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  NetworkSettings net_settings;
  config_.load_network_settings(net_settings);
  if (net_settings.ap_password[0] == '\0') {
    generate_random_ap_password(net_settings.ap_password, sizeof(net_settings.ap_password));
    net_settings.checksum = calc_network_settings_crc(net_settings);
    config_.save_network_settings(net_settings);
  }

  const bool factory_setup = !auth_manager_.is_provisioned() &&
      auth_manager_.commissioning_state() == auth::CommissioningState::FactoryFirstBoot;
  std::string ap_pass = net_settings.ap_password;
  if (factory_setup) {
    ESP_LOGI("lumigrid", "\n==================================================\n"
        "LUMIGRID FIRST SETUP\n==================================================\n"
        "Wi-Fi:       LumiGrid-Setup\n"
        "Wi-Fi key:   %s\n"
        "Web:         http://192.168.4.1\n\n"
        "Create a permanent Admin Access Key in the setup UI.\n"
        "==================================================",
        net_settings.ap_password);
  }

  // Derive AP SSID:
  //   safe_mode            -> "LumiGrid-Recovery"  (config invalid / crash
  //   loop) valid config + name  -> "LumiGrid-<device_name>"  (installed
  //   device) fallback             -> "LumiGrid-Factory"   (first boot / no
  //   config)
  std::string ap_ssid;
  if (factory_setup) {
    ap_ssid = "LumiGrid-Setup";
  } else if (safe_mode_) {
    ap_ssid = "LumiGrid-Recovery";
  } else if (configuration_valid_ && strlen(dev_cfg.device_name) > 0 &&
             std::strcmp(dev_cfg.device_name, "lumigrid") != 0) {
    ap_ssid = std::string("LumiGrid-") + dev_cfg.device_name;
  } else {
    ap_ssid = "LumiGrid-Factory";
  }

  if (!factory_setup && !safe_mode_ && net_settings.ap_name[0] != '\0') {
    ap_ssid = net_settings.ap_name;
  }

  WifiMode wifi_mode = WifiMode::ApOnly;
  if (!factory_setup && !safe_mode_) {
    switch (net_settings.mode) {
    case NetworkMode::StaOnly:
      wifi_mode = WifiMode::StaOnly;
      break;
    case NetworkMode::StaWithApFallback:
      wifi_mode = WifiMode::StaWithApFallback;
      break;
    case NetworkMode::ApOnly:
    default:
      wifi_mode = WifiMode::ApOnly;
      break;
    }
  }

  WifiConfig wifi_cfg{.mode = wifi_mode,
                      .sta_ssid = net_settings.sta_ssid,
                      .sta_password = net_settings.sta_password,
                      .ap_ssid = ap_ssid,
                      .ap_password = ap_pass,
                      .ap_channel = net_settings.ap_channel,
                      .max_retry = 3};

  // Always delete and recreate the semaphore so no stale "give" from a previous
  // WiFi callback can cause a spurious ready signal on the next start() cycle.
  {
    SemaphoreHandle_t old_sem =
        net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
    if (old_sem)
      vSemaphoreDelete(old_sem);
    net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
  }

  if (!net_ready_sem_.load(std::memory_order_acquire)) {
    rollback_services(StartupStage::NetworkConfigured, ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }

  startup_state_ = StartupState::StartingNetwork;
  ESP_LOGI("lumigrid",
           "Step 5: wifi.start() - Mode: %s, AP SSID: '%s', STA SSID: '%s'...",
           wifi_mode == WifiMode::StaOnly  ? "STA"
           : wifi_mode == WifiMode::ApOnly ? "AP"
                                           : "STA+AP Fallback",
           wifi_cfg.ap_ssid.c_str(), wifi_cfg.sta_ssid.c_str());
  esp_err_t err = wifi_.start(wifi_cfg, [this](WifiState state) {
    set_network_plan_state(state == WifiState::Connected ? NetworkPlanState::StaConnected :
                           state == WifiState::ApActive ? NetworkPlanState::ApOnly : NetworkPlanState::StaDown);
    api_.queue_service_reconcile();
    if (state == WifiState::Connected) {
      auth_manager_.set_commissioning_state(auth::CommissioningState::None);
    }
    if (state == WifiState::Connected || state == WifiState::ApActive) {
      // Load with acquire so we see the semaphore written by the creator task
      SemaphoreHandle_t sem = net_ready_sem_.load(std::memory_order_acquire);
      if (sem) {
        xSemaphoreGive(sem);
      }
    }
  });
  if (err != ESP_OK) {
    ESP_LOGE("lumigrid", "wifi.start() FAILED: %s", esp_err_to_name(err));
    rollback_services(StartupStage::NetworkConfigured, err);
    return err;
  }
  started_subsystems_.wifi = true;
  log_heap_stage("wifi");

  // Wait synchronously for network readiness (10s bounded timeout)
  if (xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire),
                     pdMS_TO_TICKS(10000)) != pdTRUE) {
    ESP_LOGW("lumigrid", "Network readiness semaphore timed out after 10s; "
                         "proceeding with Web UI startup anyway");
  }

  lifecycle_state_ = RuntimeLifecycle::NetworkReady;
  startup_stage_ = StartupStage::NetworkConfigured;

  esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap_netif) {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
      ESP_LOGW("lumigrid",
               "==================================================");
      ESP_LOGW("lumigrid", "  ACCESS POINT READY");
      ESP_LOGW("lumigrid", "  SSID:     %s", wifi_cfg.ap_ssid.c_str());
      ESP_LOGW("lumigrid", "  Security: %s",
               wifi_cfg.ap_password.empty() ? "open" : "WPA2 configured");
      ESP_LOGW("lumigrid", "  Web UI:   http://" IPSTR, IP2STR(&ip_info.ip));
      ESP_LOGW("lumigrid",
               "==================================================");
    }
  }

  lifecycle_state_ = RuntimeLifecycle::ServicesStarting;

  DeviceConfig &current_cfg = *dev_cfg_storage;
  config_err = config_.load_config(current_cfg);
  if (config_err != ESP_OK) {
    rollback_services(StartupStage::ConfigLoaded, config_err);
    return config_err;
  }
  dmx_router_.update_config(current_cfg);

  uint16_t active_unis[16];
  size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
  uint8_t port_count = (u_count > 0 && u_count <= 4)
                           ? static_cast<uint8_t>(u_count)
                           : (u_count > 4 ? 4 : 0);

  ArtnetNodeInfo node{};
  node.short_name = "LumiGrid Factory";
  node.long_name = "LumiGrid 8xALED 8xPWM Factory Test Node";
  node.report = "#0001 [0000] LumiGrid ready";
  node.num_ports = port_count;
  if (port_count > 0) {
    uint8_t net = 0, subnet = 0, univ = 0;
    decode_port_address(active_unis[0], net, subnet, univ);
    node.net = net;
    node.subnet = subnet;
    for (uint8_t i = 0; i < port_count; ++i) {
      uint8_t p_net = 0, p_sub = 0, p_univ = 0;
      decode_port_address(active_unis[i], p_net, p_sub, p_univ);
      node.sw_out[i] = p_univ;
    }
  }
  std::memcpy(node.mac, mac, 6);

  ESP_LOGI("lumigrid", "Step 6: Setting up network services & API...");
  api_.set_wifi(&wifi_);
  api_.set_auth_manager(&auth_manager_);
  api_.set_show_clock_provider([this]() { return clock_.now_ms(); });

  api_.set_network_apply_handler(
      [this](const NetworkApplyRequest &req) -> esp_err_t {
        return queue_network_apply(req);
      },
      [this](NetworkApplyState &state, uint32_t &cmd_id, const char *&err_str) {
        state = apply_state_.load();
        cmd_id = pending_apply_command_id_.load();
        err_str = last_apply_error_;
      },
      [this]() -> bool { return is_safe_mode(); });

  api_.set_config_apply_callback([this](
                                     const DeviceConfig &old_cfg,
                                     const DeviceConfig &new_cfg) -> esp_err_t {
    if (safe_mode_)
      return ESP_OK;

    const bool sync_en_changed = (old_cfg.sync_enabled != new_cfg.sync_enabled);
    const bool sync_role_changed = (old_cfg.sync_role != new_cfg.sync_role);
    const bool sync_token_changed = (old_cfg.sync_token != new_cfg.sync_token);

    if (!sync_en_changed && !sync_role_changed && !sync_token_changed) {
      return ESP_OK;
    }

    if (!new_cfg.sync_enabled) {
      if (started_subsystems_.node_sync) {
        node_sync_.stop(1000);
        started_subsystems_.node_sync = false;
      }
      return ESP_OK;
    }

    if (started_subsystems_.node_sync) {
      node_sync_.stop(1000);
      started_subsystems_.node_sync = false;
    }

    esp_err_t start_err =
        node_sync_.start(static_cast<NodeRole>(new_cfg.sync_role));
    if (start_err == ESP_OK) {
      started_subsystems_.node_sync = true;
    }
    return start_err;
  });

  node_sync_.set_transport_provider([this](uint64_t now_ms) {
    return scheduler_.transport_snapshot(now_ms);
  });

  // LG-034 В§25: CueRunner-driven status broadcasts. Meaningful runtime
  // transitions request a coalesced status push (notify_status_async is
  // internally deduplicated); the 5 Hz stream cap is enforced by the
  // broadcast pipeline, not here. Snapshot collection stays observational.
  cue_runner_.set_event_callback([this](CueRunner::Event event) {
    switch (event) {
    case CueRunner::Event::CueChanged:
    case CueRunner::Event::PhaseChanged:
    case CueRunner::Event::Paused:
    case CueRunner::Event::Resumed:
    case CueRunner::Event::ManualCueCompleted:
    case CueRunner::Event::Stopped:
      api_.notify_status_async();
      break;
    }
  });

  node_sync_.set_remote_transport_callback([this](const ShowTransportSnapshot
                                                      &remote_snap,
                                                  uint8_t &out_error) -> bool {
    if (remote_snap.mode == ShowTransportMode::Scene) {
      if (cue_runner_.snapshot(0).running)
        cue_runner_.stop(CueStopReason::Preempted, clock_.now_ms());
      scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene,
                                        remote_snap.bpm, clock_.now_ms());
      if (!config_.scene_exists(remote_snap.active_scene)) {
        out_error = static_cast<uint8_t>(SyncTransportError::SceneMissing);
        return false;
      }
      if (lighting_.active_scene_index() != remote_snap.active_scene) {
        if (!lighting_.activate_scene_checked(remote_snap.active_scene)) {
          out_error =
              static_cast<uint8_t>(SyncTransportError::RuntimeUnavailable);
          return false;
        }
      }
      return true;
    } else if (remote_snap.mode == ShowTransportMode::Timeline) {
      Sequence local_seq{};
      if (config_.load_sequence(local_seq) != ESP_OK) {
        out_error = static_cast<uint8_t>(SyncTransportError::TimelineMissing);
        return false;
      }
      if (remote_snap.timeline_active_checksum != 0 &&
          local_seq.checksum != remote_snap.timeline_active_checksum) {
        out_error = static_cast<uint8_t>(SyncTransportError::TimelineMismatch);
        return false;
      }
      if (cue_runner_.snapshot(0).running)
        cue_runner_.stop(CueStopReason::Preempted, clock_.now_ms());
      scheduler_.apply_remote_timeline(remote_snap.transport_generation,
                                       remote_snap.timeline_anchor_ms,
                                       remote_snap.timeline_active_checksum,
                                       remote_snap.bpm, clock_.now_ms());
      return true;
    } else if (remote_snap.mode == ShowTransportMode::Cue) {
      scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Cue,
                                        remote_snap.bpm, clock_.now_ms());
      if (remote_snap.cue.list_index >= kMaxCueLists) {
        out_error = static_cast<uint8_t>(SyncTransportError::CueListMissing);
        return false;
      }

      // LG-034 В§29: content parity gate. A follower must never run a
      // different Cue List under synchronized transport.
      CueList local_list{};
      if (config_.load_cue_list(remote_snap.cue.list_index, local_list) !=
              ESP_OK ||
          local_list.count == 0) {
        out_error = static_cast<uint8_t>(SyncTransportError::CueListMissing);
        return false;
      }
      if (remote_snap.cue_list_checksum != 0 &&
          remote_snap.cue_list_checksum != local_list.checksum) {
        out_error = static_cast<uint8_t>(SyncTransportError::CueListMismatch);
        return false;
      }

      CueSyncSnapshot cue_snap{};
      cue_snap.active = remote_snap.cue.active;
      cue_snap.state = static_cast<CueRunnerState>(remote_snap.cue.state);
      cue_snap.paused_from =
          static_cast<CueRunnerState>(remote_snap.cue_paused_from);
      cue_snap.list_index = remote_snap.cue.list_index;
      cue_snap.current_cue = remote_snap.cue.current_cue;
      cue_snap.next_cue = remote_snap.cue.next_cue;
      cue_snap.cue_anchor_ms = remote_snap.cue.cue_anchor_ms;
      cue_snap.paused_elapsed_ms = remote_snap.cue.paused_elapsed_ms;
      cue_snap.manual_fade_q15 = remote_snap.cue.manual_fade_q15;
      cue_snap.generation = remote_snap.cue.generation;
      cue_snap.list_checksum = remote_snap.cue_list_checksum;
      cue_snap.transition_progress_q15 =
          remote_snap.cue_transition_progress_q15;
      cue_snap.source_scene = remote_snap.cue_source_scene;

      esp_err_t cue_err =
          cue_runner_.apply_sync_snapshot(cue_snap, clock_.now_ms());
      if (cue_err != ESP_OK) {
        if (cue_err == ESP_ERR_NOT_FOUND)
          out_error = static_cast<uint8_t>(SyncTransportError::SceneMissing);
        else if (cue_err == ESP_ERR_INVALID_ARG)
          out_error = static_cast<uint8_t>(SyncTransportError::CueIndexInvalid);
        else
          out_error = static_cast<uint8_t>(SyncTransportError::CueListMissing);
        return false;
      }
      return true;
    } else {
      scheduler_.clear_remote_show_mode();
      cue_runner_.clear_remote_sync();
      return true;
    }
  });

  node_sync_.set_transport_callback([this](const ShowTransportSnapshot &snap) {
    if (snap.mode == ShowTransportMode::Timeline) {
      scheduler_.apply_remote_timeline(
          snap.transport_generation, snap.timeline_anchor_ms,
          snap.timeline_active_checksum, snap.bpm, clock_.now_ms());
    } else if (snap.mode == ShowTransportMode::Scene) {
      scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene,
                                        snap.bpm, clock_.now_ms());
    } else if (snap.mode == ShowTransportMode::Cue) {
      scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Cue,
                                        snap.bpm, clock_.now_ms());
    } else {
      scheduler_.clear_remote_show_mode();
    }
  });

  if (compute_service_plan({is_safe_mode(), commissioning_plan_state(), network_plan_state_.load(), &current_cfg}).scheduler && !started_subsystems_.scheduler) {
    ESP_LOGI("lumigrid", "Starting scheduler...");
    scheduler_.start(current_cfg.timezone);
    started_subsystems_.scheduler = true;
    task_stack_registry().register_task("scheduler", 4096);
  }

  err = start_network_services();
  if (err != ESP_OK) {
    rollback_services(StartupStage::ProtocolsStarted, err);
    return err;
  }

  startup_stage_ = StartupStage::RuntimeStarted;
  services_started_ = true;
  if (!safe_mode_) {
    startup_state_ = StartupState::Ready;
  }
  lifecycle_state_ = RuntimeLifecycle::Running;

  return ESP_OK;
}

void LumigridRuntime::stop_network_services() {
  ServiceGuard guard(services_mutex_);
  ESP_LOGI("lumigrid", "Stopping network-dependent services for rebind...");
  if (started_subsystems_.mqtt) {
    DiagnosticLog::instance().set_stream_callback(nullptr);
    mqtt_.stop();
    started_subsystems_.mqtt = false;
  }
  if (started_subsystems_.node_sync) {
    node_sync_.stop(1000);
    started_subsystems_.node_sync = false;
  }
  if (started_subsystems_.sacn) {
    sacn_.stop();
    started_subsystems_.sacn = false;
  }
  if (started_subsystems_.artnet) {
    artnet_.stop();
    started_subsystems_.artnet = false;
  }
  if (started_subsystems_.mdns) {
    mdns_.stop();
    started_subsystems_.mdns = false;
  }
  if (started_subsystems_.fleet_discovery) {
    fleet_discovery_.stop();
    started_subsystems_.fleet_discovery = false;
  }
  if (started_subsystems_.api) {
    api_.stop();
    started_subsystems_.api = false;
  }
}

esp_err_t LumigridRuntime::start_network_services() {
  ServiceGuard guard(services_mutex_);
  std::unique_ptr<DeviceConfig> current_cfg_storage(new (std::nothrow) DeviceConfig{});
  if (!current_cfg_storage) {
    ESP_LOGE("lumigrid", "Network service configuration allocation failed");
    return ESP_ERR_NO_MEM;
  }
  DeviceConfig &current_cfg = *current_cfg_storage;
  esp_err_t config_err = config_.load_config(current_cfg);
  if (config_err != ESP_OK) return config_err;

  set_network_plan_state(wifi_.state() == WifiState::Connected ? NetworkPlanState::StaConnected :
                         wifi_.state() == WifiState::ApActive ? NetworkPlanState::ApOnly : NetworkPlanState::StaDown);
  dmx_router_.update_config(current_cfg);

  if (!started_subsystems_.api) {
    ESP_LOGI("lumigrid", "Starting api.start(80, 8080)...");
    api_.set_wifi(&wifi_);
    api_.set_auth_manager(&auth_manager_);
    api_.set_fleet_discovery(&fleet_discovery_);
    api_.set_show_clock_provider([this]() { return clock_.now_ms(); });
    esp_err_t err =
        api_.start(80, 8080, &config_, &lighting_, &scheduler_, &node_sync_,
                   &dmx_router_, &cue_runner_, &show_transport_);
    if (err == ESP_OK) {
      started_subsystems_.api = true;
      log_heap_stage("http_api");
    } else {
      ESP_LOGE("lumigrid", "api.start() FAILED: %s", esp_err_to_name(err));
      return err;
    }
  }

  if (!started_subsystems_.mdns) {
    ESP_LOGI("lumigrid", "Starting mdns.start()...");
    NodeIdentity identity = get_node_identity(current_cfg.device_name);
    esp_err_t err = mdns_.start(identity.id, identity.name, 80, identity);
    if (err == ESP_OK) {
      started_subsystems_.mdns = true;
      log_heap_stage("mdns");
    } else {
      ESP_LOGE("lumigrid", "mdns startup failed: %s", esp_err_to_name(err));
      return err;
    }
  }

  // LG-038.3 В§21/В§22: Fleet Discovery only has value with real LAN
  // connectivity. It must NOT start merely because the setup/recovery AP is
  // active (В§66: first-boot AP-only keeps fleet_disc off).
  const esp_err_t reconcile_err = reconcile_services(current_cfg, current_cfg, true);
  if (reconcile_err != ESP_OK) return reconcile_err;


  return ESP_OK;
}

esp_err_t
LumigridRuntime::queue_network_apply(const NetworkApplyRequest &request) {
  if (network_apply_stopping_.load()) return ESP_ERR_INVALID_STATE;
  // One apply at a time (unchanged contract; the HTTP layer maps this to 409).
  if (apply_in_progress_.exchange(true)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (network_apply_stopping_.load()) {
    apply_in_progress_.store(false);
    return ESP_ERR_INVALID_STATE;
  }

  apply_state_.store(NetworkApplyState::Applying);
  pending_apply_command_id_.store(request.command_id);
  last_apply_error_ = nullptr;

  // LG-038.3 В§8/В§10: single owned pending request; no FreeRTOS queue and no
  // permanent worker. The 8 KB task exists only for the duration of the
  // apply and deletes itself afterwards.
  pending_apply_ = request;
  esp_err_t start_err = start_network_apply_task();
  if (start_err != ESP_OK) {
    apply_in_progress_.store(false);
    apply_state_.store(NetworkApplyState::Idle);
    pending_apply_command_id_.store(0);
    return start_err;
  }
  return ESP_OK;
}

esp_err_t LumigridRuntime::start_network_apply_task() {
  if (net_apply_task_ != nullptr) {
    // A previous one-shot worker is still finishing; treat as in-progress.
    return ESP_ERR_INVALID_STATE;
  }
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(&LumigridRuntime::net_apply_worker, "net_apply",
                              8192, this, 5, &handle, 1) != pdPASS) {
    note_network_task_start_failure();
    return ESP_ERR_NO_MEM;
  }
  net_apply_task_ = handle;
  task_stack_registry().register_task("net_apply", 8192);
  // Publish ownership before allowing the child to run or terminate.
  xTaskNotifyGive(handle);
  return ESP_OK;
}

void LumigridRuntime::net_apply_worker(void *arg) {
  auto *self = static_cast<LumigridRuntime *>(arg);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  // Copy the owned pending request, run exactly one apply, then exit so the
  // 8 KB stack is reclaimed while the node sits idle (В§7).
  NetworkApplyRequest req = self->pending_apply_;
  self->execute_network_apply(req);
  task_stack_registry().unregister_task("net_apply");
  self->net_apply_task_.store(nullptr);
  self->apply_in_progress_.store(false);
  vTaskDelete(nullptr);
}

void LumigridRuntime::execute_network_apply(const NetworkApplyRequest &req) {
  ESP_LOGI("lumigrid",
           "Executing network apply: target mode %s, SSID '%s' (cmd %" PRIu32
           ")",
           network_mode_to_string(req.desired.mode), req.desired.sta_ssid,
           req.command_id);

  // 1. Give HTTP 202 response 200ms to escape the socket
  vTaskDelay(pdMS_TO_TICKS(200));

  // Snapshot of the DeviceConfig in force before this apply; used as the
  // "old plan" baseline when reconciling services afterwards (В§65).
  std::unique_ptr<DeviceConfig> config_storage(new (std::nothrow) DeviceConfig{});
  if (!config_storage || config_.load_config(*config_storage) != ESP_OK) {
    note_network_task_start_failure();
    api_.command_journal().failed(req.command_id, ESP_ERR_NO_MEM);
    last_apply_error_ = "config_allocation_failed";
    apply_state_.store(NetworkApplyState::Failed);
    pending_apply_command_id_.store(0);
    return;
  }
  const DeviceConfig& dev_cfg = *config_storage;

  // 2. Stop network-dependent services
  stop_network_services();

  // 3. Stop Wi-Fi intentionally
  if (started_subsystems_.wifi) {
    wifi_.stop();
    started_subsystems_.wifi = false;
  }

  apply_state_.store(NetworkApplyState::WaitingForNetwork);


  std::string ap_ssid;
  if (safe_mode_) {
    ap_ssid = "LumiGrid-Recovery";
  } else if (req.desired.ap_name[0] != '\0') {
    ap_ssid = req.desired.ap_name;
  } else if (configuration_valid_ && strlen(dev_cfg.device_name) > 0 &&
             std::strcmp(dev_cfg.device_name, "lumigrid") != 0) {
    ap_ssid = std::string("LumiGrid-") + dev_cfg.device_name;
  } else {
    ap_ssid = "LumiGrid-Factory";
  }

  // Network apply uses the desired/persisted AP password
  std::string ap_pass = req.desired.ap_password;
  if (ap_pass.empty()) {
    ap_pass = req.previous.ap_password;
  }
  if (ap_pass.empty()) {
    NetworkSettings cur_net;
    config_.load_network_settings(cur_net);
    ap_pass = cur_net.ap_password;
  }

  WifiMode target_wifi_mode = WifiMode::ApOnly;
  switch (req.desired.mode) {
  case NetworkMode::StaOnly:
    target_wifi_mode = WifiMode::StaOnly;
    break;
  case NetworkMode::StaWithApFallback:
    target_wifi_mode = WifiMode::StaWithApFallback;
    break;
  case NetworkMode::ApOnly:
  default:
    target_wifi_mode = WifiMode::ApOnly;
    break;
  }

  WifiConfig wifi_cfg{.mode = target_wifi_mode,
                      .sta_ssid = req.desired.sta_ssid,
                      .sta_password = req.desired.sta_password,
                      .ap_ssid = ap_ssid,
                      .ap_password = ap_pass,
                      .ap_channel = (req.desired.ap_channel >= 1 &&
                                     req.desired.ap_channel <= 13)
                                        ? req.desired.ap_channel
                                        : (uint8_t)1,
                      .max_retry = 3};

  // Recreate readiness semaphore
  {
    SemaphoreHandle_t old_sem =
        net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
    if (old_sem)
      vSemaphoreDelete(old_sem);
    net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
  }

  esp_err_t err = wifi_.start(wifi_cfg, [this](WifiState state) {
    set_network_plan_state(state == WifiState::Connected ? NetworkPlanState::StaConnected :
                           state == WifiState::ApActive ? NetworkPlanState::ApOnly : NetworkPlanState::StaDown);
    api_.queue_service_reconcile();
    if (state == WifiState::Connected) {
      auth_manager_.set_commissioning_state(auth::CommissioningState::None);
    }
    if (state == WifiState::Connected || state == WifiState::ApActive ||
        state == WifiState::FallbackAp || state == WifiState::Failed) {
      SemaphoreHandle_t sem = net_ready_sem_.load(std::memory_order_acquire);
      if (sem)
        xSemaphoreGive(sem);
    }
  });

  if (err == ESP_OK) {
    started_subsystems_.wifi = true;
    // Wait up to 15s for network to achieve ready state
    xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire),
                   pdMS_TO_TICKS(15000));
  }

  // Determine success
  bool success = false;
  if (started_subsystems_.wifi) {
    if (req.desired.mode == NetworkMode::ApOnly &&
        wifi_.state() == WifiState::ApActive) {
      success = true;
    } else if (req.desired.mode == NetworkMode::StaOnly &&
               wifi_.state() == WifiState::Connected) {
      success = true;
    } else if (req.desired.mode == NetworkMode::StaWithApFallback &&
               wifi_.state() == WifiState::Connected) {
      success = true;
    }
  }

  if (success) {
    ESP_LOGI("lumigrid", "Network apply SUCCESS: desired network active. "
                         "Persisting configuration.");
    const esp_err_t save_err = config_.save_network_settings(req.desired);
    if (save_err != ESP_OK) {
      api_.command_journal().failed(req.command_id, save_err);
      last_apply_error_ = "network_persist_failed";
      apply_state_.store(NetworkApplyState::Failed);
      pending_apply_command_id_.store(0);
      start_network_services();
      return;
    }

    // LG-038.3 В§65: recompute the service plan after a route/mode change.
    if (wifi_.state() == WifiState::Connected) {
      set_network_plan_state(NetworkPlanState::StaConnected);
    } else {
      set_network_plan_state(NetworkPlanState::ApOnly);
    }
    const esp_err_t service_err = start_network_services();
    if (service_err != ESP_OK) {
      api_.command_journal().failed(req.command_id, service_err);
      last_apply_error_ = "service_start_failed";
      apply_state_.store(NetworkApplyState::Failed);
      pending_apply_command_id_.store(0);
      return;
    }
    api_.command_journal().applied(req.command_id);
    last_apply_error_ = nullptr;
    apply_state_.store(NetworkApplyState::Idle);
    pending_apply_command_id_.store(0);
    return;
  }

  // Failure path
  ESP_LOGW("lumigrid", "Network apply FAILED for command %" PRIu32,
           req.command_id);
  api_.command_journal().failed(req.command_id, ESP_FAIL);
  last_apply_error_ = "sta_connect_failed";

  if (req.desired.mode == NetworkMode::StaWithApFallback &&
      wifi_.is_fallback_ap()) {
    ESP_LOGI("lumigrid",
             "StaWithApFallback: Recovery AP active at 192.168.4.1. Keeping "
             "previous persisted config intact.");
    set_network_plan_state(NetworkPlanState::StaDown);
    start_network_services();
    apply_state_.store(NetworkApplyState::Failed);
    pending_apply_command_id_.store(0);
    return;
  }

  // Otherwise, rollback to req.previous
  ESP_LOGW("lumigrid", "Rolling back to previous network configuration...");
  apply_state_.store(NetworkApplyState::RollingBack);

  if (started_subsystems_.wifi) {
    wifi_.stop();
    started_subsystems_.wifi = false;
  }

  WifiMode prev_wifi_mode = WifiMode::ApOnly;
  switch (req.previous.mode) {
  case NetworkMode::StaOnly:
    prev_wifi_mode = WifiMode::StaOnly;
    break;
  case NetworkMode::StaWithApFallback:
    prev_wifi_mode = WifiMode::StaWithApFallback;
    break;
  case NetworkMode::ApOnly:
  default:
    prev_wifi_mode = WifiMode::ApOnly;
    break;
  }

  std::string prev_ap_name;
  if (req.previous.ap_name[0] != '\0') {
    prev_ap_name = req.previous.ap_name;
  } else {
    prev_ap_name = ap_ssid;
  }

  WifiConfig prev_cfg{.mode = prev_wifi_mode,
                      .sta_ssid = req.previous.sta_ssid,
                      .sta_password = req.previous.sta_password,
                      .ap_ssid = prev_ap_name,
                      .ap_password = ap_pass,
                      .ap_channel = (req.previous.ap_channel >= 1 &&
                                     req.previous.ap_channel <= 13)
                                        ? req.previous.ap_channel
                                        : (uint8_t)1,
                      .max_retry = 3};

  {
    SemaphoreHandle_t old_sem =
        net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
    if (old_sem)
      vSemaphoreDelete(old_sem);
    net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
  }

  err = wifi_.start(prev_cfg, [this](WifiState state) {
    set_network_plan_state(state == WifiState::Connected ? NetworkPlanState::StaConnected :
                           state == WifiState::ApActive ? NetworkPlanState::ApOnly : NetworkPlanState::StaDown);
    api_.queue_service_reconcile();
    if (state == WifiState::Connected || state == WifiState::ApActive ||
        state == WifiState::FallbackAp || state == WifiState::Failed) {
      SemaphoreHandle_t sem = net_ready_sem_.load(std::memory_order_acquire);
      if (sem)
        xSemaphoreGive(sem);
    }
  });

  if (err == ESP_OK) {
    started_subsystems_.wifi = true;
    xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire),
                   pdMS_TO_TICKS(15000));
  }

  // Rollback path: previous network restored. DHCP/identity is immutable;
  // discovery just rebinds (LG-038.3 В§23).
  if (wifi_.state() == WifiState::Connected) {
    set_network_plan_state(NetworkPlanState::StaConnected);
  } else if (wifi_.state() == WifiState::ApActive ||
             wifi_.state() == WifiState::FallbackAp) {
    set_network_plan_state(NetworkPlanState::StaDown);
  }
  start_network_services();
  apply_state_.store(NetworkApplyState::Failed);
  pending_apply_command_id_.store(0);
}

// в”Ђв”Ђ LG-038.3 В§61-65: deterministic service planner в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

CommissioningPlanState LumigridRuntime::commissioning_plan_state() const {
    if (auth_manager_.is_provisioned()) {
        return CommissioningPlanState::Provisioned;
    }
    switch (auth_manager_.commissioning_state()) {
        case auth::CommissioningState::FactoryFirstBoot:
            return CommissioningPlanState::FactoryFirstBoot;
        case auth::CommissioningState::PhysicalRecovery:
            return CommissioningPlanState::PhysicalRecovery;
        default:
            return CommissioningPlanState::PhysicalRecovery;
    }
}

RuntimeServicePlan LumigridRuntime::service_plan() const {
    // Observed truth, not the plan: what is actually running right now.
    RuntimeServicePlan actual{};
    actual.lighting = started_subsystems_.lighting;
    actual.scheduler = started_subsystems_.scheduler;
    actual.api = started_subsystems_.api;
    actual.mdns = started_subsystems_.mdns;
    actual.fleet_discovery = started_subsystems_.fleet_discovery;
    actual.artnet = started_subsystems_.artnet;
    actual.sacn = started_subsystems_.sacn;
    actual.node_sync = started_subsystems_.node_sync;
    actual.mqtt = started_subsystems_.mqtt;
    actual.factory_worker = factory_runner_.lifecycle_state() == FactoryRunner::LifecycleState::Running;
    actual.network_apply_worker = (net_apply_task_ != nullptr);
    return actual;
}

esp_err_t LumigridRuntime::start_artnet_service(const DeviceConfig &cfg) {
    if (started_subsystems_.artnet) {
        return ESP_OK;
    }
    if (!cfg.protocol.artnet_enabled || safe_mode_) {
        return ESP_OK;
    }
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    uint8_t port_count = (u_count > 0 && u_count <= 4)
                             ? static_cast<uint8_t>(u_count)
                             : (u_count > 4 ? 4 : 0);

    ArtnetNodeInfo node{};
    node.short_name = "LumiGrid Factory";
    node.long_name = "LumiGrid 8xALED 8xPWM Factory Test Node";
    node.report = "#0001 [0000] LumiGrid ready";
    node.num_ports = port_count;
    if (port_count > 0) {
        uint8_t net = 0, subnet = 0, univ = 0;
        decode_port_address(active_unis[0], net, subnet, univ);
        node.net = net;
        node.subnet = subnet;
        for (uint8_t i = 0; i < port_count; ++i) {
            uint8_t p_net = 0, p_sub = 0, p_univ = 0;
            decode_port_address(active_unis[i], p_net, p_sub, p_univ);
            node.sw_out[i] = p_univ;
        }
    }
    std::memcpy(node.mac, mac, 6);

    esp_err_t err = artnet_.start(node, [this](const ArtnetDmxPacket &packet) {
        if (!safe_mode_) {
            dmx_router_.handle_artnet_dmx(packet);
        }
    });
    if (err == ESP_OK) {
        started_subsystems_.artnet = true;
        artnet_.set_output_universes(active_unis, port_count);
        task_stack_registry().register_task("lg_artnet", 4096);
    }
    return err;
}

esp_err_t LumigridRuntime::start_sacn_service(const DeviceConfig &cfg) {
    if (started_subsystems_.sacn) {
        return ESP_OK;
    }
    if (!cfg.protocol.sacn_enabled || safe_mode_) {
        return ESP_OK;
    }
    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    esp_err_t err = sacn_.start([this](const SacnDmxPacket &packet) {
        if (!safe_mode_) {
            dmx_router_.handle_sacn_dmx(packet);
        }
    });
    if (err == ESP_OK) {
        started_subsystems_.sacn = true;
        sacn_.set_subscribed_universes(active_unis, u_count);
        task_stack_registry().register_task("lg_sacn", 4096);
    }
    return err;
}

esp_err_t LumigridRuntime::reconcile_services(const DeviceConfig &old_cfg,
                                              const DeviceConfig &new_cfg,
                                              bool network_changed) {
    ServiceGuard guard(services_mutex_);
    // LG-038.3 В§64/В§65: compute old and new plans, act on differences only.
    const auto desired = compute_service_plan({is_safe_mode(), commissioning_plan_state(), network_plan_state_.load(), &new_cfg});
    // Prepare newly requested protocols before retiring any old protocol.
    // A second-worker allocation failure can then undo only the first new one.
    const bool had_artnet = started_subsystems_.artnet;
    if (desired.artnet && !had_artnet) {
        const esp_err_t err = start_artnet_service(new_cfg);
        if (err != ESP_OK) return err;
    }
    if (desired.sacn && !started_subsystems_.sacn) {
        const esp_err_t err = start_sacn_service(new_cfg);
        if (err != ESP_OK) {
            if (!had_artnet && started_subsystems_.artnet && artnet_.stop() == ESP_OK)
                started_subsystems_.artnet = false;
            return err;
        }
    }
    if (!desired.lighting && started_subsystems_.lighting) {
        lighting_.stop();
        started_subsystems_.lighting = false;
    }
    if (desired.lighting && !started_subsystems_.lighting) {
        const esp_err_t err = lighting_.start();
        if (err != ESP_OK) return err;
        started_subsystems_.lighting = true;
        task_stack_registry().register_task("lighting_rt", 10240);
    }
    if (!desired.scheduler && started_subsystems_.scheduler) {
        scheduler_.stop();
        started_subsystems_.scheduler = false;
    }
    if (desired.scheduler && !started_subsystems_.scheduler) {
        const esp_err_t err = scheduler_.start(new_cfg.timezone);
        if (err != ESP_OK) return err;
        started_subsystems_.scheduler = true;
        task_stack_registry().register_task("scheduler", 4096);
    }

    const DeviceConfig& current = new_cfg;

    // в”Ђв”Ђ Art-Net lifecycle (В§15) в”Ђв”Ђ
    if (!desired.artnet) {
        if (started_subsystems_.artnet) {
            const esp_err_t stop_err = artnet_.stop();
            if (stop_err != ESP_OK) return stop_err;
            started_subsystems_.artnet = false;
            ESP_LOGI("lumigrid", "Art-Net disabled: worker stopped");
        }
    } else if (!started_subsystems_.artnet) {
        esp_err_t err = start_artnet_service(current);
        if (err != ESP_OK) {
            ESP_LOGE("lumigrid", "Art-Net start failed (%s); state rolled back",
                     esp_err_to_name(err));
            return err; // started_subsystems_.artnet stays false в†’ consistent
        }
        ESP_LOGI("lumigrid", "Art-Net enabled: worker started");
    } else if (old_cfg.protocol.universe_start != current.protocol.universe_start ||
               network_changed) {
        // true -> true: refresh mappings only (В§15).
        uint16_t active_unis[16];
        size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
        uint8_t port_count = (u_count > 0 && u_count <= 4)
                                 ? static_cast<uint8_t>(u_count)
                                 : (u_count > 4 ? 4 : 0);
        artnet_.set_output_universes(active_unis, port_count);
    }

    // в”Ђв”Ђ sACN lifecycle (В§16) в”Ђв”Ђ
    if (!desired.sacn) {
        if (started_subsystems_.sacn) {
            const esp_err_t stop_err = sacn_.stop();
            if (stop_err != ESP_OK) return stop_err;
            started_subsystems_.sacn = false;
            ESP_LOGI("lumigrid", "sACN disabled: worker stopped");
        }
    } else if (!started_subsystems_.sacn) {
        esp_err_t err = start_sacn_service(current);
        if (err != ESP_OK) {
            ESP_LOGE("lumigrid", "sACN start failed (%s); state rolled back",
                     esp_err_to_name(err));
            return err;
        }
        ESP_LOGI("lumigrid", "sACN enabled: worker started");
    } else if (network_changed ||
               old_cfg.protocol.universe_start != current.protocol.universe_start) {
        uint16_t active_unis[16];
        size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
        sacn_.set_subscribed_universes(active_unis, u_count);
    }

    // в”Ђв”Ђ Fleet discovery follows network lifecycle (В§23) в”Ђв”Ђ
    if (network_changed && started_subsystems_.fleet_discovery) {
        const esp_err_t err = fleet_discovery_.stop();
        if (err != ESP_OK) return err;
        started_subsystems_.fleet_discovery = false;
    }
    if (desired.fleet_discovery) {
        if (!started_subsystems_.fleet_discovery) {
            NodeIdentity identity = get_node_identity(current.device_name);
            const esp_err_t err = fleet_discovery_.start(identity);
            if (err != ESP_OK) return err;
            {
                started_subsystems_.fleet_discovery = true;
                task_stack_registry().register_task("fleet_disc", 4096);
                ESP_LOGI("lumigrid", "Fleet discovery started (STA connected)");
            }
        }
    } else if (started_subsystems_.fleet_discovery) {
        const esp_err_t stop_err = fleet_discovery_.stop();
        if (stop_err != ESP_OK) return stop_err;
        started_subsystems_.fleet_discovery = false;
        ESP_LOGI("lumigrid", "Fleet discovery stopped (no shared LAN)");
    }

    // в”Ђв”Ђ Sync follows config AND network (В§25, В§65) в”Ђв”Ђ
    const bool sync_wanted = desired.node_sync;
    if (!sync_wanted && started_subsystems_.node_sync) {
        const esp_err_t stop_err = node_sync_.stop(1000);
        if (stop_err != ESP_OK) return stop_err;
        started_subsystems_.node_sync = false;
        task_stack_registry().unregister_task("sync_master");
        task_stack_registry().unregister_task("sync_worker");
        task_stack_registry().unregister_task("sync_auto");
    }
    if (sync_wanted && !started_subsystems_.node_sync) {
        const esp_err_t err = node_sync_.start(static_cast<NodeRole>(current.sync_role));
        if (err != ESP_OK) return err;
        started_subsystems_.node_sync = true;
        task_stack_registry().register_task(current.sync_role == static_cast<uint8_t>(NodeRole::Master) ? "sync_master" :
            current.sync_role == static_cast<uint8_t>(NodeRole::Auto) ? "sync_auto" : "sync_worker", 4096);
    }
    if (!desired.mqtt && started_subsystems_.mqtt) {
        DiagnosticLog::instance().set_stream_callback(nullptr);
        mqtt_.stop();
        started_subsystems_.mqtt = false;
    }
    if (desired.mqtt && !started_subsystems_.mqtt) {
    ESP_LOGI("lumigrid", "Starting mqtt.start()...");
    char broker_uri[128];
    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d",
             current.mqtt_broker, current.mqtt_port);
    NodeIdentity id = get_node_identity(current.device_name);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "lumigrid/%s", id.id);

    const esp_err_t init_err = mqtt_.init(broker_uri, id.id, prefix, &lighting_);
    if (init_err != ESP_OK) return init_err;
    {
      const esp_err_t start_err = mqtt_.start();
      if (start_err != ESP_OK) return start_err;
      started_subsystems_.mqtt = true;
      static MqttService *s_mqtt_ptr = &mqtt_;
      DiagnosticLog::instance().set_stream_callback(
          [](const char *msg, size_t len) {
            if (s_mqtt_ptr)
              s_mqtt_ptr->publish_log(msg, len);
          });
      mqtt_.set_command_callback([this](const char *payload, size_t len) {
        ESP_LOGI("lumigrid", "Received MQTT command: %.*s", (int)len, payload);
      });
    }
  }
    return ESP_OK;
}

RecoveryStatus LumigridRuntime::get_recovery_status() const {
  RecoveryStatus st{};
  st.safe_mode = is_safe_mode();
  st.config_valid = is_configuration_valid();
  st.commissioning_state = auth_manager_.commissioning_state();
  st.startup_state = static_cast<uint8_t>(startup_state_);
  st.failing_stage = static_cast<uint8_t>(failing_stage_);
  st.failing_error = static_cast<int32_t>(failing_error_);
  WifiState ws = wifi_.state();
  st.recovery_network_active =
      (ws == WifiState::ApActive || ws == WifiState::FallbackAp);
  st.outputs_forced_off =
      !output_.output_enabled() || output_.is_estop_latched() || is_safe_mode();
  st.ota_state = OtaBootVerifier::instance().boot_state();
  st.restore_state = restore_state_;
  if (configuration_first_error_[0] != '\0') {
    std::strncpy(st.failure_reason, configuration_first_error_,
                 sizeof(st.failure_reason) - 1);
    st.failure_reason[sizeof(st.failure_reason) - 1] = '\0';
  }
  return st;
}

esp_err_t LumigridRuntime::apply_device_config(const DeviceConfig &old_cfg,
                                               const DeviceConfig &new_cfg,
                                               const ConfigApplyPlan &plan) {
  ServiceGuard guard(services_mutex_);
  if (plan.impact == ConfigApplyImpact::None) {
    return ESP_OK;
  }

  // 1. Output reconfigure if topology changed (pixel count, type, order,
  // enabled channels)
  if (plan.requires_output_reconfigure()) {
    ESP_LOGI(
        "lumigrid",
        "Output topology change detected. Reconfiguring OutputController...");
    set_maintenance_mode(MaintenanceMode::ConfigApply);

    esp_err_t reconf_err = output_.reconfigure(new_cfg);
    if (reconf_err != ESP_OK) {
      ESP_LOGE("lumigrid",
               "Output reconfigure failed (%s), rolling back to previous "
               "output topology",
               esp_err_to_name(reconf_err));
      set_maintenance_mode(MaintenanceMode::None);
      return reconf_err;
    }
    set_maintenance_mode(MaintenanceMode::None);
  }

  // 2. Lighting hot updates (gamma, power budget, soft-start, watchdog)
  if (plan.requires_lighting_hot()) {
    lighting_.reload_device_config();
  }

  // 3. DMX protocol reconfigure
  // LG-038.3 В§14: mapping refresh is only part of the story. Protocol
  // enable/disable must actually start/stop the workers (В§15/В§16).
  if (plan.requires_dmx_reconfigure()) {
    dmx_router_.update_config(new_cfg);
    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    uint8_t port_count = (u_count > 0 && u_count <= 4)
                             ? static_cast<uint8_t>(u_count)
                             : (u_count > 4 ? 4 : 0);
    if (started_subsystems_.sacn) {
      sacn_.set_subscribed_universes(active_unis, u_count);
    }
    if (started_subsystems_.artnet) {
      artnet_.set_output_universes(active_unis, port_count);
    }
  }

  // Stop changed service instances before the planner starts their replacements.
  if (plan.requires_scheduler_restart() && started_subsystems_.scheduler) {
    const esp_err_t err = scheduler_.stop();
    if (err != ESP_OK) return err;
    started_subsystems_.scheduler = false;
    task_stack_registry().unregister_task("scheduler");
  }
  if (plan.requires_sync_restart() && started_subsystems_.node_sync) {
    const esp_err_t err = node_sync_.stop(1000);
    if (err != ESP_OK) return err;
    started_subsystems_.node_sync = false;
    task_stack_registry().unregister_task("sync_master");
    task_stack_registry().unregister_task("sync_worker");
    task_stack_registry().unregister_task("sync_auto");
  }
  if (plan.requires_mqtt_restart() && started_subsystems_.mqtt) {
    DiagnosticLog::instance().set_stream_callback(nullptr);
    mqtt_.stop();
    started_subsystems_.mqtt = false;
  }
  const esp_err_t reconcile_err = reconcile_services(old_cfg, new_cfg, false);
  if (reconcile_err != ESP_OK) return reconcile_err;

  // 7. Check certification invalidation on hardware-relevant config changes
  // (LG-037)
  cert_service_.check_invalidation(new_cfg, api_.identity().hw_revision,
                                   api_.identity().fw_version);

  return ESP_OK;
}

void LumigridRuntime::request_service_reconcile() {
  service_reconcile_pending_.store(true, std::memory_order_release);
  if (!services_started_.load(std::memory_order_acquire) ||
      apply_in_progress_.load(std::memory_order_acquire) ||
      network_apply_stopping_.load(std::memory_order_acquire)) {
    return;
  }

  bool expected = false;
  if (!reconcile_worker_running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return; // Already running; it will observe pending flag before exiting
  }

  BaseType_t ret = xTaskCreate([](void* arg) {
    auto* self = static_cast<LumigridRuntime*>(arg);
    while (self->service_reconcile_pending_.exchange(false, std::memory_order_acq_rel)) {
      if (self->services_started_.load(std::memory_order_acquire) &&
          !self->apply_in_progress_.load(std::memory_order_acquire) &&
          !self->network_apply_stopping_.load(std::memory_order_acquire)) {
        std::unique_ptr<DeviceConfig> cfg(new (std::nothrow) DeviceConfig{});
        if (cfg && self->config_.load_config(*cfg) == ESP_OK) {
          const esp_err_t result = self->reconcile_services(*cfg, *cfg, true);
          if (result != ESP_OK) {
            ESP_LOGE("lumigrid", "Service reconciliation failed: %s", esp_err_to_name(result));
          }
        } else {
          note_api_allocation_failure();
        }
      }
    }
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGD("lumigrid", "runtime_svc finished, stack watermark: %u", (unsigned)watermark);
    self->reconcile_worker_running_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
  }, "runtime_svc", 4096, this, 3, nullptr);

  if (ret != pdPASS) {
    reconcile_worker_running_.store(false, std::memory_order_release);
    ESP_LOGE("lumigrid", "Failed to create runtime_svc task");
  }
}

} // namespace lg
