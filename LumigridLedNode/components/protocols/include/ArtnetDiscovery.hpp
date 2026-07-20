// components/protocols/include/ArtnetDiscovery.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace lg {

struct ArtnetNodeInfo {
    std::string short_name = "LumiGrid Factory";
    std::string long_name = "LumiGrid Factory Test Node";
    std::string report = "#0001 [0000] LumiGrid ready";
    uint16_t oem = 0xFFFF;
    uint16_t esta = 0x0000;
    uint16_t version = 1;
    uint8_t net = 0;
    uint8_t subnet = 0;
    uint8_t universe = 0;
    uint8_t mac[6] = {0};
    uint8_t ip[4] = {0};
    uint8_t status1 = 0xD2;
    uint8_t status2 = 0x08;
    uint8_t style = 0x00;
};

struct ArtnetDmxPacket {
    uint16_t universe = 0;
    uint16_t length = 0;
    uint8_t sequence = 0;
    uint8_t data[512] = {};
};

using ArtnetDmxCallback = std::function<void(const ArtnetDmxPacket&)>;

class ArtnetDiscovery {
public:
    esp_err_t start(const ArtnetNodeInfo& info, ArtnetDmxCallback dmx_callback);

private:
    static void task_entry(void* arg);
    void task();
    bool handle_poll(const uint8_t* packet, size_t length, const sockaddr_in& source, int sock);
    bool handle_dmx(const uint8_t* packet, size_t length);
    void send_poll_reply(const sockaddr_in& destination, int sock);

    ArtnetNodeInfo info_{};
    ArtnetDmxCallback dmx_callback_{};
};

} // namespace lg
