// components/sync/NodeSync.cpp
#include "NodeSync.hpp"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

#include <sys/time.h>
#include <algorithm>
#include <cstring>
#include <limits>

static const char* TAG = "sync";

namespace lg {
namespace {

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool sequence_is_newer(uint32_t candidate, uint32_t previous) {
    return static_cast<int32_t>(candidate - previous) > 0;
}

uint32_t elapsed_ms(uint32_t now, uint32_t then) {
    return now - then;
}

uint32_t auto_election_delay(const uint8_t mac[6]) {
    constexpr uint32_t kAutoElectionBaseMs = 800;
    return kAutoElectionBaseMs +
        static_cast<uint32_t>(mac[5]) * 4U;
}

} // namespace

NodeSync::NodeSync(
    SignageEngine& engine,
    ConfigStore& config,
    ClockDiscipline& clock
) : engine_(engine), config_(config), clock_(clock) {}

esp_err_t NodeSync::start(NodeRole role) {
    if (task_handle_ != nullptr) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&status_mutex_);
    configured_role_ = role;
    active_role_ = role == NodeRole::Auto ? NodeRole::Worker : role;
    master_address_ = 0;
    std::memset(master_mac_, 0, sizeof(master_mac_));
    last_packet_ms_ = 0;
    last_sequence_ = 0;
    has_sequence_ = false;
    packets_received_ = 0;
    packets_missed_ = 0;
    std::memset(peers_, 0, sizeof(peers_));
    running_ = true;
    portEXIT_CRITICAL(&status_mutex_);

    TaskFunction_t task = worker_task_entry;
    const char* task_name = "sync_worker";
    if (role == NodeRole::Master) {
        task = master_task_entry;
        task_name = "sync_master";
    } else if (role == NodeRole::Auto) {
        task = auto_task_entry;
        task_name = "sync_auto";
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(
        task, task_name, 4096, this, 3, &task_handle_, 0
    );
    if (ok != pdPASS) {
        portENTER_CRITICAL(&status_mutex_);
        running_ = false;
        portEXIT_CRITICAL(&status_mutex_);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Started as %s (port %u)",
             role == NodeRole::Master ? "Master" :
             role == NodeRole::Worker ? "Worker" : "Auto",
             kSyncPort);
    return ESP_OK;
}

void NodeSync::stop() {
    // The task owns and closes its socket after the short receive timeout.
    // Avoid force-deleting a task while it is in a socket call.
    portENTER_CRITICAL(&status_mutex_);
    running_ = false;
    portEXIT_CRITICAL(&status_mutex_);
}

void NodeSync::master_task_entry(void* arg) {
    static_cast<NodeSync*>(arg)->master_task();
    vTaskDelete(nullptr);
}

void NodeSync::worker_task_entry(void* arg) {
    static_cast<NodeSync*>(arg)->worker_task();
    vTaskDelete(nullptr);
}

void NodeSync::auto_task_entry(void* arg) {
    static_cast<NodeSync*>(arg)->auto_task();
    vTaskDelete(nullptr);
}

bool NodeSync::open_socket(int& socket, bool broadcast) {
    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return false;
    }

    int reuse = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (broadcast) {
        int enabled = 1;
        setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(kSyncPort);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Bind failed on port %u", kSyncPort);
        close(socket);
        socket = -1;
        return false;
    }

    timeval timeout{};
    timeout.tv_usec = 50000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return true;
}

void NodeSync::master_task() {
    int socket = -1;
    if (open_socket(socket, true)) {
        sock_ = socket;
        master_loop(socket, false);
        close(socket);
    }
    sock_ = -1;
    portENTER_CRITICAL(&status_mutex_);
    running_ = false;
    portEXIT_CRITICAL(&status_mutex_);
    task_handle_ = nullptr;
}

void NodeSync::worker_task() {
    int socket = -1;
    if (open_socket(socket, false)) {
        sock_ = socket;
        worker_loop(socket, false);
        close(socket);
    }
    sock_ = -1;
    portENTER_CRITICAL(&status_mutex_);
    running_ = false;
    portEXIT_CRITICAL(&status_mutex_);
    task_handle_ = nullptr;
}

void NodeSync::auto_task() {
    int socket = -1;
    if (open_socket(socket, true)) {
        sock_ = socket;
        NodeRole next_role = NodeRole::Worker;
        for (;;) {
            portENTER_CRITICAL(&status_mutex_);
            const bool running = running_;
            portEXIT_CRITICAL(&status_mutex_);
            if (!running) break;

            next_role = next_role == NodeRole::Master
                ? master_loop(socket, true)
                : worker_loop(socket, true);
        }
        close(socket);
    }
    sock_ = -1;
    portENTER_CRITICAL(&status_mutex_);
    running_ = false;
    portEXIT_CRITICAL(&status_mutex_);
    task_handle_ = nullptr;
}

