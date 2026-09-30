// components/sync/include/NodeSync.hpp
#pragma once
#include "ClockDiscipline.hpp"
#include "LightingRuntime.hpp"
#include "SignageTypes.hpp"
#include "ShowTransport.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "ConfigStore.hpp"
#include "lwip/sockets.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace lg {

enum class SyncTransportError : uint8_t {
    None = 0,
    SceneMissing = 1,
    CueListMissing = 2,
    CueIndexInvalid = 3,
    RuntimeUnavailable = 4,
    UnsupportedTransport = 5,
    TimelineMissing = 6,
    TimelineMismatch = 7,
    TimelineInvalid = 8,
};

inline const char* sync_transport_error_str(uint8_t err) {
    switch (static_cast<SyncTransportError>(err)) {
        case SyncTransportError::None: return "none";
        case SyncTransportError::SceneMissing: return "scene_missing";
        case SyncTransportError::CueListMissing: return "cue_list_missing";
        case SyncTransportError::CueIndexInvalid: return "cue_index_invalid";
        case SyncTransportError::RuntimeUnavailable: return "runtime_unavailable";
        case SyncTransportError::UnsupportedTransport: return "unsupported_transport";
        case SyncTransportError::TimelineMissing: return "timeline_missing";
        case SyncTransportError::TimelineMismatch: return "timeline_mismatch";
        case SyncTransportError::TimelineInvalid: return "timeline_invalid";
        default: return "none";
    }
}

struct SyncPeerStatus {
    uint32_t address = 0; // IPv4 address in network byte order
    uint32_t age_ms = 0;
    uint16_t latency_ms = 0;
};

struct NodeSyncStatus {
    NodeRole configured_role = NodeRole::Auto;
    NodeRole active_role = NodeRole::Auto;
    bool running = false;
    bool clock_locked = false;
    bool clock_has_epoch = false;
    uint8_t peer_count = 0;
    uint32_t last_packet_age_ms = 0;
    uint32_t packets_received = 0;
    uint32_t packets_missed = 0;
    int32_t clock_offset_ms = 0;
    uint32_t clock_jitter_us = 0;
    SyncPeerStatus peers[8] = {};
    uint32_t master_address = 0;       // IPv4 of current leader (0 for leader itself)
    uint8_t  transport = 0;            // 0=standalone, 1=scene, 2=timeline, 3=cue
    uint32_t transport_generation = 0;
    uint16_t remote_bpm = 0;
    bool     remote_timeline_enabled = false;
    bool     transport_apply_ok = true;
    uint8_t  transport_error = 0;
    CueTransportSnapshot cue{};
};

/// Multi-node synchronization over UDP broadcast with bounded clock discipline.
class NodeSync {
public:
    using LegacyTransportCallback = std::function<void(bool timeline_enabled, uint16_t bpm)>;
    using TransportCallback = std::function<void(const ShowTransportSnapshot&)>;
    using TransportProvider = std::function<ShowTransportSnapshot(uint64_t now_ms)>;
    using RemoteTransportCallback = std::function<bool(const ShowTransportSnapshot&, uint8_t& out_error)>;

    NodeSync(LightingRuntime& engine, ConfigStore& config, ClockDiscipline& clock);
    ~NodeSync();

    esp_err_t start(NodeRole role);
    esp_err_t stop(uint32_t timeout_ms = 1500);

    NodeRole role() const { return configured_role_; }
    NodeRole active_role() const;
    NodeSyncStatus status() const;

    void reset_config_cache();

    void set_transport_callback(TransportCallback callback) {
        transport_callback_ = std::move(callback);
    }
    void set_legacy_transport_callback(LegacyTransportCallback callback) {
        legacy_transport_callback_ = std::move(callback);
    }

    void set_transport_provider(TransportProvider provider) {
        transport_provider_ = std::move(provider);
    }

    void set_remote_transport_callback(RemoteTransportCallback callback) {
        remote_transport_callback_ = std::move(callback);
    }
    enum class PacketType : uint8_t {
        Heartbeat = 1,
        Acknowledgement = 2,
    };

