// components/discovery/include/FleetDiscoveryService.hpp
#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "NodeIdentity.hpp"

namespace lg::discovery {

enum class NodePresence : uint8_t {
    Online = 0,   // age <= 20s
    Stale = 1,    // age 20s .. 60s
    Offline = 2,  // age > 60s
};

struct DiscoveredNode {
    char id[24] = {};
    char name[32] = {};
    char hostname[64] = {};

    char family[16] = {};
    char model[24] = {};
    char firmware[16] = {};

    uint32_t ipv4 = 0;
    uint16_t port = 80;

    uint32_t last_seen_ms = 0;
};

class FleetDiscoveryService {
public:
    static constexpr size_t kMaxDiscoveredNodes = 32;
    static constexpr uint32_t kQueryIntervalMs = 10000;   // 10s query interval
    static constexpr uint32_t kQueryTimeoutMs = 1000;     // <= 1000ms query timeout
    static constexpr uint32_t kOnlineThresholdMs = 20000; // present/online age <= 20s
    static constexpr uint32_t kStaleThresholdMs = 60000;  // stale age 20s..60s (offline/expired > 60s)

    FleetDiscoveryService();
    ~FleetDiscoveryService();

    // Start discovery worker with our own node identity (to exclude self by immutable ID)
    esp_err_t start(const NodeIdentity& self_identity);
    esp_err_t stop();

    // Direct reconciliation API (used by worker and deterministic test harness)
    // Returns true if node was inserted or updated; false if rejected (e.g. self or invalid)
    bool reconcile_node(const DiscoveredNode& node, uint32_t now_ms);

    // Prune entries older than stale threshold (age > 60s)
    void prune_expired(uint32_t now_ms);

    // Thread-safe snapshot copy of discovered nodes
    size_t get_snapshot(DiscoveredNode* out_nodes, size_t max_count, uint32_t now_ms) const;

    // Get count of discovered nodes
    size_t count() const;

    // Helper to determine node presence based on age
    static NodePresence determine_presence(uint32_t last_seen_ms, uint32_t now_ms);
    static const char* presence_to_string(NodePresence presence);

    // Helper to format/parse IPv4
    static void format_ipv4(uint32_t ip, char out_str[16]);
    static uint32_t parse_ipv4(const char* ip_str);

    // Single query scan (called by background worker task or test harness)
    size_t run_scan_once(uint32_t now_ms);

    const NodeIdentity& self_identity() const { return self_identity_; }
    void set_self_identity(const NodeIdentity& self_identity) { self_identity_ = self_identity; }

private:
    static void worker_task(void* arg);

    NodeIdentity self_identity_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    volatile bool running_ = false;

    DiscoveredNode nodes_[kMaxDiscoveredNodes] = {};
    size_t node_count_ = 0;
};

} // namespace lg::discovery
