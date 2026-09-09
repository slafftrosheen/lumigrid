// components/protocols/include/ArtnetDiscovery.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <array>
#include <mutex>

#include "esp_err.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace lg {

/// Decode a 15-bit Art-Net 4 Port-Address into Net (bits 14..8), Sub-Net (bits 7..4), and Universe (bits 3..0).
inline void decode_port_address(uint16_t port_address, uint8_t& net, uint8_t& subnet, uint8_t& universe_id) {
    net = static_cast<uint8_t>((port_address >> 8) & 0x7F);
    subnet = static_cast<uint8_t>((port_address >> 4) & 0x0F);
    universe_id = static_cast<uint8_t>(port_address & 0x0F);
}

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
    uint8_t num_ports = 1;
    std::array<uint8_t, 4> sw_out = {0, 0, 0, 0};
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
    uint32_t source_ip = 0;
    uint8_t data[512] = {};
};

using ArtnetDmxCallback = std::function<void(const ArtnetDmxPacket&)>;

class ArtnetDiscovery {
public:
    esp_err_t start(const ArtnetNodeInfo& info, ArtnetDmxCallback dmx_callback);

    /// Update reported output universes for ArtPoll replies using full 15-bit Port-Address decoding
    void set_output_universes(const uint16_t* universes, uint8_t count);

private:
    static void task_entry(void* arg);
    void task();
    bool handle_poll(const uint8_t* packet, size_t length, const sockaddr_in& source, int sock);
    bool handle_dmx(const uint8_t* packet, size_t length, const sockaddr_in& source);
    void send_poll_reply(const sockaddr_in& destination, int sock);

    ArtnetNodeInfo info_{};
    ArtnetDmxCallback dmx_callback_{};
    std::mutex mtx_;
};

} // namespace lg
