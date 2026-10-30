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

// Reject overlapping/reentrant lifecycle operations instead of deadlocking a
// worker callback or allowing two starts to publish competing task owners.
class LifecycleGuard {
public:
    explicit LifecycleGuard(std::atomic_flag& busy)
        : busy_(busy), owned_(!busy_.test_and_set(std::memory_order_acquire)) {}
    ~LifecycleGuard() { if (owned_) busy_.clear(std::memory_order_release); }
    bool owned() const { return owned_; }
private:
    std::atomic_flag& busy_;
    bool owned_;
};

int receive_sync_packet(int socket, NodeSync::SyncPacket& packet, sockaddr_in& sender) {
    // One extra byte distinguishes a valid datagram from a truncated oversized
    // one. Reading into a packet-sized buffer silently accepted valid prefixes.
    uint8_t bytes[sizeof(NodeSync::SyncPacket) + 1]{};
    socklen_t sender_len = sizeof(sender);
    const int received = recvfrom(socket, bytes, sizeof(bytes), 0,
        reinterpret_cast<sockaddr*>(&sender), &sender_len);
    if (received == static_cast<int>(sizeof(packet))) {
        std::memcpy(&packet, bytes, sizeof(packet));
    }
    return received;
}

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
    LightingRuntime& engine,
    ConfigStore& config,
    ClockDiscipline& clock
) : engine_(engine), config_(config), clock_(clock) {
    shutdown_sem_ = xSemaphoreCreateBinary();
}

NodeSync::~NodeSync() {
    // Object destruction cannot safely abandon a worker that can still touch
    // this object. Normal callers use bounded stop(); destruction joins it.
    while (stop(1500) != ESP_OK) {
        ESP_LOGW(TAG, "Waiting for NodeSync ownership to drain before destruction");
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (shutdown_sem_) {
        vSemaphoreDelete(shutdown_sem_);
        shutdown_sem_ = nullptr;
    }
}

void NodeSync::refresh_config_cache() {
    const uint32_t now = now_ms();
    if (last_config_cache_ms_ != 0 && now - last_config_cache_ms_ < kConfigCacheIntervalMs) return;
    DeviceConfig cfg{};
    config_.load_config(cfg);
    cached_sync_token_ = cfg.sync_token;
    cached_sync_enabled_ = cfg.sync_enabled;
    last_config_cache_ms_ = now;
}

void NodeSync::reset_config_cache() {
    portENTER_CRITICAL(&status_mutex_);
    last_config_cache_ms_ = 0;
    cached_sync_token_ = 0;
    cached_sync_enabled_ = false;
    portEXIT_CRITICAL(&status_mutex_);
    refresh_config_cache();
}

esp_err_t NodeSync::start(NodeRole role) {
    LifecycleGuard guard(lifecycle_busy_);
    if (!guard.owned()) return ESP_ERR_INVALID_STATE;
    if (role != NodeRole::Auto && role != NodeRole::Master && role != NodeRole::Worker)
        return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&status_mutex_);
    const bool owned = task_handle_ != nullptr || stopping_;
    portEXIT_CRITICAL(&status_mutex_);
    if (owned) return ESP_ERR_INVALID_STATE;

    if (shutdown_sem_ == nullptr) shutdown_sem_ = xSemaphoreCreateBinary();
    if (shutdown_sem_ == nullptr) return ESP_ERR_NO_MEM;
    // No worker is owned here. Discard an obsolete completion token before
    // starting a new generation; it must never acknowledge the next worker.
    while (xSemaphoreTake(shutdown_sem_, 0) == pdTRUE) {}
    reset_config_cache();

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
    remote_transport_snapshot_ = {};
    local_transport_snapshot_ = {};
    remote_timeline_enabled_ = false;
    remote_bpm_ = 0;
    transport_apply_ok_ = true;
    transport_error_ = 0;
    for (auto& peer : peers_) peer = Peer{};
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

    TaskHandle_t created_task = nullptr;
    const BaseType_t ok = xTaskCreatePinnedToCore(
        task, task_name, 4096, this, 3, &created_task, 0
    );
    if (ok != pdPASS) {
        portENTER_CRITICAL(&status_mutex_);
        running_ = false;
        task_handle_ = nullptr;
        portEXIT_CRITICAL(&status_mutex_);
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&status_mutex_);
    task_handle_ = created_task;
    portEXIT_CRITICAL(&status_mutex_);

    ESP_LOGI(TAG, "Started as %s (port %u)",
             role == NodeRole::Master ? "Master" :
             role == NodeRole::Worker ? "Worker" : "Auto",
             kSyncPort);
    return ESP_OK;
}

