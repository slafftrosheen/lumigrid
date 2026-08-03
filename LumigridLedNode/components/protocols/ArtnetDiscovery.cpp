// components/protocols/ArtnetDiscovery.cpp
#include "ArtnetDiscovery.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include "esp_netif.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace lg {
namespace {
constexpr char kTag[] = "artnet";
constexpr uint16_t kArtnetPort = 6454;
constexpr uint16_t kOpPoll = 0x2000;
constexpr uint16_t kOpPollReply = 0x2100;
constexpr uint16_t kOpDmx = 0x5000;

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>(p[1]) |
           (static_cast<uint16_t>(p[0]) << 8);
}

void write_le16(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>(val & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

void copy_padded(uint8_t* dst, size_t length, const std::string& src) {
    std::memset(dst, 0, length);
    std::memcpy(dst, src.data(), std::min(length - 1, src.length()));
}

} // namespace

esp_err_t ArtnetDiscovery::start(const ArtnetNodeInfo& info, ArtnetDmxCallback dmx_callback) {
    info_ = info;
    dmx_callback_ = std::move(dmx_callback);

    return xTaskCreatePinnedToCore(
        task_entry,
        "lg_artnet",
        4096,
        this,
        4,
        nullptr,
        0
    ) == pdPASS ? ESP_OK : ESP_FAIL;
}

void ArtnetDiscovery::task_entry(void* arg) {
    static_cast<ArtnetDiscovery*>(arg)->task();
}

void ArtnetDiscovery::task() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(kTag, "socket failed");
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kArtnetPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(kTag, "bind failed");
        ::close(sock);
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    uint8_t buffer[600];

    for (;;) {
        sockaddr_in source = {};
        socklen_t source_len = sizeof(source);

        const int received = ::recvfrom(
            sock,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &source_len
        );

        if (received <= 0) continue;

        if (received < 10) continue;
        if (std::memcmp(buffer, "Art-Net\0", 8) != 0) continue;

        const uint16_t opcode = le16(buffer + 8);
        if (opcode == kOpPoll) {
            handle_poll(buffer, received, source, sock);
        } else if (opcode == kOpDmx) {
            handle_dmx(buffer, received);
        }
    }
}

bool ArtnetDiscovery::handle_poll(const uint8_t* packet, size_t length, const sockaddr_in& source, int sock) {
    // Basic ArtPoll validation
    if (length < 14) return false;
    
    // Respond with PollReply broadcast
    sockaddr_in bcast = {};
    bcast.sin_family = AF_INET;
    bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    
    send_poll_reply(bcast, sock);
    return true;
}

bool ArtnetDiscovery::handle_dmx(const uint8_t* packet, size_t length) {
    if (length < 18) return false;

    ArtnetDmxPacket out;
    out.sequence = packet[12];
    out.universe = le16(packet + 14);
    out.length = be16(packet + 16);

    if (out.length == 0 || out.length > 512) return false;
    if (18 + out.length > length) return false;

    std::memcpy(out.data, packet + 18, out.length);
    
    if (dmx_callback_) {
        dmx_callback_(out);
    }
    return true;
}

void ArtnetDiscovery::send_poll_reply(const sockaddr_in& destination, int sock) {
    uint8_t reply[239] = {};

    // --- Header (0-9) ---
    std::memcpy(reply, "Art-Net\0", 8);         // 0-7:   ID
    write_le16(reply + 8, kOpPollReply);         // 8-9:   OpCode 0x2100 LE

    // --- IP Address (10-13) ---
    esp_netif_ip_info_t ip_info = {};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif) esp_netif_get_ip_info(netif, &ip_info);
    }

    if (netif) {
        const uint8_t* ip_bytes = reinterpret_cast<const uint8_t*>(&ip_info.ip.addr);
        reply[10] = ip_bytes[0];                 // 10-13: IP (network byte order)
        reply[11] = ip_bytes[1];
        reply[12] = ip_bytes[2];
        reply[13] = ip_bytes[3];
    } else {
        reply[10] = info_.ip[0];
        reply[11] = info_.ip[1];
        reply[12] = info_.ip[2];
        reply[13] = info_.ip[3];
    }

    // --- Port (14-15) ---
    reply[14] = 0x36;                            // 14-15: Port 0x1936 LE
    reply[15] = 0x19;

    // --- Firmware Version (16-17, big-endian) ---
    reply[16] = static_cast<uint8_t>((info_.version >> 8) & 0xFF); // 16: VersInfoH
    reply[17] = static_cast<uint8_t>(info_.version & 0xFF);        // 17: VersInfoL

    // --- Net/Sub/OEM (18-21) ---
    reply[18] = info_.net;                       // 18: NetSwitch
    reply[19] = info_.subnet;                    // 19: SubSwitch
    reply[20] = static_cast<uint8_t>((info_.oem >> 8) & 0xFF); // 20: OemHi (big-endian)
    reply[21] = static_cast<uint8_t>(info_.oem & 0xFF);        // 21: OemLo

    // --- Status/ESTA (22-25) ---
    reply[22] = 0x00;                            // 22: UbeaVersion
    reply[23] = info_.status1;                   // 23: Status1
    write_le16(reply + 24, info_.esta);          // 24-25: EstaMan (little-endian per spec)

    // --- Names & Report (26-171) ---
    copy_padded(reply + 26, 18, info_.short_name);  // 26-43:   ShortName[18]
    copy_padded(reply + 44, 64, info_.long_name);   // 44-107:  LongName[64]
    copy_padded(reply + 108, 64, info_.report);      // 108-171: NodeReport[64]

    // --- Port Info (172-177) ---
    reply[172] = 0x00;                           // 172: NumPortsHi
    reply[173] = 0x01;                           // 173: NumPortsLo = 1
    reply[174] = 0x80;                           // 174: PortTypes[0] = output
    // 175-177: PortTypes[1-3] = 0 (unused ports)

    // --- Good Input/Output (178-185) ---
    // 178-181: GoodInput[0-3] = 0 (no input ports)
    reply[182] = 0x80;                           // 182: GoodOutputA[0] = data transmitting
    // 183-185: GoodOutputA[1-3] = 0 (unused ports)

    // --- SwIn/SwOut (186-193) ---
    // 186-189: SwIn[0-3] = 0 (no input ports)
    reply[190] = info_.universe;                 // 190: SwOut[0] = output universe
    // 191-193: SwOut[1-3] = 0 (unused ports)

    // --- Misc (194-199) ---
    // 194: AcnPriority = 0
    // 195: SwMacro = 0 (deprecated)
    // 196: SwRemote = 0 (deprecated)
    // 197-199: Spare[3] = 0

    // --- Style (200) ---
    reply[200] = info_.style;                    // 200: Style

    // --- MAC (201-206) ---
    for (int i = 0; i < 6; ++i) {
        reply[201 + i] = info_.mac[i];           // 201-206: MAC[6]
    }

    // --- Bind/Status2 (207-212) ---
    // 207-210: BindIp[4] = 0 (same as node IP, already reported)
    reply[211] = 1;                              // 211: BindIndex (1 = root device)
    reply[212] = info_.status2;                  // 212: Status2

    // 213-238: Art-Net 4 extended fields default to 0

    sockaddr_in dest = destination;
    dest.sin_port = htons(kArtnetPort);
    sendto(sock, reply, sizeof(reply), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

} // namespace lg
