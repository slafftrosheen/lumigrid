#include "esp_log.h"
#include "esp_mac.h"

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

    ESP_ERROR_CHECK(output.init());
    ESP_ERROR_CHECK(output.blackout());
    ESP_ERROR_CHECK(runner.start());

    lg::WifiConfig wifi_cfg{
        .mode = lg::WifiMode::StaWithApFallback,
        .sta_ssid = "YOUR_WIFI",
        .sta_password = "YOUR_PASS",
        .ap_ssid = "LumiGrid-Factory",
        .ap_password = "lumigrid123",
        .max_retry = 10
    };

    ESP_ERROR_CHECK(wifi.start(
        wifi_cfg,
        [&](lg::WifiState state) {
            if (state == lg::WifiState::Connected || state == lg::WifiState::ApActive) {
                static bool services_started = false;
                if (services_started) return;
                services_started = true;

                uint8_t mac[6] = {};
                esp_read_mac(mac, ESP_MAC_WIFI_STA);

                lg::ArtnetNodeInfo node{};
                node.short_name = "LumiGrid Factory";
                node.long_name = "LumiGrid 8xALED 8xPWM Factory Test Node";
                node.report = "#0001 [0000] LumiGrid ready";
                std::memcpy(node.mac, mac, 6);

                ESP_ERROR_CHECK(api.start(80));
                ESP_ERROR_CHECK(mdns.start("lumigrid-factory", "LumiGrid Factory", 80));

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
                        // No need to manually call api.notify_status_async() here because 
                        // runner.enqueue -> runner processes it -> calls status_callback_ automatically
                    }
                ));
            }
        }
    ));

    ESP_LOGI("lumigrid-factory", "Boot complete, waiting for network");
}
