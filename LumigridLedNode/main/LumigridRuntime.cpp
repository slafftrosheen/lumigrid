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
        sacn_.set_subscribed_universes(active_unis, u_count);
        artnet_.set_output_universes(active_unis, static_cast<uint8_t>(u_count));
    });
}

void LumigridRuntime::recovery_task(void* arg) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int hold_ticks = 0;
    while (1) {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            hold_ticks++;
            if (hold_ticks >= 50) { // 50 * 100ms = 5 seconds
                ESP_LOGE("lumigrid", "FACTORY RESET INITIATED VIA HARDWARE BUTTON");
                nvs_handle_t handle;
                if (nvs_open("lumigrid", NVS_READWRITE, &handle) == ESP_OK) {
                    nvs_erase_all(handle);
                    nvs_commit(handle);
                    nvs_close(handle);
                }
                esp_restart();
            }
        } else {
            hold_ticks = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t LumigridRuntime::init() {
    DiagnosticLog::instance().init();

    if (esp_reset_reason() == ESP_RST_POWERON) {
        s_boot_count = 0;
    }
    s_boot_count++;
    
    safe_mode_ = (s_boot_count >= 3);

    xTaskCreate(recovery_task, "recovery", 4096, NULL, 5, NULL);

    ESP_LOGI("lumigrid", "=== BOOT START ===");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("lumigrid", "Step 1: Hardware output init...");
    esp_err_t err = output_.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "output_.init() FAILED: %s", esp_err_to_name(err));

    ESP_LOGI("lumigrid", "Step 1.1: source_manager.init()...");
    err = source_manager_.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "source_manager_.init() FAILED: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI("lumigrid", "Step 2: config.init()...");
    err = config_.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "config_.init() FAILED: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(500));

    DeviceConfig dev_cfg;
    if (config_.load_config(dev_cfg) == ESP_OK) {
        api::update_tokens(dev_cfg.admin_token, dev_cfg.installer_token, dev_cfg.operator_token);
    }

    // Run Stage 2 Runtime Hardening Verification (Items 2, 16, 21, 22)
    run_runtime_hardening_tests();

    if (!safe_mode_) {
        ESP_LOGI("lumigrid", "Step 3: lighting.start()...");
        err = lighting_.start();
        if (err != ESP_OK) ESP_LOGE("lumigrid", "lighting.start() FAILED: %s", esp_err_to_name(err));
    } else {
        ESP_LOGW("lumigrid", "SAFE MODE ACTIVE: Skipping signage processing");
    }

    ESP_LOGI("lumigrid", "Step 4: factory_runner.start()...");
    err = factory_runner_.start();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "factory_runner.start() FAILED: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;
}

esp_err_t LumigridRuntime::start() {
    DeviceConfig dev_cfg;
    config_.load_config(dev_cfg);

    WifiConfig wifi_cfg{
        .mode = WifiMode::ApOnly,
        .sta_ssid = "",
        .sta_password = "",
        .ap_ssid = safe_mode_ ? "LumiGrid-Recovery" : "LumiGrid-Factory",
        .ap_password = "lumigrid123",
        .max_retry = 1
    };

    ESP_LOGI("lumigrid", "Step 5: wifi.start()...");
    ESP_ERROR_CHECK(wifi_.start(
        wifi_cfg,
        [this](WifiState state) {
            if (state == WifiState::Connected || state == WifiState::ApActive) {
                static bool services_started = false;
                if (services_started) return;
                services_started = true;

                esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                if (ap_netif) {
                    esp_netif_ip_info_t ip_info;
                    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
                        ESP_LOGI("lumigrid",
                            "=== UI READY === Connect to 'LumiGrid-Factory' then open http://" IPSTR " ===",
                            IP2STR(&ip_info.ip));
                    }
                }

                uint8_t mac[6] = {};
                esp_read_mac(mac, ESP_MAC_WIFI_STA);

                DeviceConfig current_cfg;
                config_.load_config(current_cfg);
                dmx_router_.update_config(current_cfg);

                uint16_t active_unis[16];
                size_t u_count = dmx_router_.get_active_universes(active_unis, 16);
                uint8_t port_count = (u_count > 0 && u_count <= 4) ? static_cast<uint8_t>(u_count) : 1;

                ArtnetNodeInfo node{};
                node.short_name = "LumiGrid Factory";
                node.long_name = "LumiGrid 8xALED 8xPWM Factory Test Node";
                node.report = "#0001 [0000] LumiGrid ready";
                node.num_ports = port_count;
                for (uint8_t i = 0; i < port_count; ++i) {
                    node.sw_out[i] = static_cast<uint8_t>(active_unis[i] & 0x0F);
                }
                std::memcpy(node.mac, mac, 6);

                ESP_LOGI("lumigrid", "Step 6: api.start(80, 8080)...");
                ESP_ERROR_CHECK(api_.start(80, 8080, &config_, &lighting_, &scheduler_, &node_sync_));

                ESP_LOGI("lumigrid", "Step 7: mdns.start()...");
                ESP_ERROR_CHECK(mdns_.start(current_cfg.device_name, "LumiGrid Signage", 80));

                ESP_LOGI("lumigrid", "Step 8: artnet.start()...");
                ESP_ERROR_CHECK(artnet_.start(
                    node,
                    [this](const ArtnetDmxPacket& packet) {
                        dmx_router_.handle_artnet_dmx(packet);
                    }
                ));
                artnet_.set_output_universes(active_unis, port_count);

                ESP_LOGI("lumigrid", "Step 8.5: sacn.start()...");
                ESP_ERROR_CHECK(sacn_.start(
                    [this](const SacnDmxPacket& packet) {
                        dmx_router_.handle_sacn_dmx(packet);
                    }
                ));
                sacn_.set_subscribed_universes(active_unis, u_count);

                if (!safe_mode_) {
                    ESP_LOGI("lumigrid", "Step 9: scheduler.start()...");
                    scheduler_.start(current_cfg.timezone);
                }

                if (current_cfg.sync_enabled && !safe_mode_) {
                    ESP_LOGI("lumigrid", "Step 10: node_sync.start()...");
                    node_sync_.start(static_cast<NodeRole>(current_cfg.sync_role));
                }

                if (current_cfg.mqtt_enabled && strlen(current_cfg.mqtt_broker) > 0 && !safe_mode_) {
                    ESP_LOGI("lumigrid", "Step 11: mqtt.start()...");
                    char broker_uri[128];
                    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", current_cfg.mqtt_broker, current_cfg.mqtt_port);
                    char prefix[64];
                    snprintf(prefix, sizeof(prefix), "lumigrid/%s", current_cfg.device_name);

                    if (mqtt_.init(broker_uri, current_cfg.device_name, prefix, &lighting_) == ESP_OK) {
                        mqtt_.start();
                        
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
        }
    ));

    return ESP_OK;
}

} // namespace lg
