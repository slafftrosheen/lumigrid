#include "LumigridRuntime.hpp"
#include "RuntimeHardeningTests.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "DiagnosticLog.hpp"
#include "ApiAuth.hpp"
#include "NodeIdentity.hpp"
#include "NetworkSettings.hpp"
#include "OtaBootVerifier.hpp"
#include "RecoveryTypes.hpp"
#include "ConfigApplyPlan.hpp"
#include <cstring>
#include <cinttypes>

namespace lg {

RTC_DATA_ATTR static int s_boot_count = 0;

LumigridRuntime::LumigridRuntime()
    : source_manager_(),
      factory_runner_(source_manager_, output_),
      api_(factory_runner_),
      lighting_(source_manager_, output_, config_),
      cue_runner_(config_, lighting_),
      dmx_router_(lighting_),
      clock_(),
      scheduler_(config_, lighting_, clock_, &cue_runner_),
      node_sync_(lighting_, config_, clock_) {
    lighting_.set_config_reload_callback([this](const DeviceConfig& cfg) {
        dmx_router_.update_config(cfg);
        uint16_t active_unis[16];
        size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
        uint8_t port_count = (u_count > 0 && u_count <= 4) ? static_cast<uint8_t>(u_count) : (u_count > 4 ? 4 : 0);
        sacn_.set_subscribed_universes(active_unis, u_count);
        artnet_.set_output_universes(active_unis, port_count);
    });
    lighting_.set_maintenance_callback([this](uint32_t now_ms) {
        dmx_router_.tick(now_ms);
    });
}

void LumigridRuntime::recovery_task(void* arg) {
    LumigridRuntime* self = static_cast<LumigridRuntime*>(arg);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Startup grace period (3 seconds) to let power stabilize and avoid strapping false-positives
    vTaskDelay(pdMS_TO_TICKS(3000));

    // If GPIO0 is already LOW at the end of grace period, warn and wait for release to prevent stuck reboot loop
    if (gpio_get_level(GPIO_NUM_0) == 0) {
        ESP_LOGW("lumigrid", "GPIO0 is LOW on boot; waiting for button release before arming factory reset");
        while (gpio_get_level(GPIO_NUM_0) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    int stable_press_count = 0;
    constexpr int kRequiredHoldTicks = 250; // 250 * 20ms = 5000ms
    bool reset_triggered = false;

    while (!reset_triggered) {
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms debounce sampling interval

        if (gpio_get_level(GPIO_NUM_0) == 0) {
            stable_press_count++;
            if (stable_press_count >= kRequiredHoldTicks) {
                reset_triggered = true;
                ESP_LOGE("lumigrid", "FACTORY RESET INITIATED VIA HARDWARE BUTTON (5s hold)");

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
                ESP_LOGW("lumigrid", "Factory reset complete. Release button to reboot...");
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
    DiagnosticLog::instance().init();

    if (esp_reset_reason() == ESP_RST_POWERON) {
        s_boot_count = 0;
    }
    s_boot_count++;
    
    safe_mode_ = (s_boot_count >= 3);
    startup_state_ = safe_mode_ ? StartupState::SafeMode : StartupState::Booting;
    startup_stage_ = StartupStage::None;
    failing_stage_ = StartupStage::None;
    failing_error_ = ESP_OK;

    xTaskCreate(recovery_task, "recovery", 4096, this, 5, NULL);

    api_.set_diagnostics_provider([this]() -> StartupDiagnostics {
        return StartupDiagnostics{
            startup_state_str(),
            startup_stage_str(),
            failing_stage_str(),
            static_cast<int>(failing_error_)
        };
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
        ESP_LOGE("lumigrid", "HARD FAILURE: output_.init() failed: %s. Outputs blacked out and runtime faulted!", esp_err_to_name(err));
        return err;
    }
    startup_stage_ = StartupStage::OutputInitialized;

    ESP_LOGI("lumigrid", "Step 1.1: source_manager.init()...");
    err = source_manager_.init();
    if (err != ESP_OK) {
        startup_state_ = StartupState::Faulted;
        failing_stage_ = StartupStage::SourceManagerInitialized;
        failing_error_ = err;
        safe_mode_ = true;
        output_.blackout();
        output_.set_output_enabled(false);
        ESP_LOGE("lumigrid", "source_manager_.init() FAILED: %s", esp_err_to_name(err));
        return err;
    }
    startup_stage_ = StartupStage::SourceManagerInitialized;
    vTaskDelay(pdMS_TO_TICKS(100));

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
    vTaskDelay(pdMS_TO_TICKS(500));

    // Power-loss-safe restore marker check (LG-036 §41)
    uint32_t restore_chk = 0;
    if (config_.is_restore_in_progress(&restore_chk)) {
        safe_mode_ = true;
        startup_state_ = StartupState::SafeMode;
        failing_stage_ = StartupStage::ConfigLoaded;
        failing_error_ = ESP_ERR_INVALID_STATE;
        restore_state_ = RestoreState::Interrupted;
        configuration_valid_ = false;
        std::strncpy(configuration_first_error_, "Previous restore was interrupted before completion", sizeof(configuration_first_error_) - 1);
        output_.blackout();
        output_.set_output_enabled(false);
        ESP_LOGE("lumigrid", "RESTORE INTERRUPTED DETECTED ON BOOT (checksum %08" PRIx32 ")! Entering SAFE MODE!", restore_chk);
    }

    // OTA boot verification initialization (LG-036 §56)
    OtaBootVerifier::instance().init();

    // Wire recovery and transactional config apply hooks
    api_.set_recovery_status_provider([this]() { return get_recovery_status(); });
    api_.set_runtime_apply_func([this](const DeviceConfig& old_cfg, const DeviceConfig& new_cfg, const ConfigApplyPlan& plan) {
        return apply_device_config(old_cfg, new_cfg, plan);
    });
    api_.set_maintenance_mode_func([this]() { return maintenance_mode(); });

    // Initialize AuthStore and AuthManager
    auth_store_.init();
    auth_manager_.init();
    api::set_auth_manager(&auth_manager_);

    DeviceConfig dev_cfg;
    if (config_.load_config(dev_cfg) == ESP_OK) {
        startup_stage_ = StartupStage::ConfigLoaded;
        // Migrate legacy credentials into AuthStore if needed (Case A: universal defaults -> unprovisioned; Case B: custom -> migrated)
        auth_manager_.migrate_legacy_tokens(dev_cfg.admin_token, dev_cfg.installer_token, dev_cfg.operator_token);

        if (auth_manager_.is_provisioned()) {
            auth_manager_.set_commissioning_state(auth::CommissioningState::None);
        } else {
            NetworkSettings net_settings{};
            esp_err_t net_err = config_.load_network_settings(net_settings);
            bool has_configured_sta = (net_err == ESP_OK && net_settings.mode != NetworkMode::ApOnly && net_settings.sta_ssid[0] != '\0');
            if (has_configured_sta) {
                auth_manager_.set_commissioning_state(auth::CommissioningState::None);
                ESP_LOGW("lumigrid", "Node unprovisioned with configured STA network: bootstrap disabled until physical recovery");
            } else if (safe_mode_) {
                auth_manager_.set_commissioning_state(auth::CommissioningState::PhysicalRecovery);
            } else {
                auth_manager_.set_commissioning_state(auth::CommissioningState::FactoryFirstBoot);
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
            std::strncpy(configuration_first_error_, val_res.first_error, sizeof(configuration_first_error_) - 1);
            configuration_first_error_[sizeof(configuration_first_error_) - 1] = '\0';
            output_.blackout();
            output_.set_output_enabled(false);
            ESP_LOGE("lumigrid", "STARTUP CONFIG VALIDATION FAILED (%u errors): %s. Entering SAFE/RECOVERY MODE!",
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
                snprintf(configuration_first_error_, sizeof(configuration_first_error_), "output.reconfigure failed: %s", esp_err_to_name(reconf_err));
                output_.blackout();
                output_.set_output_enabled(false);
                ESP_LOGE("lumigrid", "Output reconfigure failed: %s. Entering SAFE MODE!", esp_err_to_name(reconf_err));
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
        std::strncpy(configuration_first_error_, "Failed to load device config from NVS", sizeof(configuration_first_error_) - 1);
        configuration_first_error_[sizeof(configuration_first_error_) - 1] = '\0';
        output_.blackout();
        output_.set_output_enabled(false);
        ESP_LOGE("lumigrid", "Failed to load device config! Entering SAFE MODE!");
    }

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
    ESP_LOGW("lumigrid", "==================================================");
    ESP_LOGW("lumigrid", "  RUNTIME HARDENING TESTS: ENABLED (TEST BUILD)   ");
    ESP_LOGW("lumigrid", "==================================================");
    run_runtime_hardening_tests();
#else
    ESP_LOGI("lumigrid", "Runtime hardening tests: DISABLED (PRODUCTION IMAGE)");
#endif

    if (!safe_mode_) {
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
        }
    } else {
        output_.blackout();
        output_.set_output_enabled(false);
        ESP_LOGW("lumigrid", "SAFE MODE ACTIVE: Keeping outputs blacked out, skipping lighting runtime start");
    }

    ESP_LOGI("lumigrid", "Step 4: factory_runner.start()...");
    err = factory_runner_.start();
    if (err != ESP_OK) {
        startup_state_ = StartupState::Faulted;
        failing_stage_ = StartupStage::FactoryStarted;
        failing_error_ = err;
        ESP_LOGE("lumigrid", "factory_runner.start() FAILED: %s", esp_err_to_name(err));
    } else {
        startup_stage_ = StartupStage::FactoryStarted;
        started_subsystems_.factory = true;
    }

    // Step 5: Initialize CertificationService (LG-037)
    cert_service_.init(&config_, &factory_runner_, api_.identity());
    api_.set_certification_service(&cert_service_);

    vTaskDelay(pdMS_TO_TICKS(500));

    if (!net_apply_queue_) {
        net_apply_queue_ = xQueueCreate(1, sizeof(NetworkApplyRequest));
    }
    if (!net_apply_task_ && net_apply_queue_) {
        xTaskCreatePinnedToCore(&LumigridRuntime::net_apply_worker, "net_apply", 8192, this, 5, &net_apply_task_, 1);
    }

    return (startup_state_ == StartupState::Faulted) ? failing_error_ : ESP_OK;
}

void LumigridRuntime::rollback_services(StartupStage failed_stage, esp_err_t error) {
    ESP_LOGW("lumigrid", "Rolling back runtime services due to failure at stage %s: %s",
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
    // to preserve recovery access and telemetry at 192.168.4.1 even during failures.
    if (started_subsystems_.factory) {
        factory_runner_.stop();
        started_subsystems_.factory = false;
    }
    if (started_subsystems_.lighting) {
        lighting_.stop();
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
    if (lifecycle_state_ == RuntimeLifecycle::Stopped || lifecycle_state_ == RuntimeLifecycle::Constructed) {
        return ESP_OK;
    }
    lifecycle_state_ = RuntimeLifecycle::Stopping;
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

    if (net_apply_task_) {
        TaskHandle_t t = net_apply_task_;
        net_apply_task_ = nullptr;
        vTaskDelete(t);
    }
    if (net_apply_queue_) {
        QueueHandle_t q = net_apply_queue_;
        net_apply_queue_ = nullptr;
        vQueueDelete(q);
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
        ESP_LOGW("lumigrid", "start() rejected: runtime is already %s", lifecycle_state_str());
        return ESP_ERR_INVALID_STATE;
    }
    lifecycle_state_ = RuntimeLifecycle::Initializing;
    services_started_ = false;

    DeviceConfig dev_cfg{};
    config_.load_config(dev_cfg);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    std::string ap_pass;
#if defined(CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS) && defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS)
    ap_pass = "lumigrid123";
#else
    // LG-036: Recovery AP credential semantics.
    // The MAC-derived passphrase "lg_<mac-fragment>" serves strictly as transitional
    // layer-2 wireless access to reach the node during first-boot commissioning or recovery.
    // NOTE: Recovery AP access password != node authentication boundary.
    // Actual authorization remains strictly gated by AuthManager and physical recovery.
    char mac_pass[32];
    snprintf(mac_pass, sizeof(mac_pass), "lg_%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    ap_pass = mac_pass;
#endif


    // Derive AP SSID:
    //   safe_mode            -> "LumiGrid-Recovery"  (config invalid / crash loop)
    //   valid config + name  -> "LumiGrid-<device_name>"  (installed device)
    //   fallback             -> "LumiGrid-Factory"   (first boot / no config)
    std::string ap_ssid;
    if (safe_mode_) {
        ap_ssid = "LumiGrid-Recovery";
    } else if (configuration_valid_ && strlen(dev_cfg.device_name) > 0
               && std::strcmp(dev_cfg.device_name, "lumigrid") != 0) {
        ap_ssid = std::string("LumiGrid-") + dev_cfg.device_name;
    } else {
        ap_ssid = "LumiGrid-Factory";
    }

    NetworkSettings net_settings;
    config_.load_network_settings(net_settings);
    if (!safe_mode_ && net_settings.ap_name[0] != '\0') {
        ap_ssid = net_settings.ap_name;
    }

    WifiMode wifi_mode = WifiMode::ApOnly;
    if (!safe_mode_) {
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

    WifiConfig wifi_cfg{
        .mode = wifi_mode,
        .sta_ssid = net_settings.sta_ssid,
        .sta_password = net_settings.sta_password,
        .ap_ssid = ap_ssid,
        .ap_password = ap_pass,
        .ap_channel = net_settings.ap_channel,
        .max_retry = 3
    };

    // Always delete and recreate the semaphore so no stale "give" from a previous
    // WiFi callback can cause a spurious ready signal on the next start() cycle.
    {
        SemaphoreHandle_t old_sem = net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
        if (old_sem) vSemaphoreDelete(old_sem);
        net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
    }

    startup_state_ = StartupState::StartingNetwork;
    ESP_LOGI("lumigrid", "Step 5: wifi.start() - Mode: %s, AP SSID: '%s', STA SSID: '%s'...",
             wifi_mode == WifiMode::StaOnly ? "STA" :
             wifi_mode == WifiMode::ApOnly ? "AP" : "STA+AP Fallback",
             wifi_cfg.ap_ssid.c_str(), wifi_cfg.sta_ssid.c_str());
    esp_err_t err = wifi_.start(
        wifi_cfg,
        [this](WifiState state) {
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
        }
    );
    if (err != ESP_OK) {
        ESP_LOGE("lumigrid", "wifi.start() FAILED: %s", esp_err_to_name(err));
        rollback_services(StartupStage::NetworkConfigured, err);
        return err;
    }
    started_subsystems_.wifi = true;

    // Wait synchronously for network readiness (10s bounded timeout)
    if (xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire), pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW("lumigrid", "Network readiness semaphore timed out after 10s; proceeding with Web UI startup anyway");
    }

    lifecycle_state_ = RuntimeLifecycle::NetworkReady;
    startup_stage_ = StartupStage::NetworkConfigured;

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            ESP_LOGW("lumigrid", "==================================================");
            ESP_LOGW("lumigrid", "  ACCESS POINT READY");
            ESP_LOGW("lumigrid", "  SSID:     %s", wifi_cfg.ap_ssid.c_str());
            ESP_LOGW("lumigrid", "  Security: %s", wifi_cfg.ap_password.empty() ? "open" : "WPA2 configured");
            ESP_LOGW("lumigrid", "  Web UI:   http://" IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGW("lumigrid", "==================================================");
        }
    }


    lifecycle_state_ = RuntimeLifecycle::ServicesStarting;

    DeviceConfig current_cfg;
    config_.load_config(current_cfg);
    dmx_router_.update_config(current_cfg);

    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    uint8_t port_count = (u_count > 0 && u_count <= 4) ? static_cast<uint8_t>(u_count) : (u_count > 4 ? 4 : 0);

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
        [this](const NetworkApplyRequest& req) -> esp_err_t {
            return queue_network_apply(req);
        },
        [this](NetworkApplyState& state, uint32_t& cmd_id, const char*& err_str) {
            state = apply_state_.load();
            cmd_id = pending_apply_command_id_.load();
            err_str = last_apply_error_;
        },
        [this]() -> bool {
            return is_safe_mode();
        }
    );

    api_.set_config_apply_callback([this](const DeviceConfig& old_cfg, const DeviceConfig& new_cfg) -> esp_err_t {
        if (safe_mode_) return ESP_OK;

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

        esp_err_t start_err = node_sync_.start(static_cast<NodeRole>(new_cfg.sync_role));
        if (start_err == ESP_OK) {
            started_subsystems_.node_sync = true;
        }
        return start_err;
    });

    node_sync_.set_transport_provider([this](uint64_t now_ms) {
        return scheduler_.transport_snapshot(now_ms);
    });

    // LG-034 §25: CueRunner-driven status broadcasts. Meaningful runtime
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

    node_sync_.set_remote_transport_callback([this](const ShowTransportSnapshot& remote_snap, uint8_t& out_error) -> bool {
        if (remote_snap.mode == ShowTransportMode::Scene) {
            if (cue_runner_.snapshot(0).running) cue_runner_.stop(CueStopReason::Preempted, clock_.now_ms());
            scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene, remote_snap.bpm, clock_.now_ms());
            if (!config_.scene_exists(remote_snap.active_scene)) {
                out_error = static_cast<uint8_t>(SyncTransportError::SceneMissing);
                return false;
            }
            if (lighting_.active_scene_index() != remote_snap.active_scene) {
                if (!lighting_.activate_scene_checked(remote_snap.active_scene)) {
                    out_error = static_cast<uint8_t>(SyncTransportError::RuntimeUnavailable);
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
            if (remote_snap.timeline_active_checksum != 0 && local_seq.checksum != remote_snap.timeline_active_checksum) {
                out_error = static_cast<uint8_t>(SyncTransportError::TimelineMismatch);
                return false;
            }
            if (cue_runner_.snapshot(0).running) cue_runner_.stop(CueStopReason::Preempted, clock_.now_ms());
            scheduler_.apply_remote_timeline(
                remote_snap.transport_generation,
                remote_snap.timeline_anchor_ms,
                remote_snap.timeline_active_checksum,
                remote_snap.bpm,
                clock_.now_ms()
            );
            return true;
        } else if (remote_snap.mode == ShowTransportMode::Cue) {
            scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Cue, remote_snap.bpm, clock_.now_ms());
            if (remote_snap.cue.list_index >= kMaxCueLists) {
                out_error = static_cast<uint8_t>(SyncTransportError::CueListMissing);
                return false;
            }

            // LG-034 §29: content parity gate. A follower must never run a
            // different Cue List under synchronized transport.
            CueList local_list{};
            if (config_.load_cue_list(remote_snap.cue.list_index, local_list) != ESP_OK || local_list.count == 0) {
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
            cue_snap.paused_from = static_cast<CueRunnerState>(remote_snap.cue_paused_from);
            cue_snap.list_index = remote_snap.cue.list_index;
            cue_snap.current_cue = remote_snap.cue.current_cue;
            cue_snap.next_cue = remote_snap.cue.next_cue;
            cue_snap.cue_anchor_ms = remote_snap.cue.cue_anchor_ms;
            cue_snap.paused_elapsed_ms = remote_snap.cue.paused_elapsed_ms;
            cue_snap.manual_fade_q15 = remote_snap.cue.manual_fade_q15;
            cue_snap.generation = remote_snap.cue.generation;
            cue_snap.list_checksum = remote_snap.cue_list_checksum;
            cue_snap.transition_progress_q15 = remote_snap.cue_transition_progress_q15;
            cue_snap.source_scene = remote_snap.cue_source_scene;

            esp_err_t cue_err = cue_runner_.apply_sync_snapshot(cue_snap, clock_.now_ms());
            if (cue_err != ESP_OK) {
                if (cue_err == ESP_ERR_NOT_FOUND) out_error = static_cast<uint8_t>(SyncTransportError::SceneMissing);
                else if (cue_err == ESP_ERR_INVALID_ARG) out_error = static_cast<uint8_t>(SyncTransportError::CueIndexInvalid);
                else out_error = static_cast<uint8_t>(SyncTransportError::CueListMissing);
                return false;
            }
            return true;
        } else {
            scheduler_.clear_remote_show_mode();
            cue_runner_.clear_remote_sync();
            return true;
        }
    });

    node_sync_.set_transport_callback([this](const ShowTransportSnapshot& snap) {
        if (snap.mode == ShowTransportMode::Timeline) {
            scheduler_.apply_remote_timeline(
                snap.transport_generation,
                snap.timeline_anchor_ms,
                snap.timeline_active_checksum,
                snap.bpm,
                clock_.now_ms()
            );
        } else if (snap.mode == ShowTransportMode::Scene) {
            scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Scene, snap.bpm, clock_.now_ms());
        } else if (snap.mode == ShowTransportMode::Cue) {
            scheduler_.apply_remote_show_mode(Scheduler::RemoteShowMode::Cue, snap.bpm, clock_.now_ms());
        } else {
            scheduler_.clear_remote_show_mode();
        }
    });

    if (!safe_mode_ && !started_subsystems_.scheduler) {
        ESP_LOGI("lumigrid", "Starting scheduler...");
        scheduler_.start(current_cfg.timezone);
        started_subsystems_.scheduler = true;
    }

    start_network_services();

    startup_stage_ = StartupStage::RuntimeStarted;
    services_started_ = true;
    if (!safe_mode_) {
        startup_state_ = StartupState::Ready;
    }
    lifecycle_state_ = RuntimeLifecycle::Running;

    if (OtaBootVerifier::instance().is_pending_verify()) {
        if (startup_state_ != StartupState::Faulted && started_subsystems_.api) {
            ESP_LOGI("lumigrid", "OTA candidate image reached stable state. Confirming image and cancelling rollback...");
            OtaBootVerifier::instance().confirm_candidate();
        }
    }
    return ESP_OK;
}

void LumigridRuntime::stop_network_services() {
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

void LumigridRuntime::start_network_services() {
    DeviceConfig current_cfg{};
    config_.load_config(current_cfg);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    dmx_router_.update_config(current_cfg);

    uint16_t active_unis[16];
    size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
    uint8_t port_count = (u_count > 0 && u_count <= 4) ? static_cast<uint8_t>(u_count) : (u_count > 4 ? 4 : 0);

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

    if (!started_subsystems_.api) {
        ESP_LOGI("lumigrid", "Starting api.start(80, 8080)...");
        api_.set_wifi(&wifi_);
        api_.set_auth_manager(&auth_manager_);
        api_.set_fleet_discovery(&fleet_discovery_);
        api_.set_show_clock_provider([this]() { return clock_.now_ms(); });
        esp_err_t err = api_.start(80, 8080, &config_, &lighting_, &scheduler_, &node_sync_, &dmx_router_, &cue_runner_, &show_transport_);
        if (err == ESP_OK) {
            started_subsystems_.api = true;
        } else {
            ESP_LOGE("lumigrid", "api.start() FAILED: %s", esp_err_to_name(err));
        }
    }

    if (!started_subsystems_.mdns) {
        ESP_LOGI("lumigrid", "Starting mdns.start()...");
        NodeIdentity identity = get_node_identity(current_cfg.device_name);
        esp_err_t err = mdns_.start(identity.id, identity.name, 80, identity);
        if (err == ESP_OK) {
            started_subsystems_.mdns = true;
        }
    }

    if (!started_subsystems_.fleet_discovery) {
        ESP_LOGI("lumigrid", "Starting fleet_discovery.start()...");
        NodeIdentity identity = get_node_identity(current_cfg.device_name);
        esp_err_t err = fleet_discovery_.start(identity);
        if (err == ESP_OK) {
            started_subsystems_.fleet_discovery = true;
        }
    }

    if (!started_subsystems_.artnet) {
        ESP_LOGI("lumigrid", "Starting artnet.start()...");
        esp_err_t err = artnet_.start(
            node,
            [this](const ArtnetDmxPacket& packet) {
                if (!safe_mode_) {
                    dmx_router_.handle_artnet_dmx(packet);
                }
            }
        );
        if (err == ESP_OK) {
            started_subsystems_.artnet = true;
            artnet_.set_output_universes(active_unis, port_count);
        }
    }

    if (!started_subsystems_.sacn) {
        ESP_LOGI("lumigrid", "Starting sacn.start()...");
        esp_err_t err = sacn_.start(
            [this](const SacnDmxPacket& packet) {
                if (!safe_mode_) {
                    dmx_router_.handle_sacn_dmx(packet);
                }
            }
        );
        if (err == ESP_OK) {
            started_subsystems_.sacn = true;
            sacn_.set_subscribed_universes(active_unis, u_count);
        }
    }

    if (current_cfg.sync_enabled && !safe_mode_ && !started_subsystems_.node_sync) {
        ESP_LOGI("lumigrid", "Starting node_sync.start()...");
        node_sync_.start(static_cast<NodeRole>(current_cfg.sync_role));
        started_subsystems_.node_sync = true;
    }

    if (current_cfg.mqtt_enabled && strlen(current_cfg.mqtt_broker) > 0 && !safe_mode_ && !started_subsystems_.mqtt) {
        ESP_LOGI("lumigrid", "Starting mqtt.start()...");
        char broker_uri[128];
        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", current_cfg.mqtt_broker, current_cfg.mqtt_port);
        NodeIdentity id = get_node_identity(current_cfg.device_name);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "lumigrid/%s", id.id);

        if (mqtt_.init(broker_uri, id.id, prefix, &lighting_) == ESP_OK) {
            mqtt_.start();
            started_subsystems_.mqtt = true;
            static MqttService* s_mqtt_ptr = &mqtt_;
            DiagnosticLog::instance().set_stream_callback([](const char* msg, size_t len) {
                if (s_mqtt_ptr) s_mqtt_ptr->publish_log(msg, len);
            });
            mqtt_.set_command_callback([this](const char* payload, size_t len) {
                ESP_LOGI("lumigrid", "Received MQTT command: %.*s", (int)len, payload);
            });
        }
    }
}

esp_err_t LumigridRuntime::queue_network_apply(const NetworkApplyRequest& request) {
    if (apply_in_progress_.exchange(true)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!net_apply_queue_) {
        apply_in_progress_.store(false);
        return ESP_ERR_INVALID_STATE;
    }
    apply_state_.store(NetworkApplyState::Applying);
    pending_apply_command_id_.store(request.command_id);
    last_apply_error_ = nullptr;

    if (xQueueSend(net_apply_queue_, &request, 0) != pdTRUE) {
        apply_in_progress_.store(false);
        apply_state_.store(NetworkApplyState::Idle);
        pending_apply_command_id_.store(0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void LumigridRuntime::net_apply_worker(void* arg) {
    auto* self = static_cast<LumigridRuntime*>(arg);
    NetworkApplyRequest req{};
    while (self) {
        if (xQueueReceive(self->net_apply_queue_, &req, portMAX_DELAY) == pdTRUE) {
            self->execute_network_apply(req);
        }
    }
    vTaskDelete(nullptr);
}

void LumigridRuntime::execute_network_apply(const NetworkApplyRequest& req) {
    ESP_LOGI("lumigrid", "Executing network apply: target mode %s, SSID '%s' (cmd %" PRIu32 ")",
             network_mode_to_string(req.desired.mode), req.desired.sta_ssid, req.command_id);

    // 1. Give HTTP 202 response 200ms to escape the socket
    vTaskDelay(pdMS_TO_TICKS(200));

    // 2. Stop network-dependent services
    stop_network_services();

    // 3. Stop Wi-Fi intentionally
    if (started_subsystems_.wifi) {
        wifi_.stop();
        started_subsystems_.wifi = false;
    }

    apply_state_.store(NetworkApplyState::WaitingForNetwork);

    DeviceConfig dev_cfg{};
    config_.load_config(dev_cfg);

    std::string ap_ssid;
    if (safe_mode_) {
        ap_ssid = "LumiGrid-Recovery";
    } else if (req.desired.ap_name[0] != '\0') {
        ap_ssid = req.desired.ap_name;
    } else if (configuration_valid_ && strlen(dev_cfg.device_name) > 0 && std::strcmp(dev_cfg.device_name, "lumigrid") != 0) {
        ap_ssid = std::string("LumiGrid-") + dev_cfg.device_name;
    } else {
        ap_ssid = "LumiGrid-Factory";
    }

    std::string ap_pass;
#if defined(CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS) && defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS)
    ap_pass = "lumigrid123";
#else
    // LG-036: Recovery AP credential semantics.
    // The MAC-derived passphrase "lg_<mac-fragment>" serves strictly as transitional
    // layer-2 wireless access to reach the node during first-boot commissioning or recovery.
    // NOTE: Recovery AP access password != node authentication boundary.
    // Actual authorization remains strictly gated by AuthManager and physical recovery.
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_pass[32];
    snprintf(mac_pass, sizeof(mac_pass), "lg_%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
    ap_pass = mac_pass;
#endif


    WifiMode target_wifi_mode = WifiMode::ApOnly;
    switch (req.desired.mode) {
        case NetworkMode::StaOnly: target_wifi_mode = WifiMode::StaOnly; break;
        case NetworkMode::StaWithApFallback: target_wifi_mode = WifiMode::StaWithApFallback; break;
        case NetworkMode::ApOnly: default: target_wifi_mode = WifiMode::ApOnly; break;
    }

    WifiConfig wifi_cfg{
        .mode = target_wifi_mode,
        .sta_ssid = req.desired.sta_ssid,
        .sta_password = req.desired.sta_password,
        .ap_ssid = ap_ssid,
        .ap_password = ap_pass,
        .ap_channel = (req.desired.ap_channel >= 1 && req.desired.ap_channel <= 13) ? req.desired.ap_channel : (uint8_t)1,
        .max_retry = 3
    };

    // Recreate readiness semaphore
    {
        SemaphoreHandle_t old_sem = net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
        if (old_sem) vSemaphoreDelete(old_sem);
        net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
    }

    esp_err_t err = wifi_.start(wifi_cfg, [this](WifiState state) {
        if (state == WifiState::Connected) {
            auth_manager_.set_commissioning_state(auth::CommissioningState::None);
        }
        if (state == WifiState::Connected || state == WifiState::ApActive ||
            state == WifiState::FallbackAp || state == WifiState::Failed) {
            SemaphoreHandle_t sem = net_ready_sem_.load(std::memory_order_acquire);
            if (sem) xSemaphoreGive(sem);
        }
    });

    if (err == ESP_OK) {
        started_subsystems_.wifi = true;
        // Wait up to 15s for network to achieve ready state
        xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire), pdMS_TO_TICKS(15000));
    }

    // Determine success
    bool success = false;
    if (started_subsystems_.wifi) {
        if (req.desired.mode == NetworkMode::ApOnly && wifi_.state() == WifiState::ApActive) {
            success = true;
        } else if (req.desired.mode == NetworkMode::StaOnly && wifi_.state() == WifiState::Connected) {
            success = true;
        } else if (req.desired.mode == NetworkMode::StaWithApFallback && wifi_.state() == WifiState::Connected) {
            success = true;
        }
    }

    if (success) {
        ESP_LOGI("lumigrid", "Network apply SUCCESS: desired network active. Persisting configuration.");
        config_.save_network_settings(req.desired);

        start_network_services();

        api_.command_journal().applied(req.command_id);
        last_apply_error_ = nullptr;
        apply_state_.store(NetworkApplyState::Idle);
        pending_apply_command_id_.store(0);
        apply_in_progress_.store(false);
        return;
    }

    // Failure path
    ESP_LOGW("lumigrid", "Network apply FAILED for command %" PRIu32, req.command_id);
    api_.command_journal().failed(req.command_id, ESP_FAIL);
    last_apply_error_ = "sta_connect_failed";

    if (req.desired.mode == NetworkMode::StaWithApFallback && wifi_.is_fallback_ap()) {
        ESP_LOGI("lumigrid", "StaWithApFallback: Recovery AP active at 192.168.4.1. Keeping previous persisted config intact.");
        start_network_services();
        apply_state_.store(NetworkApplyState::Failed);
        pending_apply_command_id_.store(0);
        apply_in_progress_.store(false);
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
        case NetworkMode::StaOnly: prev_wifi_mode = WifiMode::StaOnly; break;
        case NetworkMode::StaWithApFallback: prev_wifi_mode = WifiMode::StaWithApFallback; break;
        case NetworkMode::ApOnly: default: prev_wifi_mode = WifiMode::ApOnly; break;
    }

    std::string prev_ap_name;
    if (req.previous.ap_name[0] != '\0') {
        prev_ap_name = req.previous.ap_name;
    } else {
        prev_ap_name = ap_ssid;
    }

    WifiConfig prev_cfg{
        .mode = prev_wifi_mode,
        .sta_ssid = req.previous.sta_ssid,
        .sta_password = req.previous.sta_password,
        .ap_ssid = prev_ap_name,
        .ap_password = ap_pass,
        .ap_channel = (req.previous.ap_channel >= 1 && req.previous.ap_channel <= 13) ? req.previous.ap_channel : (uint8_t)1,
        .max_retry = 3
    };

    {
        SemaphoreHandle_t old_sem = net_ready_sem_.exchange(nullptr, std::memory_order_acq_rel);
        if (old_sem) vSemaphoreDelete(old_sem);
        net_ready_sem_.store(xSemaphoreCreateBinary(), std::memory_order_release);
    }

    err = wifi_.start(prev_cfg, [this](WifiState state) {
        if (state == WifiState::Connected || state == WifiState::ApActive ||
            state == WifiState::FallbackAp || state == WifiState::Failed) {
            SemaphoreHandle_t sem = net_ready_sem_.load(std::memory_order_acquire);
            if (sem) xSemaphoreGive(sem);
        }
    });

    if (err == ESP_OK) {
        started_subsystems_.wifi = true;
        xSemaphoreTake(net_ready_sem_.load(std::memory_order_acquire), pdMS_TO_TICKS(15000));
    }

    start_network_services();
    apply_state_.store(NetworkApplyState::Failed);
    pending_apply_command_id_.store(0);
    apply_in_progress_.store(false);
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
    st.recovery_network_active = (ws == WifiState::ApActive || ws == WifiState::FallbackAp);
    st.outputs_forced_off = !output_.output_enabled() || output_.is_estop_latched() || is_safe_mode();
    st.ota_state = OtaBootVerifier::instance().boot_state();
    st.restore_state = restore_state_;
    if (configuration_first_error_[0] != '\0') {
        std::strncpy(st.failure_reason, configuration_first_error_, sizeof(st.failure_reason) - 1);
        st.failure_reason[sizeof(st.failure_reason) - 1] = '\0';
    }
    return st;
}

esp_err_t LumigridRuntime::apply_device_config(
    const DeviceConfig& old_cfg,
    const DeviceConfig& new_cfg,
    const ConfigApplyPlan& plan
) {
    if (plan.impact == ConfigApplyImpact::None) {
        return ESP_OK;
    }

    // 1. Output reconfigure if topology changed (pixel count, type, order, enabled channels)
    if (plan.requires_output_reconfigure()) {
        ESP_LOGI("lumigrid", "Output topology change detected. Reconfiguring OutputController...");
        set_maintenance_mode(MaintenanceMode::ConfigApply);
        output_.blackout();

        esp_err_t reconf_err = output_.reconfigure(new_cfg);
        if (reconf_err != ESP_OK) {
            ESP_LOGE("lumigrid", "Output reconfigure failed (%s), rolling back to previous output topology", esp_err_to_name(reconf_err));
            output_.reconfigure(old_cfg);
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
    if (plan.requires_dmx_reconfigure()) {
        dmx_router_.update_config(new_cfg);
        uint16_t active_unis[16];
        size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
        uint8_t port_count = (u_count > 0 && u_count <= 4) ? static_cast<uint8_t>(u_count) : (u_count > 4 ? 4 : 0);
        sacn_.set_subscribed_universes(active_unis, u_count);
        artnet_.set_output_universes(active_unis, port_count);
    }

    // 4. Scheduler restart (e.g. timezone changed)
    if (plan.requires_scheduler_restart()) {
        if (started_subsystems_.scheduler) {
            scheduler_.stop();
            started_subsystems_.scheduler = false;
        }
        scheduler_.start(new_cfg.timezone);
        started_subsystems_.scheduler = true;
    }

    // 5. Sync restart
    if (plan.requires_sync_restart()) {
        if (started_subsystems_.node_sync) {
            node_sync_.stop(1000);
            started_subsystems_.node_sync = false;
        }
        if (new_cfg.sync_enabled && !safe_mode_) {
            esp_err_t sync_err = node_sync_.start(static_cast<NodeRole>(new_cfg.sync_role));
            if (sync_err != ESP_OK) {
                ESP_LOGE("lumigrid", "Sync restart failed (%s), rolling back Sync state", esp_err_to_name(sync_err));
                if (old_cfg.sync_enabled) {
                    node_sync_.start(static_cast<NodeRole>(old_cfg.sync_role));
                    started_subsystems_.node_sync = true;
                }
                return sync_err;
            }
            started_subsystems_.node_sync = true;
        }
    }

    // 6. MQTT restart
    if (plan.requires_mqtt_restart()) {
        if (started_subsystems_.mqtt) {
            DiagnosticLog::instance().set_stream_callback(nullptr);
            mqtt_.stop();
            started_subsystems_.mqtt = false;
        }
        if (new_cfg.mqtt_enabled && strlen(new_cfg.mqtt_broker) > 0 && !safe_mode_) {
            char broker_uri[128];
            snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", new_cfg.mqtt_broker, new_cfg.mqtt_port);
            NodeIdentity id = get_node_identity(new_cfg.device_name);
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "lumigrid/%s", id.id);
            if (mqtt_.init(broker_uri, id.id, prefix, &lighting_) == ESP_OK) {
                mqtt_.start();
                started_subsystems_.mqtt = true;
            }
        }
    }

    // 7. Check certification invalidation on hardware-relevant config changes (LG-037)
    cert_service_.check_invalidation(new_cfg, api_.identity().hw_revision, api_.identity().fw_version);

    return ESP_OK;
}

} // namespace lg