NodeRole NodeSync::master_loop(int socket, bool auto_mode) {
    uint8_t local_mac[6] = {};
    esp_read_mac(local_mac, ESP_MAC_WIFI_STA);

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(kSyncPort);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    uint32_t sequence = 0;
    uint32_t next_heartbeat_ms = now_ms();

    for (;;) {
        portENTER_CRITICAL(&status_mutex_);
        const bool running = running_;
        portEXIT_CRITICAL(&status_mutex_);
        if (!running) break;

        const uint32_t now = now_ms();
        if (static_cast<int32_t>(now - next_heartbeat_ms) >= 0) {
            Sequence sequence_state{};
            config_.load_sequence(sequence_state);

            SyncPacket packet{};
            packet.magic = kMagic;
            packet.version = kPacketVersion;
            packet.type = PacketType::Heartbeat;
            packet.sequence = sequence++;
            packet.bpm = sequence_state.bpm;
            packet.sequence_enabled = sequence_state.enabled;
            packet.flags = clock_.has_epoch() ? 0x01 : 0x00;
            packet.global_clock_ms = clock_.now_ms();
            packet.sent_monotonic_ms = now;
            std::memcpy(packet.sender_mac, local_mac, sizeof(local_mac));

            sendto(socket, &packet, sizeof(packet), 0,
                   reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
            next_heartbeat_ms = now + kSyncIntervalMs;
        }

        SyncPacket packet{};
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        const int received = recvfrom(
            socket,
            &packet,
            sizeof(packet),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &sender_len
        );
        if (received != sizeof(SyncPacket) ||
            packet.magic != kMagic ||
            packet.version != kPacketVersion) {
            continue;
        }

        if (packet.type == PacketType::Acknowledgement) {
            const uint32_t round_trip = elapsed_ms(now_ms(), packet.sent_monotonic_ms);
            update_peer(sender, static_cast<uint16_t>(
                std::min<uint32_t>(round_trip, std::numeric_limits<uint16_t>::max())
            ));
            continue;
        }

        if (auto_mode &&
            packet.type == PacketType::Heartbeat &&
            std::memcmp(packet.sender_mac, local_mac, sizeof(local_mac)) < 0) {
            ESP_LOGI(TAG, "Auto role yielding to lower-priority leader");
            portENTER_CRITICAL(&status_mutex_);
            active_role_ = NodeRole::Worker;
            portEXIT_CRITICAL(&status_mutex_);
            reset_master();
            return NodeRole::Worker;
        }
    }

    return NodeRole::Master;
}

NodeRole NodeSync::worker_loop(int socket, bool auto_mode) {
    uint8_t local_mac[6] = {};
    esp_read_mac(local_mac, ESP_MAC_WIFI_STA);
    const uint32_t started_ms = now_ms();

    for (;;) {
        portENTER_CRITICAL(&status_mutex_);
        const bool running = running_;
        const uint32_t last_packet_ms = last_packet_ms_;
        portEXIT_CRITICAL(&status_mutex_);
        if (!running) break;

        SyncPacket packet{};
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);
        const int received = recvfrom(
            socket,
            &packet,
            sizeof(packet),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &sender_len
        );
        if (received == sizeof(SyncPacket) &&
            packet.magic == kMagic &&
            packet.version == kPacketVersion &&
            packet.type == PacketType::Heartbeat &&
            std::memcmp(packet.sender_mac, local_mac, sizeof(local_mac)) != 0 &&
            handle_heartbeat(packet, sender)) {
            send_ack(socket, packet, sender);
        }

        if (auto_mode) {
            const uint32_t now = now_ms();
            portENTER_CRITICAL(&status_mutex_);
            const uint32_t updated_last_packet_ms = last_packet_ms_;
            portEXIT_CRITICAL(&status_mutex_);
            const uint32_t reference = updated_last_packet_ms == 0
                ? started_ms
                : updated_last_packet_ms;
            if (elapsed_ms(now, reference) >= auto_election_delay(local_mac)) {
                ESP_LOGI(TAG, "Auto role elected this node as leader");
                portENTER_CRITICAL(&status_mutex_);
                active_role_ = NodeRole::Master;
                portEXIT_CRITICAL(&status_mutex_);
                reset_master();
                return NodeRole::Master;
            }
        }
    }

    return NodeRole::Worker;
}