esp_err_t NodeSync::stop(uint32_t timeout_ms) {
    LifecycleGuard guard(lifecycle_busy_);
    if (!guard.owned()) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&status_mutex_);
    const TaskHandle_t owned_task = task_handle_;
    if (owned_task == nullptr && !stopping_) {
        portEXIT_CRITICAL(&status_mutex_);
        return ESP_OK;
    }
    stopping_ = true;
    running_ = false;
    portEXIT_CRITICAL(&status_mutex_);

    // The worker owns its socket through close(). No cross-task shutdown of a
    // descriptor that may already have been closed and reused by another service.
    // Validated bounded receive/send waits allow cooperative exit.
    if (owned_task != nullptr) {
        if (xTaskGetCurrentTaskHandle() == owned_task) return ESP_ERR_INVALID_STATE;
        if (shutdown_sem_ == nullptr) return ESP_ERR_INVALID_STATE;
        if (xSemaphoreTake(shutdown_sem_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            ESP_LOGW(TAG, "NodeSync task did not exit within %ums; ownership retained",
                     static_cast<unsigned>(timeout_ms));
            return ESP_ERR_TIMEOUT;
        }
        portENTER_CRITICAL(&status_mutex_);
        task_handle_ = nullptr;
        portEXIT_CRITICAL(&status_mutex_);
    }

    // Release remote cue ownership as well as remote scheduler state. If that
    // callback rejects cleanup, retain stopping_ and allow a later stop to retry.
    // Never call application code while holding the status spinlock.
    ShowTransportSnapshot empty_snap{};
    empty_snap.mode = ShowTransportMode::Standalone;
    if (remote_transport_callback_) {
        uint8_t error = 0;
        if (!remote_transport_callback_(empty_snap, error)) {
            portENTER_CRITICAL(&status_mutex_);
            transport_apply_ok_ = false;
            transport_error_ = error != 0 ? error :
                static_cast<uint8_t>(SyncTransportError::RuntimeUnavailable);
            portEXIT_CRITICAL(&status_mutex_);
            return ESP_FAIL;
        }
    }
    if (transport_callback_) transport_callback_(empty_snap);
    if (legacy_transport_callback_) legacy_transport_callback_(false, 120);

    portENTER_CRITICAL(&status_mutex_);
    master_address_ = 0;
    std::memset(master_mac_, 0, sizeof(master_mac_));
    last_packet_ms_ = 0;
    has_sequence_ = false;
    remote_timeline_enabled_ = false;
    remote_bpm_ = 0;
    remote_transport_snapshot_ = {};
    local_transport_snapshot_ = {};
    active_role_ = NodeRole::Auto;
    transport_apply_ok_ = true;
    transport_error_ = 0;
    last_config_cache_ms_ = 0;
    cached_sync_token_ = 0;
    cached_sync_enabled_ = false;
    for (auto& peer : peers_) peer = Peer{};
    stopping_ = false;
    portEXIT_CRITICAL(&status_mutex_);
    return ESP_OK;
}

bool NodeSync::should_run() const {
    portENTER_CRITICAL(&status_mutex_);
    const bool running = running_;
    portEXIT_CRITICAL(&status_mutex_);
    return running;
}

void NodeSync::finish_task() {
    portENTER_CRITICAL(&status_mutex_);
    running_ = false;
    const SemaphoreHandle_t done = shutdown_sem_;
    portEXIT_CRITICAL(&status_mutex_);
    // Final access to instance-owned resources. After giving this semaphore the
    // owner may finish stop() or destroy the object; the entry only self-deletes.
    xSemaphoreGive(done);
}

void NodeSync::master_task_entry(void* arg) {
    auto* self = static_cast<NodeSync*>(arg);
    self->master_task();
    self->finish_task();
    vTaskDelete(nullptr);
}

void NodeSync::worker_task_entry(void* arg) {
    auto* self = static_cast<NodeSync*>(arg);
    self->worker_task();
    self->finish_task();
    vTaskDelete(nullptr);
}

