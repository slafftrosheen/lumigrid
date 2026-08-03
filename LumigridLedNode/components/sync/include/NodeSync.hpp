// components/sync/include/NodeSync.hpp
#pragma once
#include "SignageEngine.hpp"
#include "SignageTypes.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ConfigStore.hpp"

namespace lg {

/// Multi-node synchronization over UDP broadcast.
///
/// A Master node broadcasts its current sequence state
/// at 10 Hz on port 6455. Worker nodes receive these packets and snap
/// their SignageEngine to the master's timeline, achieving pixel-perfect
/// synchronized animations across multiple sign segments.
class NodeSync {
public:
    NodeSync(SignageEngine& engine, ConfigStore& config);

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
    ConfigStore&   config_;
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
        uint16_t bpm;
        uint8_t  sequence_enabled;
        uint32_t global_clock_ms;
    };

    static_assert(sizeof(SyncPacket) == 16, "SyncPacket must be 16 bytes");
};

} // namespace lg
