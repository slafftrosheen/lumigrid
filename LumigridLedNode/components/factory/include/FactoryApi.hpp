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
}

namespace lg {

class FactoryApi {
public:
    explicit FactoryApi(FactoryRunner& runner);

    /// Start HTTP servers. Separate signage and factory APIs by port.
    esp_err_t start(uint16_t signage_port = 80,
                    uint16_t factory_port = 8080,
                    ConfigStore* config = nullptr,
                    LightingRuntime* signage = nullptr,
                    Scheduler* scheduler = nullptr,
                    NodeSync* node_sync = nullptr);
    void stop();

    FactoryRunner& runner() { return runner_; }
    ConfigStore* config() { return config_; }
    LightingRuntime* signage() { return signage_; }
    Scheduler* scheduler() { return scheduler_; }
    NodeSync* node_sync() { return node_sync_; }

    void notify_status_async();

    static void ws_broadcast_work(void* arg);
    static void ws_broadcast_preview_work(void* arg);
    static esp_err_t serve_dashboard(httpd_req_t* request);

    FactoryRunner& runner_;
    ConfigStore* config_ = nullptr;
    LightingRuntime* signage_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    NodeSync* node_sync_ = nullptr;
    httpd_handle_t signage_server_ = nullptr;
    httpd_handle_t factory_server_ = nullptr;
    size_t max_open_sockets_ = 0;
    std::atomic<bool> preview_pending_{false};
};

} // namespace lg
