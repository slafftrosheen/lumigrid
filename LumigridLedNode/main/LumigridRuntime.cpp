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
#include <cstring>

namespace lg {

RTC_DATA_ATTR static int s_boot_count = 0;

LumigridRuntime::LumigridRuntime()
    : source_manager_(),
      factory_runner_(source_manager_, output_),
      api_(factory_runner_),
      lighting_(source_manager_, output_, config_),
      dmx_router_(lighting_),
      clock_(),
      scheduler_(config_, lighting_, clock_),
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

                nvs_handle_t handle;
                if (nvs_open("lumigrid", NVS_READWRITE, &handle) == ESP_OK) {
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

    DeviceConfig dev_cfg;
    if (config_.load_config(dev_cfg) == ESP_OK) {
        startup_stage_ = StartupStage::ConfigLoaded;
        api::update_tokens(dev_cfg.admin_token, dev_cfg.installer_token, dev_cfg.operator_token);

        ConfigStore::ConfigValidationResult val_res;
        esp_err_t val_err = config_.validate_config(dev_cfg, &val_res);
        if (val_err != ESP_OK) {
            safe_mode_ = true;
            startup_state_ = StartupState::SafeMode;
            failing_stage_ = StartupStage::ConfigValidated;
            failing_error_ = val_err;
            configuration_valid_ = false;
            configuration_error_count_ = val_res.error_count;
            std::strncpy(configuration_first_error_, val_res.first_error, sizeof(configuration_first_error_));
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
        std::strncpy(configuration_first_error_, "Failed to load device config from NVS", sizeof(configuration_first_error_));
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
    vTaskDelay(pdMS_TO_TICKS(500));

    return (startup_state_ == StartupState::Faulted) ? failing_error_ : ESP_OK;
}

void LumigridRuntime::rollback_services(StartupStage failed_stage, esp_err_t error) {
    ESP_LOGW("lumigrid", "Rolling back runtime services due to failure at stage %s: %s",
             startup_stage_to_name(failed_stage), esp_err_to_name(error));

    // Reverse-order teardown: only stop services that actually started
    if (started_subsystems_.mqtt) {
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
    if (started_subsystems_.api) {
        api_.stop();
        started_subsystems_.api = false;
    }
    if (started_subsystems_.wifi) {
        wifi_.stop();
        started_subsystems_.wifi = false;
    }
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

    DeviceConfig dev_cfg;
    config_.load_config(dev_cfg);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    std::string ap_pass;
#if defined(CONFIG_LUMIGRID_ENABLE_DEV_CREDENTIALS)
    ap_pass = "lumigrid123";
#else
    if (strlen(dev_cfg.installer_token) >= 8) {
        ap_pass = dev_cfg.installer_token;
    } else {
        char mac_pass[32];
        snprintf(mac_pass, sizeof(mac_pass), "lg_%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
        ap_pass = mac_pass;
    }
#endif

    WifiConfig wifi_cfg{
        .mode = WifiMode::ApOnly,
        .sta_ssid = "",
        .sta_password = "",
        .ap_ssid = safe_mode_ ? "LumiGrid-Recovery" : "LumiGrid-Factory",
        .ap_password = ap_pass,
        .max_retry = 1
    };

    if (!net_ready_sem_) {
        net_ready_sem_ = xSemaphoreCreateBinary();
    } else {
        xSemaphoreTake(net_ready_sem_, 0); // Drain stale token
    }

    startup_state_ = StartupState::StartingNetwork;
    ESP_LOGI("lumigrid", "Step 5: wifi.start()...");
    esp_err_t err = wifi_.start(
        wifi_cfg,
        [this](WifiState state) {
            if (state == WifiState::Connected || state == WifiState::ApActive) {
                if (net_ready_sem_) {
                    xSemaphoreGive(net_ready_sem_);
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
    if (xSemaphoreTake(net_ready_sem_, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE("lumigrid", "Network readiness timed out after 10s");
        rollback_services(StartupStage::NetworkConfigured, ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    lifecycle_state_ = RuntimeLifecycle::NetworkReady;
    startup_stage_ = StartupStage::NetworkConfigured;

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            ESP_LOGI("lumigrid",
                "=== UI READY === Connect to '%s' then open http://" IPSTR " ===",
                wifi_cfg.ap_ssid.c_str(),
                IP2STR(&ip_info.ip));
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

    ESP_LOGI("lumigrid", "Step 6: api.start(80, 8080)...");
    err = api_.start(80, 8080, &config_, &lighting_, &scheduler_, &node_sync_, &dmx_router_);
    if (err != ESP_OK) {
        ESP_LOGE("lumigrid", "api.start() FAILED: %s", esp_err_to_name(err));
        rollback_services(StartupStage::NetworkConfigured, err);
        return err;
    }
    started_subsystems_.api = true;

    ESP_LOGI("lumigrid", "Step 7: mdns.start()...");
    err = mdns_.start(current_cfg.device_name, "LumiGrid Signage", 80);
    if (err != ESP_OK) {
        ESP_LOGW("lumigrid", "mdns.start() returned %s (non-fatal)", esp_err_to_name(err));
    } else {
        started_subsystems_.mdns = true;
    }

    ESP_LOGI("lumigrid", "Step 8: artnet.start()...");
    err = artnet_.start(
        node,
        [this](const ArtnetDmxPacket& packet) {
            if (!safe_mode_) {
                dmx_router_.handle_artnet_dmx(packet);
            }
        }
    );
    if (err != ESP_OK) {
        ESP_LOGE("lumigrid", "artnet.start() FAILED: %s", esp_err_to_name(err));
        rollback_services(StartupStage::NetworkConfigured, err);
        return err;
    }
    started_subsystems_.artnet = true;
    artnet_.set_output_universes(active_unis, port_count);

    ESP_LOGI("lumigrid", "Step 8.5: sacn.start()...");
    err = sacn_.start(
        [this](const SacnDmxPacket& packet) {
            if (!safe_mode_) {
                dmx_router_.handle_sacn_dmx(packet);
            }
        }
    );
    if (err != ESP_OK) {
        ESP_LOGE("lumigrid", "sacn.start() FAILED: %s", esp_err_to_name(err));
        rollback_services(StartupStage::ProtocolsStarted, err);
        return err;
    }
    started_subsystems_.sacn = true;
    sacn_.set_subscribed_universes(active_unis, u_count);
    startup_stage_ = StartupStage::ProtocolsStarted;

    if (!safe_mode_) {
        ESP_LOGI("lumigrid", "Step 9: scheduler.start()...");
        scheduler_.start(current_cfg.timezone);
        started_subsystems_.scheduler = true;
    }

    if (current_cfg.sync_enabled && !safe_mode_) {
        ESP_LOGI("lumigrid", "Step 10: node_sync.start()...");
        node_sync_.start(static_cast<NodeRole>(current_cfg.sync_role));
        started_subsystems_.node_sync = true;
    }

    if (current_cfg.mqtt_enabled && strlen(current_cfg.mqtt_broker) > 0 && !safe_mode_) {
        ESP_LOGI("lumigrid", "Step 11: mqtt.start()...");
        char broker_uri[128];
        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", current_cfg.mqtt_broker, current_cfg.mqtt_port);
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "lumigrid/%s", current_cfg.device_name);

        if (mqtt_.init(broker_uri, current_cfg.device_name, prefix, &lighting_) == ESP_OK) {
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

    startup_stage_ = StartupStage::RuntimeStarted;
    services_started_ = true;
    if (!safe_mode_) {
        startup_state_ = StartupState::Ready;
    }
    lifecycle_state_ = RuntimeLifecycle::Running;
    return ESP_OK;
}

} // namespace lg
