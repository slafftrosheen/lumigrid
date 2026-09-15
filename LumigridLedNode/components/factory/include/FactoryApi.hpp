// components/factory/include/FactoryApi.hpp
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "FactoryRunner.hpp"
#include <atomic>

struct cJSON;

// Forward declarations for signage components
namespace lg {
class ConfigStore;
class LightingRuntime;
class Scheduler;
class NodeSync;
class DmxRouter;
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
                    DmxRouter* dmx_router = nullptr);
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
    DmxRouter* dmx_router() { return dmx_router_; }

    void notify_status_async();

    static void ws_broadcast_work(void* arg);
    static void ws_broadcast_preview_work(void* arg);
    static esp_err_t serve_dashboard(httpd_req_t* request);

    FactoryRunner& runner_;
    ConfigStore* config_ = nullptr;
    LightingRuntime* signage_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    NodeSync* node_sync_ = nullptr;
    DmxRouter* dmx_router_ = nullptr;
    std::function<StartupDiagnostics()> diagnostics_provider_{};
    httpd_handle_t signage_server_ = nullptr;
    httpd_handle_t factory_server_ = nullptr;
    size_t max_open_sockets_ = 0;
    std::atomic<bool> preview_pending_{false};
};

} // namespace lg
