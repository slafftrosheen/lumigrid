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

extern "C" void app_main(void) {
    static lg::OutputController output;
    static lg::FactoryRunner runner(output);
    static lg::FactoryApi api(runner);
    static lg::WifiService wifi;
    static lg::MdnsService mdns;
    static lg::ArtnetDiscovery artnet;

    ESP_LOGI("lumigrid-factory", "=== BOOT START ===");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Let serial console connect

    ESP_LOGI("lumigrid-factory", "Step 1: output.init()...");
    esp_err_t err = output.init();
    if (err != ESP_OK) ESP_LOGE("lumigrid-factory", "output.init() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid-factory", "output.init() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI("lumigrid-factory", "Step 2: output.blackout()...");
    err = output.blackout();
    if (err != ESP_OK) ESP_LOGE("lumigrid-factory", "output.blackout() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid-factory", "output.blackout() OK");
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI("lumigrid-factory", "Step 3: runner.start()...");
    err = runner.start();
    if (err != ESP_OK) ESP_LOGE("lumigrid-factory", "runner.start() FAILED: %s", esp_err_to_name(err));
    else ESP_LOGI("lumigrid-factory", "runner.start() OK — auto-loop should be driving LEDs now");
    vTaskDelay(pdMS_TO_TICKS(500));

    lg::WifiConfig wifi_cfg{
        .mode = lg::WifiMode::ApOnly,
        .sta_ssid = "YOUR_WIFI",
        .sta_password = "YOUR_PASS",
        .ap_ssid = "LumiGrid-Factory",
        .ap_password = "lumigrid123",
        .max_retry = 1
    };

    ESP_LOGI("lumigrid-factory", "Step 4: wifi.start() (AP: LumiGrid-Factory / lumigrid123)...");
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
                        ESP_LOGI("lumigrid-factory",
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

                ESP_LOGI("lumigrid-factory", "Step 5: api.start(80)...");
                ESP_ERROR_CHECK(api.start(80));
                ESP_LOGI("lumigrid-factory", "Step 6: mdns.start()...");
                ESP_ERROR_CHECK(mdns.start("lumigrid-factory", "LumiGrid Factory", 80));

                ESP_LOGI("lumigrid-factory", "Step 7: artnet.start()...");
                ESP_ERROR_CHECK(artnet.start(
                    node,
                    [&](const lg::ArtnetDmxPacket& packet) {
                        if (packet.universe != 0) return;

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

                ESP_LOGI("lumigrid-factory", "=== ALL SERVICES UP ===");
            }
        }
    ));

    ESP_LOGI("lumigrid-factory", "Boot complete, waiting for network events");
}
