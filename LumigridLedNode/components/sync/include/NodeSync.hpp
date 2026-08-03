// components/sync/include/NodeSync.hpp
#pragma once
#include "SignageEngine.hpp"
#include "SignageTypes.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lg {

/// Multi-node synchronization over UDP broadcast.
///
/// A Master node broadcasts its current preset index and elapsed time
/// at 10 Hz on port 6455. Worker nodes receive these packets and snap
/// their SignageEngine to the master's timeline, achieving pixel-perfect
/// synchronized animations across multiple sign segments.
class NodeSync {
public:
    explicit NodeSync(SignageEngine& engine);

    esp_err_t start(NodeRole role);
    void stop();

    NodeRole role() const { return role_; }
    uint8_t peer_count() const { return peer_count_; }

private:
    static void master_task_entry(void* arg);
    static void worker_task_entry(void* arg);

    void master_task();
    void worker_task();

    SignageEngine& engine_;
    NodeRole role_ = NodeRole::Auto;
    int sock_ = -1;
    uint8_t peer_count_ = 0;

    TaskHandle_t task_handle_ = nullptr;

    // Sync protocol constants
    static constexpr uint16_t kSyncPort = 6455;
    static constexpr uint32_t kSyncIntervalMs = 100; // 10 Hz
    static constexpr uint32_t kMagic = 0x4C475359; // "LGSY"

    struct __attribute__((packed)) SyncPacket {
        uint32_t magic;
        uint8_t  version;
        uint32_t sequence;
        uint8_t  preset_index;
        uint32_t elapsed_ms;
        uint8_t  brightness;
    };

    static_assert(sizeof(SyncPacket) == 15, "SyncPacket must be 15 bytes");
};

} // namespace lg
