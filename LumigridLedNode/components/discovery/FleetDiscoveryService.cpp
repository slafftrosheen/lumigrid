// components/discovery/FleetDiscoveryService.cpp
#include "FleetDiscoveryService.hpp"

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"

#if defined(ESP_PLATFORM)
#include "mdns.h"
#endif

namespace lg::discovery {

static const char* TAG = "fleet_disc";

FleetDiscoveryService::FleetDiscoveryService() {
    mutex_ = xSemaphoreCreateMutex();
}

FleetDiscoveryService::~FleetDiscoveryService() {
    stop();
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t FleetDiscoveryService::start(const NodeIdentity& self_identity) {
    if (running_) {
        return ESP_OK;
    }

    self_identity_ = self_identity;
    running_ = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        &FleetDiscoveryService::worker_task,
        "fleet_disc",
        4096,
        this,
        1, // Low priority background task
        &worker_task_,
        0
    );

    if (ret != pdPASS) {
        running_ = false;
        worker_task_ = nullptr;
        ESP_LOGE(TAG, "Failed to create fleet discovery background task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Fleet discovery service started for self ID '%s'", self_identity_.id);
    return ESP_OK;
}

esp_err_t FleetDiscoveryService::stop() {
    if (!running_) {
        return ESP_OK;
    }

    running_ = false;
    if (worker_task_) {
        TaskHandle_t t = worker_task_;
        worker_task_ = nullptr;
        vTaskDelete(t);
    }

    ESP_LOGI(TAG, "Fleet discovery service stopped");
    return ESP_OK;
}

void FleetDiscoveryService::worker_task(void* arg) {
    auto* self = static_cast<FleetDiscoveryService*>(arg);
    while (self && self->running_) {
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        self->run_scan_once(now_ms);

        // Sleep for query interval (10s) in 100ms slices to respond promptly to stop()
        for (int i = 0; i < 100 && self->running_; ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    vTaskDelete(nullptr);
}

bool FleetDiscoveryService::reconcile_node(const DiscoveredNode& node, uint32_t now_ms) {
    // 1. Validation & Self Exclusion
    if (node.id[0] == '\0') {
        return false;
    }
    if (self_identity_.id[0] != '\0' && std::strcmp(node.id, self_identity_.id) == 0) {
        // Exclude self by immutable identity
        return false;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    // 2. Check if already known by immutable ID
    for (size_t i = 0; i < node_count_; ++i) {
        if (std::strcmp(nodes_[i].id, node.id) == 0) {
            // Reconcile: Update network address, port, metadata and timestamp
            nodes_[i].ipv4 = node.ipv4;
            nodes_[i].port = node.port;
            nodes_[i].last_seen_ms = now_ms;

            if (node.name[0] != '\0') {
                std::strncpy(nodes_[i].name, node.name, sizeof(nodes_[i].name) - 1);
                nodes_[i].name[sizeof(nodes_[i].name) - 1] = '\0';
            }
            if (node.hostname[0] != '\0') {
                std::strncpy(nodes_[i].hostname, node.hostname, sizeof(nodes_[i].hostname) - 1);
                nodes_[i].hostname[sizeof(nodes_[i].hostname) - 1] = '\0';
            }
            if (node.family[0] != '\0') {
                std::strncpy(nodes_[i].family, node.family, sizeof(nodes_[i].family) - 1);
                nodes_[i].family[sizeof(nodes_[i].family) - 1] = '\0';
            }
            if (node.model[0] != '\0') {
                std::strncpy(nodes_[i].model, node.model, sizeof(nodes_[i].model) - 1);
                nodes_[i].model[sizeof(nodes_[i].model) - 1] = '\0';
            }
            if (node.firmware[0] != '\0') {
                std::strncpy(nodes_[i].firmware, node.firmware, sizeof(nodes_[i].firmware) - 1);
                nodes_[i].firmware[sizeof(nodes_[i].firmware) - 1] = '\0';
            }

            if (mutex_) xSemaphoreGive(mutex_);
            return true;
        }
    }

    // 3. Not found: Prune any expired records first if full
    if (node_count_ >= kMaxDiscoveredNodes) {
        // In-place prune of records older than stale threshold
        size_t write_idx = 0;
        for (size_t read_idx = 0; read_idx < node_count_; ++read_idx) {
            uint32_t age = (now_ms >= nodes_[read_idx].last_seen_ms)
                               ? (now_ms - nodes_[read_idx].last_seen_ms)
                               : 0;
            if (age <= kStaleThresholdMs) {
                if (write_idx != read_idx) {
                    nodes_[write_idx] = nodes_[read_idx];
                }
                write_idx++;
            }
        }
        node_count_ = write_idx;
    }

    // 4. Insert new node if space available
    if (node_count_ < kMaxDiscoveredNodes) {
        nodes_[node_count_] = node;
        nodes_[node_count_].last_seen_ms = now_ms;
        node_count_++;
        if (mutex_) xSemaphoreGive(mutex_);
        return true;
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return false;
}

void FleetDiscoveryService::prune_expired(uint32_t now_ms) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < node_count_; ++read_idx) {
        uint32_t age = (now_ms >= nodes_[read_idx].last_seen_ms)
                           ? (now_ms - nodes_[read_idx].last_seen_ms)
                           : 0;
        if (age <= kStaleThresholdMs) {
            if (write_idx != read_idx) {
                nodes_[write_idx] = nodes_[read_idx];
            }
            write_idx++;
        }
    }
    node_count_ = write_idx;

    if (mutex_) xSemaphoreGive(mutex_);
}

size_t FleetDiscoveryService::get_snapshot(DiscoveredNode* out_nodes, size_t max_count, uint32_t now_ms) const {
    if (!out_nodes || max_count == 0) {
        return 0;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    size_t copied = 0;
    for (size_t i = 0; i < node_count_ && copied < max_count; ++i) {
        out_nodes[copied++] = nodes_[i];
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return copied;
}

size_t FleetDiscoveryService::count() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    size_t c = node_count_;
    if (mutex_) xSemaphoreGive(mutex_);
    return c;
}

NodePresence FleetDiscoveryService::determine_presence(uint32_t last_seen_ms, uint32_t now_ms) {
    uint32_t age = (now_ms >= last_seen_ms) ? (now_ms - last_seen_ms) : 0;
    if (age <= kOnlineThresholdMs) {
        return NodePresence::Online;
    }
    if (age <= kStaleThresholdMs) {
        return NodePresence::Stale;
    }
    return NodePresence::Offline;
}

const char* FleetDiscoveryService::presence_to_string(NodePresence presence) {
    switch (presence) {
        case NodePresence::Online: return "online";
        case NodePresence::Stale: return "stale";
        case NodePresence::Offline: default: return "offline";
    }
}

void FleetDiscoveryService::format_ipv4(uint32_t ip, char out_str[16]) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&ip);
    std::snprintf(out_str, 16, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

uint32_t FleetDiscoveryService::parse_ipv4(const char* ip_str) {
    if (!ip_str) return 0;
    unsigned int b[4] = {0};
    if (std::sscanf(ip_str, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) == 4) {
        uint32_t res = 0;
        uint8_t* p = reinterpret_cast<uint8_t*>(&res);
        p[0] = static_cast<uint8_t>(b[0]);
        p[1] = static_cast<uint8_t>(b[1]);
        p[2] = static_cast<uint8_t>(b[2]);
        p[3] = static_cast<uint8_t>(b[3]);
        return res;
    }
    return 0;
}

size_t FleetDiscoveryService::run_scan_once(uint32_t now_ms) {
    size_t count = 0;

#if defined(ESP_PLATFORM)
    mdns_result_t* results = nullptr;
    esp_err_t err = mdns_query_ptr("_lumigrid", "_tcp", kQueryTimeoutMs, kMaxDiscoveredNodes, &results);
    if (err != ESP_OK || !results) {
        return 0;
    }

    mdns_result_t* r = results;
    while (r) {
        DiscoveredNode dn{};
        dn.port = r->port ? r->port : 80;

        if (r->instance_name) {
            std::strncpy(dn.name, r->instance_name, sizeof(dn.name) - 1);
            dn.name[sizeof(dn.name) - 1] = '\0';
        }
        if (r->hostname) {
            std::strncpy(dn.hostname, r->hostname, sizeof(dn.hostname) - 1);
            dn.hostname[sizeof(dn.hostname) - 1] = '\0';
        }

        // Parse TXT records
        for (size_t t = 0; t < r->txt_count; ++t) {
            if (!r->txt[t].key || !r->txt[t].value) continue;
            if (std::strcmp(r->txt[t].key, "id") == 0) {
                std::strncpy(dn.id, r->txt[t].value, sizeof(dn.id) - 1);
                dn.id[sizeof(dn.id) - 1] = '\0';
            } else if (std::strcmp(r->txt[t].key, "family") == 0) {
                std::strncpy(dn.family, r->txt[t].value, sizeof(dn.family) - 1);
                dn.family[sizeof(dn.family) - 1] = '\0';
            } else if (std::strcmp(r->txt[t].key, "model") == 0) {
                std::strncpy(dn.model, r->txt[t].value, sizeof(dn.model) - 1);
                dn.model[sizeof(dn.model) - 1] = '\0';
            } else if (std::strcmp(r->txt[t].key, "fw") == 0) {
                std::strncpy(dn.firmware, r->txt[t].value, sizeof(dn.firmware) - 1);
                dn.firmware[sizeof(dn.firmware) - 1] = '\0';
            }
        }

        // Parse IPv4 address
        mdns_ip_addr_t* a = r->addr;
        while (a) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                dn.ipv4 = a->addr.u_addr.ip4.addr;
                break;
            }
            a = a->next;
        }

        if (dn.id[0] != '\0') {
            if (reconcile_node(dn, now_ms)) {
                count++;
            }
        }

        r = r->next;
    }

    mdns_query_results_free(results);
#endif

    // Also run routine pruning of expired records
    prune_expired(now_ms);

    return count;
}

} // namespace lg::discovery
