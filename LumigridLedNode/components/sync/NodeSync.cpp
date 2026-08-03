// components/sync/NodeSync.cpp
#include "NodeSync.hpp"
#include "esp_log.h"
#include "esp_timer.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <cstring>

static const char* TAG = "sync";

namespace lg {
namespace {

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace

NodeSync::NodeSync(SignageEngine& engine) : engine_(engine) {}

esp_err_t NodeSync::start(NodeRole role) {
    role_ = role;

    if (role_ == NodeRole::Auto || role_ == NodeRole::Master) {
        const BaseType_t ok = xTaskCreatePinnedToCore(
            master_task_entry, "sync_master", 3072, this, 3, &task_handle_, 0
        );
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
        ESP_LOGI(TAG, "Started as Master (broadcast on port %u)", kSyncPort);
    } else {
        const BaseType_t ok = xTaskCreatePinnedToCore(
            worker_task_entry, "sync_worker", 3072, this, 3, &task_handle_, 0
        );
        if (ok != pdPASS) return ESP_ERR_NO_MEM;
        ESP_LOGI(TAG, "Started as Worker (listening on port %u)", kSyncPort);
    }

    return ESP_OK;
}

void NodeSync::stop() {
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
}

// ── Master ──────────────────────────────────────────────────────────

void NodeSync::master_task_entry(void* arg) {
    static_cast<NodeSync*>(arg)->master_task();
    vTaskDelete(nullptr);
}

void NodeSync::master_task() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    // Enable broadcast
    int broadcast = 1;
    setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kSyncPort);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    uint32_t seq = 0;
    const uint32_t start_ms = now_ms();

    for (;;) {
        SyncPacket pkt{};
        pkt.magic = kMagic;
        pkt.version = 1;
        pkt.sequence = seq++;
        pkt.preset_index = engine_.active_preset_index();
        pkt.elapsed_ms = now_ms() - start_ms;
        pkt.brightness = 255; // Full, let worker apply its own budget

        sendto(sock_, &pkt, sizeof(pkt), 0,
               reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));

        vTaskDelay(pdMS_TO_TICKS(kSyncIntervalMs));
    }
}

// ── Worker ──────────────────────────────────────────────────────────

void NodeSync::worker_task_entry(void* arg) {
    static_cast<NodeSync*>(arg)->worker_task();
    vTaskDelete(nullptr);
}

void NodeSync::worker_task() {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(kSyncPort);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed on port %u", kSyncPort);
        close(sock_);
        sock_ = -1;
        return;
    }

    // Set receive timeout
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t last_preset = 0xFF;

    for (;;) {
        SyncPacket pkt{};
        const int received = recv(sock_, &pkt, sizeof(pkt), 0);

        if (received != sizeof(SyncPacket)) {
            continue; // Timeout or wrong size
        }

        if (pkt.magic != kMagic || pkt.version != 1) {
            continue; // Not our protocol
        }

        // Track that we have a master
        peer_count_ = 1;
        engine_.feed_watchdog();

        // Activate preset if changed
        if (pkt.preset_index != last_preset) {
            engine_.activate_preset(pkt.preset_index);
            last_preset = pkt.preset_index;
        }
    }
}

} // namespace lg
