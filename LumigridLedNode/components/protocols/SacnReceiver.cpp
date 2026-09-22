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

uint32_t fnv1a_32(const uint8_t* data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

esp_err_t SacnReceiver::start(SacnDmxCallback callback) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (running_) {
        return ESP_ERR_INVALID_STATE;
    }
    callback_ = std::move(callback);
    running_ = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        task_entry,
        "lg_sacn",
        4096,
        this,
        4,
        &task_handle_,
        0
    );
    if (res != pdPASS) {
        running_ = false;
        task_handle_ = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t SacnReceiver::stop() {
    TaskHandle_t task_to_wait = nullptr;
    int sock_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!running_) return ESP_OK;
        running_ = false;
        if (sock_ >= 0) {
            leave_multicast_groups(sock_);
            sock_to_close = sock_;
            sock_ = -1;
        }
        task_to_wait = task_handle_;
    }
    if (sock_to_close >= 0) {
        ::close(sock_to_close);
    }
    // Wait for the worker task to finish before returning.
    if (task_to_wait) {
        for (int i = 0; i < 40; ++i) {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (task_handle_ == nullptr) break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    return ESP_OK;
}

void SacnReceiver::set_subscribed_universes(const uint16_t* universes, size_t count) {
    std::lock_guard<std::mutex> lock(mtx_);

    std::array<uint16_t, 16> new_unis{};
    size_t new_count = 0;

    if (universes && count > 0) {
        for (size_t i = 0; i < count; ++i) {
            uint16_t u = universes[i];
            if (u == 0 || u > 63999) continue;

            // Deduplicate
            bool duplicate = false;
            for (size_t j = 0; j < new_count; ++j) {
                if (new_unis[j] == u) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && new_count < new_unis.size()) {
                new_unis[new_count++] = u;
            }
        }
    }

    if (sock_ >= 0) {
        // 1. Leave groups no longer required
        for (size_t i = 0; i < subscribed_count_; ++i) {
            uint16_t old_u = subscribed_universes_[i];
            bool still_needed = false;
            for (size_t j = 0; j < new_count; ++j) {
                if (new_unis[j] == old_u) {
                    still_needed = true;
                    break;
                }
            }
            if (!still_needed) {
                struct ip_mreq mreq = {};
                mreq.imr_multiaddr.s_addr = htonl((239U << 24) | (255U << 16) | old_u);
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                int res = ::setsockopt(sock_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
                if (res == 0) {
                    ESP_LOGI(kTag, "Left sACN multicast group 239.255.%u.%u for universe %u", (old_u >> 8) & 0xFF, old_u & 0xFF, old_u);
                }
            }
        }

        // 2. Join newly required groups
        for (size_t i = 0; i < new_count; ++i) {
            uint16_t new_u = new_unis[i];
            bool already_joined = false;
            for (size_t j = 0; j < subscribed_count_; ++j) {
                if (subscribed_universes_[j] == new_u) {
                    already_joined = true;
                    break;
                }
            }
            if (!already_joined) {
                struct ip_mreq mreq = {};
                mreq.imr_multiaddr.s_addr = htonl((239U << 24) | (255U << 16) | new_u);
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                int res = ::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
                if (res < 0) {
                    ESP_LOGW(kTag, "Failed to join multicast for universe %u (errno %d)", new_u, errno);
                } else {
                    ESP_LOGI(kTag, "Joined sACN multicast group 239.255.%u.%u for universe %u", (new_u >> 8) & 0xFF, new_u & 0xFF, new_u);
                }
            }
        }
    }

    subscribed_universes_ = new_unis;
    subscribed_count_ = new_count;
}

void SacnReceiver::leave_multicast_groups(int sock) {
    for (size_t i = 0; i < subscribed_count_; ++i) {
        uint16_t u = subscribed_universes_[i];
        if (u == 0 || u > 63999) continue;

        struct ip_mreq mreq = {};
        mreq.imr_multiaddr.s_addr = htonl((239U << 24) | (255U << 16) | u);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        int res = ::setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        if (res == 0) {
            ESP_LOGI(kTag, "Left sACN multicast group 239.255.%u.%u for universe %u", (u >> 8) & 0xFF, u & 0xFF, u);
        }
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

    while (running_) {
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

        if (received <= 0) {
            if (!running_) break;
            continue;
        }

        handle_packet(buffer, received);
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        // Only close if stop() didn't already take ownership (sock_ == -1).
        if (sock_ == sock) {
            ::close(sock);
            sock_ = -1;
        }
        task_handle_ = nullptr;
    }
    vTaskDelete(nullptr);
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

    // Extract CID (bytes 22..37)
    std::array<uint8_t, 16> cid{};
    std::memcpy(cid.data(), packet + 22, 16);
    uint32_t cid_hash = fnv1a_32(cid.data(), 16);

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
    out.cid_hash = cid_hash;
    out.cid = cid;
    out.length = prop_val_count - 1; // Subtract start code
    
    if (out.length > 512) out.length = 512;
    std::memcpy(out.data, packet + 126, out.length);

    if (callback_) {
        callback_(out);
    }
    
    return true;
}

} // namespace lg