void NodeSync::auto_task_entry(void* arg) {
    auto* self = static_cast<NodeSync*>(arg);
    self->auto_task();
    self->finish_task();
    vTaskDelete(nullptr);
}

bool NodeSync::open_socket(int& socket, bool broadcast) {
    socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return false;
    }
    const auto fail = [&]() {
        close(socket);
        socket = -1;
        return false;
    };
    int enabled = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0)
        return fail();
    if (broadcast && setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0)
        return fail();

    // A silent receiver must not halve the requested 20 Hz manual-fade cadence.
    timeval timeout{};
    timeout.tv_usec = kSyncManualIntervalMs * 1000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to bound sync socket waits");
        return fail();
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(kSyncPort);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket to port %u", kSyncPort);
        return fail();
    }
    return true;
}

void NodeSync::reset_master() {
    portENTER_CRITICAL(&status_mutex_);
    master_address_ = 0;
    std::memset(master_mac_, 0, sizeof(master_mac_));
    last_packet_ms_ = 0;
    has_sequence_ = false;
    portEXIT_CRITICAL(&status_mutex_);
}

void NodeSync::master_task() {
    int socket = -1;
    if (open_socket(socket, true)) {
        master_loop(socket, false);
        close(socket);
    }
}

void NodeSync::worker_task() {
    int socket = -1;
    if (open_socket(socket, false)) {
        worker_loop(socket, false);
        close(socket);
    }
}

