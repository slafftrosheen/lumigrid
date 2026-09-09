// components/protocols/SacnReceiver.cpp
#include "SacnReceiver.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace lg {
namespace {
constexpr char kTag[] = "sacn";
constexpr uint16_t kSacnPort = 5568;

uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>(p[1]) |
           (static_cast<uint16_t>(p[0]) << 8);
}

} // namespace

esp_err_t SacnReceiver::start(SacnDmxCallback callback) {
    callback_ = std::move(callback);

    return xTaskCreatePinnedToCore(
        task_entry,
        "lg_sacn",
        4096,
        this,
        4,
        nullptr,
        0
    ) == pdPASS ? ESP_OK : ESP_FAIL;
}

void SacnReceiver::set_subscribed_universes(const uint16_t* universes, size_t count) {
    std::lock_guard<std::mutex> lock(mtx_);
    subscribed_count_ = 0;
    if (universes && count > 0) {
        size_t n = (count < subscribed_universes_.size()) ? count : subscribed_universes_.size();
        for (size_t i = 0; i < n; ++i) {
            subscribed_universes_[i] = universes[i];
        }
        subscribed_count_ = n;
    }
    if (sock_ >= 0) {
        join_multicast_groups(sock_);
    }
}

void SacnReceiver::join_multicast_groups(int sock) {
    for (size_t i = 0; i < subscribed_count_; ++i) {
        uint16_t u = subscribed_universes_[i];
        if (u == 0 || u > 63999) continue;

        struct ip_mreq mreq = {};
        // E1.31 Section 9.3.1: 239.255.(u >> 8).(u & 0xFF)
        mreq.imr_multiaddr.s_addr = htonl((239U << 24) | (255U << 16) | u);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        int res = ::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
        if (res < 0) {
            ESP_LOGW(kTag, "Failed to join multicast for universe %u (errno %d)", u, errno);
        } else {
            ESP_LOGI(kTag, "Joined sACN multicast group 239.255.%u.%u for universe %u", (u >> 8) & 0xFF, u & 0xFF, u);
        }
    }
}

void SacnReceiver::task_entry(void* arg) {
    static_cast<SacnReceiver*>(arg)->task();
}

void SacnReceiver::task() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(kTag, "socket failed");
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kSacnPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(kTag, "bind failed");
        ::close(sock);
        vTaskDelete(nullptr);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        sock_ = sock;
        join_multicast_groups(sock);
    }

    uint8_t buffer[1024];

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

        handle_packet(buffer, received);
    }
}

bool SacnReceiver::handle_packet(const uint8_t* packet, size_t length) {
    // Basic E1.31 size check (Root + Framing + DMP headers)
    if (length < 125) return false;

    // Check ACN Packet Identifier (ASC-E1.17\0\0\0)
    if (std::memcmp(packet + 4, "ASC-E1.17\0\0\0", 12) != 0) return false;

    // Root Layer Length check
    uint16_t root_flags_len = be16(packet + 16);
    if ((root_flags_len & 0x7000) != 0x7000) return false;
    
    // Root Vector (0x00000004 for E1.31 data packet)
    if (packet[18] != 0 || packet[19] != 0 || packet[20] != 0 || packet[21] != 4) return false;

    // Framing Layer Vector (0x00000002)
    if (packet[40] != 0 || packet[41] != 0 || packet[42] != 0 || packet[43] != 2) return false;

    // Extract fields
    uint8_t priority = packet[108];
    uint8_t sequence = packet[111];
    uint16_t universe = be16(packet + 113);

    // DMP Layer Vector (0x02)
    if (packet[117] != 2) return false;

    // Address Type and Data Type (0xa1)
    if (packet[118] != 0xa1) return false;

    // Property Value Count (includes start code)
    uint16_t prop_val_count = be16(packet + 123);
    if (prop_val_count < 1 || prop_val_count > 513) return false;
    if (125 + prop_val_count > length) return false;

    // We expect the start code to be 0 for DMX data.
    if (packet[125] != 0) return false; // Non-zero start code

    SacnDmxPacket out;
    out.universe = universe;
    out.sequence = sequence;
    out.priority = priority;
    out.length = prop_val_count - 1; // Subtract start code
    
    if (out.length > 512) out.length = 512;
    std::memcpy(out.data, packet + 126, out.length);

    if (callback_) {
        callback_(out);
    }
    
    return true;
}

} // namespace lg
