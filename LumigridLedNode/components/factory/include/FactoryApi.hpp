// components/factory/include/FactoryApi.hpp
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "FactoryRunner.hpp"
#include <atomic>

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
