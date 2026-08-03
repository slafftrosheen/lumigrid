#include "esp_log.h"
#include <cstring>
#include "esp_mac.h"
#include "esp_netif.h"

#include "FactoryApi.hpp"
#include "FactoryRunner.hpp"
#include "OutputController.hpp"
#include "WifiService.hpp"
#include "MdnsService.hpp"
#include "ArtnetDiscovery.hpp"
#include "FactoryTypes.hpp"

// Signage components
#include "ConfigStore.hpp"
#include "SignageEngine.hpp"
#include "Scheduler.hpp"
#include "NodeSync.hpp"

extern "C" void app_main(void) {
    // All service objects are static so they survive app_main() return
    // and remain valid for the WiFi callback lambda that captures them by reference.
    static lg::OutputController output;
    static lg::ConfigStore config;
    static lg::FactoryRunner runner(output);
    static lg::FactoryApi api(runner);
    static lg::WifiService wifi;
    static lg::MdnsService mdns;
    static lg::ArtnetDiscovery artnet;
    static lg::SignageEngine signage(output, config);
    static lg::Scheduler scheduler(config, signage);
    static lg::NodeSync node_sync(signage, config);

    ESP_LOGI("lumigrid", "=== BOOT START ===");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Let serial console connect

    // Step 1: NVS config store — must come before output to load power settings
    ESP_LOGI("lumigrid", "Step 1: config.init()...");
    esp_err_t err = config.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "config.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "config.init() OK");

    // Step 2: Hardware output
    ESP_LOGI("lumigrid", "Step 2: output.init()...");
    err = output.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "output.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "output.init() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 3: Signage engine — renders active preset IMMEDIATELY (before network)
    ESP_LOGI("lumigrid", "Step 3: signage.start() (renders from NVS preset before network)...");
    err = signage.start();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "signage.start() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "signage.start() OK — sign is lit");

    // Step 4: Factory test runner (coexists with signage engine)
    ESP_LOGI("lumigrid", "Step 4: runner.start()...");
    err = runner.start();
    if (err != ESP_OK) ESP_LOGE("lumigrid", "runner.start() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid", "runner.start() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Load device config for WiFi and service settings
    lg::DeviceConfig dev_cfg;
    config.load_config(dev_cfg);

    lg::WifiConfig wifi_cfg{
        .mode = lg::WifiMode::ApOnly,
        .sta_ssid = "",
        .sta_password = "",
        .ap_ssid = "LumiGrid-Factory",
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

                ESP_LOGI("lumigrid", "Step 6: api.start(80)...");
                ESP_ERROR_CHECK(api.start(80, &config, &signage));

                ESP_LOGI("lumigrid", "Step 7: mdns.start()...");
                ESP_ERROR_CHECK(mdns.start(dev_cfg.device_name, "LumiGrid Signage", 80));

                ESP_LOGI("lumigrid", "Step 8: artnet.start()...");
                ESP_ERROR_CHECK(artnet.start(
                    node,
                    [&](const lg::ArtnetDmxPacket& packet) {
                        if (packet.universe != 0) return;

                        signage.feed_watchdog();
                        signage.set_stream_override(true);

                        lg::FactoryCommand command{};
                        command.type = lg::FactoryCommandType::StreamFrame;
                        command.brightness = 64;
                        command.duration_ms = 1000;

                        for (uint8_t lane = 0; lane < 8; ++lane) {
                            const size_t offset = lane * 3;
                            if (offset + 2 >= packet.length) break;

                            command.stream.lane_rgb[lane][0] = packet.data[offset + 0];
                            command.stream.lane_rgb[lane][1] = packet.data[offset + 1];
                            command.stream.lane_rgb[lane][2] = packet.data[offset + 2];
                        }

                        runner.enqueue(command);
                    }
                ));

                // Step 9: NTP scheduler
                ESP_LOGI("lumigrid", "Step 9: scheduler.start()...");
                scheduler.start(dev_cfg.timezone);

                // Step 10: Multi-node sync (if enabled)
                if (dev_cfg.sync_enabled) {
                    ESP_LOGI("lumigrid", "Step 10: node_sync.start()...");
                    node_sync.start(static_cast<lg::NodeRole>(dev_cfg.sync_role));
                }

                ESP_LOGI("lumigrid", "=== ALL SERVICES UP ===");
            }
        }
    ));

    ESP_LOGI("lumigrid", "Boot complete, sign is running");
}
