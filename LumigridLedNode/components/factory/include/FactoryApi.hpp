// components/factory/include/FactoryApi.hpp
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "FactoryRunner.hpp"
#include "CommandReceipt.hpp"
#include "JobManager.hpp"
#include "NodeIdentity.hpp"
#include "NetworkSettings.hpp"
#include <atomic>
#include <functional>

struct cJSON;

// Forward declarations for signage components
namespace lg {
class ConfigStore;
struct DeviceConfig;
class LightingRuntime;
class Scheduler;
class NodeSync;
class DmxRouter;
class ShowTransportCoordinator;
class WifiService;
}

namespace lg {

struct StartupDiagnostics {
    const char* startup_state = "Booting";
    const char* startup_stage = "None";
    const char* failing_stage = "None";
    int failing_error = 0;
};

struct RuntimeHealthSnapshot {
    static constexpr int kSchemaVersion = 2;
    static constexpr size_t kMaxUniverses = 16;

    struct System {
        uint32_t uptime_s = 0;
        uint32_t free_heap = 0;
        uint32_t min_free_heap = 0;
        int reset_reason = 0;
        float fps = 0.0f;
    } system;

    struct Network {
        int wifi_rssi = 0;
    } network;

    struct Nvs {
        int used_entries = 0;
        int free_entries = 0;
    } nvs;

    struct Config {
        bool valid = true;
        uint32_t error_count = 0;
        const char* first_error = "";
        const char* persistence_state = "Unknown";
        bool primary_valid = false;
        bool backup_valid = false;
    } config;

    struct Startup {
        const char* state = "Booting";
        const char* stage = "None";
        const char* failing_stage = "None";
        int failing_error = 0;
    } startup;

    struct Output {
        const char* state = "Healthy";
        bool fault = false;
        uint32_t fault_events = 0;
        uint32_t failed_commits = 0;
        int last_error = 0;
        bool estop_latched = false;
        bool runtime_estop = false;
        int control_source = 0;
        uint8_t live_objects = 0;
        uint32_t current_power_mw = 0;
        uint32_t throttling_events = 0;
        uint16_t active_preset = 0;
        bool watchdog_fallback = false;
    } output;

    struct Dmx {
        uint32_t sources_active = 0;
        uint32_t universes_active = 0;
        uint32_t sources_dropped = 0;
        uint32_t packets_accepted = 0;
        uint32_t packets_rejected_seq = 0;
        uint32_t packets_ignored_unpatched = 0;
        uint32_t submission_failures = 0;
        uint16_t configured_universes[kMaxUniverses] = {};
        size_t universe_count = 0;
    } dmx;

    struct Sync {
        uint32_t packet_loss = 0;
    } sync;

    void collect(class FactoryApi* api);
    cJSON* to_json() const;
};

class FactoryApi {
public:
    explicit FactoryApi(FactoryRunner& runner);

    /// Start HTTP servers. Separate signage and factory APIs by port.
    esp_err_t start(uint16_t signage_port = 80,
                    uint16_t factory_port = 8080,
                    ConfigStore* config = nullptr,
                    LightingRuntime* signage = nullptr,
                    Scheduler* scheduler = nullptr,
                    NodeSync* node_sync = nullptr,
                    DmxRouter* dmx_router = nullptr,
                    class CueRunner* cue_runner = nullptr,
                    class ShowTransportCoordinator* show_transport = nullptr);
    void stop();

    void set_diagnostics_provider(std::function<StartupDiagnostics()> provider) {
        diagnostics_provider_ = std::move(provider);
    }
    const std::function<StartupDiagnostics()>& diagnostics_provider() const {
        return diagnostics_provider_;
    }

    FactoryRunner& runner() { return runner_; }
    ConfigStore* config() { return config_; }
    LightingRuntime* signage() { return signage_; }
    Scheduler* scheduler() { return scheduler_; }
    NodeSync* node_sync() { return node_sync_; }
    class CueRunner* cue_runner() { return cue_runner_; }
    class ShowTransportCoordinator* show_transport() { return show_transport_; }
    void set_show_transport(class ShowTransportCoordinator* transport) { show_transport_ = transport; }
    DmxRouter* dmx_router() { return dmx_router_; }
    const DmxRouter* dmx_router() const { return dmx_router_; }
    WifiService* wifi() { return wifi_; }
    const WifiService* wifi() const { return wifi_; }
    void set_wifi(WifiService* wifi) { wifi_ = wifi; }

    using ShowClockProvider = std::function<uint64_t()>;
    void set_show_clock_provider(ShowClockProvider provider) { show_clock_provider_ = std::move(provider); }
    uint64_t show_clock_now_ms() const { return show_clock_provider_ ? show_clock_provider_() : (esp_timer_get_time() / 1000ULL); }

    using RuntimeConfigApplyCallback = std::function<esp_err_t(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg)>;
    void set_config_apply_callback(RuntimeConfigApplyCallback callback) {
        config_apply_callback_ = std::move(callback);
    }
    const RuntimeConfigApplyCallback& config_apply_callback() const {
        return config_apply_callback_;
    }

    using NetworkApplyQueueFunc = std::function<esp_err_t(const NetworkApplyRequest&)>;
    using NetworkApplyStatusFunc = std::function<void(NetworkApplyState& state, uint32_t& cmd_id, const char*& err_str)>;
    using SafeModeCheckFunc = std::function<bool()>;

    void set_network_apply_handler(NetworkApplyQueueFunc queue_func,
                                   NetworkApplyStatusFunc status_func,
                                   SafeModeCheckFunc safe_mode_func) {
        network_apply_queue_ = std::move(queue_func);
        network_apply_status_ = std::move(status_func);
        safe_mode_check_ = std::move(safe_mode_func);
    }

    void notify_status_async();

    static void ws_broadcast_work(void* arg);
    static void ws_broadcast_preview_work(void* arg);
    static esp_err_t serve_dashboard(httpd_req_t* request);

    CommandJournal& command_journal() { return command_journal_; }
    JobManager& job_manager() { return job_manager_; }
    uint32_t next_ws_seq() { return ws_seq_.fetch_add(1, std::memory_order_relaxed); }
    const NodeIdentity& identity() const { return identity_; }
    void set_identity(const NodeIdentity& id) { identity_ = id; }

    FactoryRunner& runner_;
    ConfigStore* config_ = nullptr;
    LightingRuntime* signage_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    NodeSync* node_sync_ = nullptr;
    DmxRouter* dmx_router_ = nullptr;
    class CueRunner* cue_runner_ = nullptr;
    class ShowTransportCoordinator* show_transport_ = nullptr;
    WifiService* wifi_ = nullptr;
    ShowClockProvider show_clock_provider_{};
    std::function<StartupDiagnostics()> diagnostics_provider_{};
    RuntimeConfigApplyCallback config_apply_callback_{};
    httpd_handle_t signage_server_ = nullptr;
    httpd_handle_t factory_server_ = nullptr;
    size_t max_open_sockets_ = 0;
    std::atomic<bool> preview_pending_{false};
    CommandJournal command_journal_{};
    JobManager job_manager_{};
    std::atomic<uint32_t> ws_seq_{1};
    NodeIdentity identity_{};
    NetworkApplyQueueFunc network_apply_queue_{};
    NetworkApplyStatusFunc network_apply_status_{};
    SafeModeCheckFunc safe_mode_check_{};
};

} // namespace lg