    // Phase 6: Canonical 68-byte packed SyncPacket v5
    struct __attribute__((packed)) SyncPacket {
        uint32_t magic;
        uint8_t  version;
        PacketType type;

        uint32_t sequence;

        uint8_t  transport;       // ShowTransportMode
        uint8_t  active_scene;
        uint16_t bpm;

        uint8_t  cue_list;
        uint8_t  cue_index;
        uint8_t  cue_next;
        uint8_t  cue_state;

        uint32_t cue_generation;
        uint32_t cue_elapsed_ms;
        uint16_t manual_fade_q15;

        uint8_t  flags;
        uint8_t  reserved;

        uint64_t global_clock_ms;
        uint32_t sent_monotonic_ms;

        uint8_t  sender_mac[6];
        uint32_t token;

        // Protocol v5 extensions (16 bytes)
        uint32_t transport_generation;
        uint64_t timeline_anchor_ms;
        uint32_t timeline_checksum;
    };

    static_assert(sizeof(SyncPacket) == 68, "SyncPacket must be 68 bytes");

private:

    static void master_task_entry(void* arg);
    static void worker_task_entry(void* arg);
    static void auto_task_entry(void* arg);

    void master_task();
    void worker_task();
    void auto_task();
    NodeRole master_loop(int socket, bool auto_mode);
    NodeRole worker_loop(int socket, bool auto_mode);
    bool open_socket(int& socket, bool broadcast);
    bool handle_heartbeat(const SyncPacket& packet, const sockaddr_in& sender);
    void send_ack(int socket, const SyncPacket& heartbeat, const sockaddr_in& sender, uint32_t token);
    void update_peer(const sockaddr_in& sender, uint16_t latency_ms);
    void reset_master();

    LightingRuntime& engine_;
    ConfigStore&   config_;
    ClockDiscipline& clock_;

    NodeRole configured_role_ = NodeRole::Auto;
    NodeRole active_role_ = NodeRole::Auto;
    int sock_ = -1;
    TaskHandle_t task_handle_ = nullptr;
    bool running_ = false;
    SemaphoreHandle_t shutdown_sem_ = nullptr;

    // Cached config fields
    uint32_t cached_sync_token_ = 0;
    bool cached_sync_enabled_ = false;
    uint32_t last_config_cache_ms_ = 0;
    static constexpr uint32_t kConfigCacheIntervalMs = 2000;
    void refresh_config_cache();

    mutable portMUX_TYPE status_mutex_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t master_address_ = 0;
    uint8_t master_mac_[6] = {};
    uint32_t last_packet_ms_ = 0;
    uint32_t last_sequence_ = 0;
    bool has_sequence_ = false;
    uint32_t packets_received_ = 0;
    uint32_t packets_missed_ = 0;
    uint16_t remote_bpm_ = 0;
    bool     remote_timeline_enabled_ = false;
    TransportCallback transport_callback_{};
    LegacyTransportCallback legacy_transport_callback_{};
    TransportProvider transport_provider_{};
    RemoteTransportCallback remote_transport_callback_{};
    ShowTransportSnapshot remote_transport_snapshot_{};
    bool transport_apply_ok_ = true;
    uint8_t transport_error_ = 0;

    static constexpr size_t kMaxPeers = 8;
    struct Peer {
        uint32_t address = 0;
        uint32_t last_seen_ms = 0;
        uint16_t latency_ms = 0;
    };
    Peer peers_[kMaxPeers] = {};

    // Sync protocol constants
    static constexpr uint16_t kSyncPort = 6455;
    static constexpr uint32_t kSyncIntervalMs = 100; // 10 Hz
    static constexpr uint32_t kSyncManualIntervalMs = 50; // 20 Hz during manual crossfade
    static constexpr uint32_t kMagic = 0x4C475359; // "LGSY"
    static constexpr uint8_t kPacketVersion = 5;
    static constexpr uint32_t kPeerTimeoutMs = 3000;
    static constexpr uint32_t kAutoElectionBaseMs = 800;
};

} // namespace lg
