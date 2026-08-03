// components/factory/include/FactoryApi.hpp
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "FactoryRunner.hpp"

// Forward declarations for signage components
namespace lg {
class ConfigStore;
class SignageEngine;
}

namespace lg {

class FactoryApi {
public:
    explicit FactoryApi(FactoryRunner& runner);

    /// Start HTTP server. Pass signage pointers (nullable) for signage REST endpoints.
    esp_err_t start(uint16_t port = 80,
                    ConfigStore* config = nullptr,
                    SignageEngine* signage = nullptr);
    void stop();

    FactoryRunner& runner() { return runner_; }
    ConfigStore* config() { return config_; }
    SignageEngine* signage() { return signage_; }

    void notify_status_async();

private:
    static void ws_broadcast_work(void* arg);
    static esp_err_t serve_dashboard(httpd_req_t* request);

    FactoryRunner& runner_;
    ConfigStore* config_ = nullptr;
    SignageEngine* signage_ = nullptr;
    httpd_handle_t server_ = nullptr;
    size_t max_open_sockets_ = 0;
};

} // namespace lg
