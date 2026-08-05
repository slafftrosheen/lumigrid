#include "esp_log.h"
#include <cstring>
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "DiagnosticLog.hpp"
#include "esp_mac.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "FactoryApi.hpp"
#include "FactoryRunner.hpp"
#include "OutputController.hpp"
#include "WifiService.hpp"
#include "MdnsService.hpp"
#include "ArtnetDiscovery.hpp"
#include "SacnReceiver.hpp"
#include "FactoryTypes.hpp"
#include "MqttService.hpp"

#include "OutputArbitrator.hpp"
#include "ConfigStore.hpp"
#include "SignageEngine.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"

RTC_DATA_ATTR static int s_boot_count = 0;

static void recovery_task(void* arg) {
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

extern "C" void app_main(void) {
    lg::DiagnosticLog::instance().init();

    // Reset boot count if this is a normal power-on
    if (esp_reset_reason() == ESP_RST_POWERON) {
        s_boot_count = 0;
    }
    s_boot_count++;
    
    bool safe_mode = (s_boot_count >= 3);

    xTaskCreate(recovery_task, "recovery", 2048, NULL, 5, NULL);

    // All service objects are static so they survive app_main() return
    // and remain valid for the WiFi callback lambda that captures them by reference.
    static lg::OutputController output;
    static lg::OutputArbitrator arbitrator(output);
    static lg::ConfigStore config;
    static lg::FactoryRunner runner(arbitrator);
    static lg::FactoryApi api(runner);
    static lg::WifiService wifi;
    static lg::MdnsService mdns;
    static lg::ArtnetDiscovery artnet;
    static lg::SacnReceiver sacn;
    static lg::SignageEngine signage(arbitrator, config);
    static lg::ClockDiscipline clock;
    static lg::Scheduler scheduler(config, signage, clock);
    static lg::NodeSync node_sync(signage, config, clock);
    static lg::MqttService mqtt;

    ESP_LOGI("lumigrid", "=== BOOT START ===");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Let serial console connect

    ESP_LOGI("lumigrid", "Step 1: Hardware output init (deterministic blackout)...");
    esp_err_t err = output.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "output.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "output.init() OK");
    
    ESP_LOGI("lumigrid", "Step 1.1: arbitrator.init()...");
    err = arbitrator.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "arbitrator.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "arbitrator.init() OK");
    vTaskDelay(pdMS_TO_TICKS(100));

    // Step 2: NVS config store
    ESP_LOGI("lumigrid", "Step 2: config.init()...");
    err = config.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "config.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "config.init() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    if (!safe_mode) {
        // Step 3: Signage engine — renders active preset IMMEDIATELY (before network)
        ESP_LOGI("lumigrid", "Step 3: signage.start() (renders from NVS preset before network)...");
        err = signage.start();
        if (err != ESP_OK) ESP_LOGE("lumigrid", "signage.start() FAILED: %s", esp_err_to_name(err));
        else ESP_LOGI("lumigrid", "signage.start() OK — sign is lit");
    } else {
        ESP_LOGW("lumigrid", "SAFE MODE ACTIVE: Skipping signage processing");
    }

    // Step 4: Factory test runner (coexists with signage engine)
    ESP_LOGI("lumigrid", "Step 4: runner.start()...");
    err = runner.start();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "runner.start() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "runner.start() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Load device config for WiFi and service settings
    static lg::DeviceConfig dev_cfg;
    config.load_config(dev_cfg);

    lg::WifiConfig wifi_cfg{
        .mode = lg::WifiMode::ApOnly,
        .sta_ssid = "",
        .sta_password = "",
        .ap_ssid = safe_mode ? "LumiGrid-Recovery" : "LumiGrid-Factory",
        .ap_password = "lumigrid123",
        .max_retry = 1
    };

    ESP_LOGI("lumigrid", "Step 5: wifi.start() (AP: LumiGrid-Factory / lumigrid123)...");
    ESP_ERROR_CHECK(wifi.start(
        wifi_cfg,
        [&](lg::WifiState state) {
            if (state == lg::WifiState::Connected || state == lg::WifiState::ApActive) {
                static bool services_started = false;
                if (services_started) return;
                services_started = true;

                // Log AP IP so device is always reachable by raw IP
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

                lg::ArtnetNodeInfo node{};
                node.short_name = "LumiGrid Factory";
                node.long_name = "LumiGrid 8xALED 8xPWM Factory Test Node";
                node.report = "#0001 [0000] LumiGrid ready";
                std::memcpy(node.mac, mac, 6);

                ESP_LOGI("lumigrid", "Step 6: api.start(80, 8080)...");
                ESP_ERROR_CHECK(api.start(80, 8080, &config, &signage, &scheduler, &node_sync));

                ESP_LOGI("lumigrid", "Step 7: mdns.start()...");
                ESP_ERROR_CHECK(mdns.start(dev_cfg.device_name, "LumiGrid Signage", 80));

                ESP_LOGI("lumigrid", "Step 8: artnet.start()...");
                ESP_ERROR_CHECK(artnet.start(
                    node,
                    [&](const lg::ArtnetDmxPacket& packet) {
                        lg::DeviceConfig cfg;
                        config.load_config(cfg);

                        if (!cfg.protocol.artnet_enabled) return;
                        if (packet.universe != cfg.protocol.universe_start) return;

                        // Map 3 DMX channels (RGB) per zone to live stream layer
                        uint8_t live_layer = cfg.protocol.live_stream_layer < 3 ? cfg.protocol.live_stream_layer : 2;
                        for (uint8_t zone = 0; zone < 8; ++zone) {
                            const size_t offset = zone * 3;
                            if (offset + 2 >= packet.length) break;

                            lg::BrandColor color{
                                packet.data[offset + 0],
                                packet.data[offset + 1],
                                packet.data[offset + 2]
                            };
                            
                            signage.inject_live_data(zone, live_layer, color, 255);
                        }
                    }
                ));

                ESP_LOGI("lumigrid", "Step 8.1: sacn.start()...");
                ESP_ERROR_CHECK(sacn.start(
                    [&](const lg::SacnDmxPacket& packet) {
                        lg::DeviceConfig cfg;
                        config.load_config(cfg);

                        if (!cfg.protocol.sacn_enabled) return;
                        if (packet.universe != cfg.protocol.universe_start) return;

                        uint8_t live_layer = cfg.protocol.live_stream_layer < 3 ? cfg.protocol.live_stream_layer : 2;
                        for (uint8_t zone = 0; zone < 8; ++zone) {
                            const size_t offset = zone * 3;
                            if (offset + 2 >= packet.length) break;

                            lg::BrandColor color{
                                packet.data[offset + 0],
                                packet.data[offset + 1],
                                packet.data[offset + 2]
                            };
                            
                            signage.inject_live_data(zone, live_layer, color, 255);
                        }
                    }
                ));

                // Step 9: NTP scheduler
                if (!safe_mode) {
                    ESP_LOGI("lumigrid", "Step 9: scheduler.start()...");
                    scheduler.start(dev_cfg.timezone);
                }

                // Step 10: Multi-node sync (if enabled)
                if (dev_cfg.sync_enabled && !safe_mode) {
                    ESP_LOGI("lumigrid", "Step 10: node_sync.start()...");
                    node_sync.start(static_cast<lg::NodeRole>(dev_cfg.sync_role));
                }

                // Step 11: MQTT telemetry
                if (dev_cfg.mqtt_enabled && strlen(dev_cfg.mqtt_broker) > 0 && !safe_mode) {
                    ESP_LOGI("lumigrid", "Step 11: mqtt.start()...");
                    char broker_uri[128];
                    snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", dev_cfg.mqtt_broker, dev_cfg.mqtt_port);
                    char prefix[64];
                    snprintf(prefix, sizeof(prefix), "lumigrid/%s", dev_cfg.device_name);

                    if (mqtt.init(broker_uri, dev_cfg.device_name, prefix, &signage) == ESP_OK) {
                        mqtt.start();
                        
                        // Register DiagnosticLog stream callback
                        lg::DiagnosticLog::instance().set_stream_callback([](const char* msg, size_t len) {
                            mqtt.publish_log(msg, len);
                        });
                        
                        // Register command callback
                        mqtt.set_command_callback([&](const char* payload, size_t len) {
                            // Example: parse command to change preset
                            // Just logging for now
                            ESP_LOGI("lumigrid", "Received MQTT command: %.*s", (int)len, payload);
                        });
                    }
                }

                ESP_LOGI("lumigrid", "=== ALL SERVICES UP ===");
                
                // Clear boot count after successful boot sequence
                s_boot_count = 0;
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
    ));

    ESP_LOGI("lumigrid", "Boot complete, sign is running");
}