bool NodeSync::handle_heartbeat(
    const SyncPacket& packet,
    const sockaddr_in& sender
) {
    const uint32_t now = now_ms();
    bool accepted = false;

    portENTER_CRITICAL(&status_mutex_);
    const bool master_stale = master_address_ == 0 ||
        elapsed_ms(now, last_packet_ms_) > kPeerTimeoutMs;
    const bool preferred_auto_master =
        configured_role_ == NodeRole::Auto &&
        std::memcmp(packet.sender_mac, master_mac_, sizeof(master_mac_)) < 0;

    if (master_stale || preferred_auto_master) {
        master_address_ = sender.sin_addr.s_addr;
        std::memcpy(master_mac_, packet.sender_mac, sizeof(master_mac_));
        has_sequence_ = false;
    }

    const bool is_current_master =
        master_address_ == sender.sin_addr.s_addr &&
        std::memcmp(master_mac_, packet.sender_mac, sizeof(master_mac_)) == 0;
    if (is_current_master &&
        (!has_sequence_ || sequence_is_newer(packet.sequence, last_sequence_))) {
        if (has_sequence_) {
            const uint32_t skipped = packet.sequence - last_sequence_ - 1;
            packets_missed_ += skipped;
        }
        last_sequence_ = packet.sequence;
        has_sequence_ = true;
        last_packet_ms_ = now;
        ++packets_received_;
        accepted = true;
    }
    portEXIT_CRITICAL(&status_mutex_);

    if (!accepted) return false;

    clock_.discipline(packet.global_clock_ms, (packet.flags & 0x01) != 0);
    engine_.feed_watchdog();
    return true;
}

void NodeSync::send_ack(
    int socket,
    const SyncPacket& heartbeat,
    const sockaddr_in& sender
) {
    uint8_t local_mac[6] = {};
    esp_read_mac(local_mac, ESP_MAC_WIFI_STA);

    SyncPacket acknowledgement{};
    acknowledgement.magic = kMagic;
    acknowledgement.version = kPacketVersion;
    acknowledgement.type = PacketType::Acknowledgement;
    acknowledgement.sequence = heartbeat.sequence;
    acknowledgement.flags = clock_.has_epoch() ? 0x01 : 0x00;
    acknowledgement.global_clock_ms = clock_.now_ms();
    acknowledgement.sent_monotonic_ms = heartbeat.sent_monotonic_ms;
    std::memcpy(acknowledgement.sender_mac, local_mac, sizeof(local_mac));

    sendto(socket, &acknowledgement, sizeof(acknowledgement), 0,
           reinterpret_cast<const sockaddr*>(&sender), sizeof(sender));
}

void NodeSync::update_peer(const sockaddr_in& sender, uint16_t latency_ms) {
    const uint32_t now = now_ms();

    portENTER_CRITICAL(&status_mutex_);
    Peer* available = nullptr;
    for (Peer& peer : peers_) {
        if (peer.address == sender.sin_addr.s_addr) {
            available = &peer;
            break;
        }
        if (available == nullptr && peer.address == 0) {
            available = &peer;
        }
    }
    if (available == nullptr) {
        available = &peers_[0];
        for (Peer& peer : peers_) {
            if (peer.last_seen_ms < available->last_seen_ms) available = &peer;
        }
    }

    available->address = sender.sin_addr.s_addr;
    available->last_seen_ms = now;
    available->latency_ms = latency_ms;
    portEXIT_CRITICAL(&status_mutex_);
}

void NodeSync::reset_master() {
    portENTER_CRITICAL(&status_mutex_);
    master_address_ = 0;
    std::memset(master_mac_, 0, sizeof(master_mac_));
    last_packet_ms_ = 0;
    last_sequence_ = 0;
    has_sequence_ = false;
    portEXIT_CRITICAL(&status_mutex_);
}

NodeRole NodeSync::active_role() const {
    portENTER_CRITICAL(&status_mutex_);
    const NodeRole role = active_role_;
    portEXIT_CRITICAL(&status_mutex_);
    return role;
}

NodeSyncStatus NodeSync::status() const {
    const uint32_t now = now_ms();
    const ClockSyncStatus clock_status = clock_.status();
    NodeSyncStatus result;

    portENTER_CRITICAL(&status_mutex_);
    result.configured_role = configured_role_;
    result.active_role = active_role_;
    result.running = running_;
    result.packets_received = packets_received_;
    result.packets_missed = packets_missed_;
    result.last_packet_age_ms = last_packet_ms_ == 0
        ? std::numeric_limits<uint32_t>::max()
        : elapsed_ms(now, last_packet_ms_);

    for (const Peer& peer : peers_) {
        if (peer.address == 0 || elapsed_ms(now, peer.last_seen_ms) > kPeerTimeoutMs) {
            continue;
        }
        SyncPeerStatus& output = result.peers[result.peer_count++];
        output.address = peer.address;
        output.age_ms = elapsed_ms(now, peer.last_seen_ms);
        output.latency_ms = peer.latency_ms;
    }
    portEXIT_CRITICAL(&status_mutex_);

    result.clock_locked = clock_status.locked;
    result.clock_has_epoch = clock_status.epoch_valid;
    result.clock_offset_ms = clock_status.offset_ms;
    result.clock_jitter_us = clock_status.jitter_us;
    return result;
}

} // namespace lg