void NodeSync::auto_task() {
    int socket = -1;
    if (open_socket(socket, true)) {
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
            refresh_config_cache();

            ShowTransportSnapshot transport{};
            if (transport_provider_) {
                transport = transport_provider_(clock_.now_ms());
            } else {
                transport.active_scene = engine_.active_scene_index();
                transport.mode = ShowTransportMode::Scene;
            }

            if (!should_run()) break;
            portENTER_CRITICAL(&status_mutex_);
            local_transport_snapshot_ = transport;
            portEXIT_CRITICAL(&status_mutex_);

            SyncPacket packet{};
            packet.magic = kMagic;
            packet.version = kPacketVersion;
            packet.type = PacketType::Heartbeat;
            packet.sequence = sequence++;

            packet.transport = static_cast<uint8_t>(transport.mode);
            packet.active_scene = transport.active_scene;
            packet.bpm = transport.bpm;

            packet.cue_list = transport.cue_list;
            packet.cue_index = transport.cue_index;
            packet.cue_next = transport.cue_next;
            packet.cue_state = static_cast<uint8_t>(transport.cue_state);

            packet.cue_generation = transport.cue_generation;
            packet.cue_elapsed_ms = transport.cue_elapsed_ms;
            packet.manual_fade_q15 = transport.manual_fade_q15;

            packet.flags = (clock_.has_epoch() ? 0x01 : 0x00) |
                           (cached_sync_enabled_ ? 0x02 : 0x00);
            packet.reserved = 0;

            packet.global_clock_ms = clock_.now_ms();
            packet.sent_monotonic_ms = now;
            std::memcpy(packet.sender_mac, local_mac, sizeof(local_mac));
            packet.token = cached_sync_token_;

            // Protocol v5 extensions
            packet.transport_generation = transport.transport_generation;
            packet.timeline_anchor_ms = transport.timeline_anchor_ms;
            packet.timeline_checksum = transport.timeline_active_checksum;

            // Protocol v6 extensions (LG-034 §28): cue parity + frozen fade
            packet.cue_list_checksum = transport.cue_list_checksum;
            packet.cue_transition_progress_q15 = transport.cue_transition_progress_q15;
            packet.cue_paused_from = transport.cue_paused_from;
            packet.cue_source_scene = transport.cue_source_scene;

            sendto(socket, &packet, sizeof(packet), 0,
                   reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));

            // Phase 5.6: Heartbeat cadence increases to 20 Hz during manual crossfade
            const uint32_t interval = (transport.mode == ShowTransportMode::Cue && transport.cue_state == CueRunnerState::Manual)
                ? kSyncManualIntervalMs
                : kSyncIntervalMs;
            next_heartbeat_ms = now + interval;
        }

        SyncPacket packet{};
        sockaddr_in sender{};
        const int received = receive_sync_packet(socket, packet, sender);
        // Stop may have been requested during a blocking socket call. Do not
        // admit another heartbeat or callback after observing that request.
        if (!should_run()) break;
        if (received != sizeof(SyncPacket) ||
            packet.magic != kMagic ||
            packet.version != kPacketVersion) {
            continue;
        }

        refresh_config_cache();
        if (packet.token != cached_sync_token_) {
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
        portEXIT_CRITICAL(&status_mutex_);
        if (!running) break;

        SyncPacket packet{};
        sockaddr_in sender{};
        const int received = receive_sync_packet(socket, packet, sender);
        // Stop may have been requested during a blocking socket call. Do not
        // admit another heartbeat or callback after observing that request.
        if (!should_run()) break;
        if (received == sizeof(SyncPacket) &&
            packet.magic == kMagic &&
            packet.version == kPacketVersion &&
            packet.type == PacketType::Heartbeat &&
            std::memcmp(packet.sender_mac, local_mac, sizeof(local_mac)) != 0) {

            refresh_config_cache();
            if (packet.token == cached_sync_token_ && handle_heartbeat(packet, sender)) {
                send_ack(socket, packet, sender, cached_sync_token_);
            }
        }

        const uint32_t now = now_ms();
        portENTER_CRITICAL(&status_mutex_);
        const bool timed_out = (last_packet_ms_ != 0 && elapsed_ms(now, last_packet_ms_) > kPeerTimeoutMs);
        if (timed_out && (master_address_ != 0 || remote_timeline_enabled_ || transport_apply_ok_)) {
            // Phase 0.6: Timeout must clear every remote transport
            master_address_ = 0;
            std::memset(master_mac_, 0, sizeof(master_mac_));
            remote_timeline_enabled_ = false;
            remote_bpm_ = 0;
            remote_transport_snapshot_ = {};
            transport_apply_ok_ = false;
            transport_error_ = 0;
            portEXIT_CRITICAL(&status_mutex_);

            if (remote_transport_callback_) {
                uint8_t dummy = 0;
                ShowTransportSnapshot empty_snap{};
                empty_snap.mode = ShowTransportMode::Standalone;
                remote_transport_callback_(empty_snap, dummy);
            }
            if (transport_callback_) {
                ShowTransportSnapshot empty_snap{};
                empty_snap.mode = ShowTransportMode::Standalone;
                transport_callback_(empty_snap);
            }
            if (legacy_transport_callback_) {
                legacy_transport_callback_(false, 120);
            }
        } else {
            portEXIT_CRITICAL(&status_mutex_);
        }

        if (auto_mode) {
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

    ShowTransportSnapshot remote_snap{};
    remote_snap.mode = static_cast<ShowTransportMode>(packet.transport);
    remote_snap.active_scene = packet.active_scene;
    remote_snap.bpm = packet.bpm;
    remote_snap.cue_list = packet.cue_list;
    remote_snap.cue_index = packet.cue_index;
    remote_snap.cue_next = packet.cue_next;
    remote_snap.cue_state = static_cast<CueRunnerState>(packet.cue_state);
    remote_snap.cue_generation = packet.cue_generation;
    remote_snap.cue_elapsed_ms = packet.cue_elapsed_ms;
    remote_snap.manual_fade_q15 = packet.manual_fade_q15;

    // Protocol v5 extensions
    remote_snap.transport_generation = packet.transport_generation;
    remote_snap.timeline_anchor_ms = packet.timeline_anchor_ms;
    remote_snap.timeline_active_checksum = packet.timeline_checksum;

    // Protocol v6 extensions (LG-034 §28)
    remote_snap.cue_list_checksum = packet.cue_list_checksum;
    remote_snap.cue_transition_progress_q15 = packet.cue_transition_progress_q15;
    remote_snap.cue_paused_from = packet.cue_paused_from;
    remote_snap.cue_source_scene = packet.cue_source_scene;

    // Backward-compat cue struct
    remote_snap.cue.active = (packet.cue_state != 0);
    remote_snap.cue.list_index = packet.cue_list;
    remote_snap.cue.state = packet.cue_state;
    remote_snap.cue.current_cue = packet.cue_index;
    remote_snap.cue.next_cue = packet.cue_next;
    remote_snap.cue.manual_fade_q15 = packet.manual_fade_q15;
    remote_snap.cue.cue_anchor_ms = packet.global_clock_ms;
    remote_snap.cue.paused_elapsed_ms = packet.cue_elapsed_ms;
    remote_snap.cue.generation = packet.cue_generation;

    uint8_t err_code = 0;
    bool apply_ok = true;

    if (remote_transport_callback_) {
        apply_ok = remote_transport_callback_(remote_snap, err_code);
    } else {
        if (remote_snap.mode == ShowTransportMode::Timeline) {
            if (legacy_transport_callback_) legacy_transport_callback_(true, remote_snap.bpm);
        } else {
            if (legacy_transport_callback_) legacy_transport_callback_(false, remote_snap.bpm);
            if (!engine_.activate_scene_checked(remote_snap.active_scene)) {
                apply_ok = false;
                err_code = static_cast<uint8_t>(SyncTransportError::SceneMissing);
            }
        }
    }

    portENTER_CRITICAL(&status_mutex_);
    remote_transport_snapshot_ = remote_snap;
    transport_apply_ok_ = apply_ok;
    transport_error_ = err_code;
    remote_bpm_ = packet.bpm;
    remote_timeline_enabled_ = (remote_snap.mode == ShowTransportMode::Timeline);
    portEXIT_CRITICAL(&status_mutex_);

    return apply_ok;
}

void NodeSync::send_ack(
    int socket,
    const SyncPacket& heartbeat,
    const sockaddr_in& sender,
    uint32_t token
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
    acknowledgement.token = token;
    acknowledgement.transport = heartbeat.transport;
    acknowledgement.cue_generation = heartbeat.cue_generation;
    acknowledgement.transport_generation = heartbeat.transport_generation;
    acknowledgement.timeline_anchor_ms = heartbeat.timeline_anchor_ms;
    acknowledgement.timeline_checksum = heartbeat.timeline_checksum;
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

    if (available != nullptr) {
        available->address = sender.sin_addr.s_addr;
        available->last_seen_ms = now;
        available->latency_ms = latency_ms;
    }
    portEXIT_CRITICAL(&status_mutex_);
}

NodeRole NodeSync::role() const {
    portENTER_CRITICAL(&status_mutex_);
    const NodeRole role = configured_role_;
    portEXIT_CRITICAL(&status_mutex_);
    return role;
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
    result.last_packet_age_ms = !has_sequence_
        ? std::numeric_limits<uint32_t>::max()
        : elapsed_ms(now, last_packet_ms_);

    result.master_address = (active_role_ == NodeRole::Master) ? 0 : master_address_;

    if (!running_) {
        result.transport = 0; // standalone
        result.transport_apply_ok = !stopping_;
        result.transport_error = transport_error_;
    } else if (active_role_ == NodeRole::Master) {
        {
            const auto& local_snap = local_transport_snapshot_;
            result.transport = static_cast<uint8_t>(local_snap.mode);
            result.transport_generation = local_snap.transport_generation;
            result.remote_bpm = local_snap.bpm;
            result.remote_timeline_enabled = (local_snap.mode == ShowTransportMode::Timeline);
            result.cue.active = (local_snap.mode == ShowTransportMode::Cue);
            result.cue.list_index = local_snap.cue_list;
            result.cue.state = static_cast<uint8_t>(local_snap.cue_state);
            result.cue.current_cue = local_snap.cue_index;
            result.cue.next_cue = local_snap.cue_next;
            result.cue.generation = local_snap.cue_generation;
            result.cue.paused_elapsed_ms = local_snap.cue_elapsed_ms;
            result.cue.manual_fade_q15 = local_snap.manual_fade_q15;
        }
        result.transport_apply_ok = true;
        result.transport_error = 0;
    } else {
        result.transport = static_cast<uint8_t>(remote_transport_snapshot_.mode);
        result.transport_generation = remote_transport_snapshot_.transport_generation;
        result.remote_bpm = remote_transport_snapshot_.bpm;
        result.remote_timeline_enabled = (remote_transport_snapshot_.mode == ShowTransportMode::Timeline);
        result.transport_apply_ok = transport_apply_ok_;
        result.transport_error = transport_error_;
        result.cue = remote_transport_snapshot_.cue;
    }

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

    result.clock_locked = result.running && clock_status.locked;
    result.clock_has_epoch = clock_status.epoch_valid;
    result.clock_offset_ms = clock_status.offset_ms;
    result.clock_jitter_us = clock_status.jitter_us;
    return result;
}

} // namespace lg
