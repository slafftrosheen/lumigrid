// components/protocols/ArtnetDiscovery.cpp
#include "ArtnetDiscovery.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>

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
    std::memcpy(reply, "Art-Net\0", 8);
    write_le16(reply + 8, kOpPollReply);

    reply[10] = info_.ip[0];
    reply[11] = info_.ip[1];
    reply[12] = info_.ip[2];
    reply[13] = info_.ip[3];

    reply[14] = 0x36;
    reply[15] = 0x19;

    reply[16] = static_cast<uint8_t>((info_.version >> 8) & 0xFF);
    reply[17] = static_cast<uint8_t>(info_.version & 0xFF);

    reply[18] = info_.net;
    reply[19] = 0x00;
    write_le16(reply + 20, info_.oem);
    reply[23] = 0x00;
    write_le16(reply + 24, info_.esta);

    copy_padded(reply + 26, 18, info_.short_name);
    copy_padded(reply + 44, 64, info_.long_name);
    copy_padded(reply + 108, 64, info_.report);

    reply[172] = 0x00;
    reply[173] = 0x01; // NumPorts

    reply[174] = 0x80; // Port 1 type: output data
    reply[175] = 0x00;
    reply[176] = 0x80;
    reply[177] = 0x00;

    reply[178] = 0x00; // GoodInput
    reply[179] = info_.universe;
    reply[180] = 0x00;
    reply[181] = 0x00;

    reply[182] = 0x00;
    reply[183] = 0x80; // GoodOutput (data is being transmitted)
    reply[184] = 0x00;
    reply[185] = 0x00;

    reply[186] = 0x00;
    reply[187] = 0x00;

    reply[188] = info_.style;

    for (int i = 0; i < 6; ++i) reply[201 + i] = info_.mac[i];

    reply[212] = info_.status1;
    reply[213] = info_.status2;

    sockaddr_in dest = destination;
    dest.sin_port = htons(kArtnetPort);
    sendto(sock, reply, sizeof(reply), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

} // namespace lg
