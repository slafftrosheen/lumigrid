// components/factory/include/FactoryApi.hpp
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"
#include "FactoryRunner.hpp"

namespace lg {

class FactoryApi {
public:
    explicit FactoryApi(FactoryRunner& runner);

    esp_err_t start(uint16_t port = 80);
    void stop();

    FactoryRunner& runner() { return runner_; }

    void notify_status_async();

private:
    static void ws_broadcast_work(void* arg);
    static esp_err_t serve_dashboard(httpd_req_t* request);

    FactoryRunner& runner_;
    httpd_handle_t server_ = nullptr;
    size_t max_open_sockets_ = 0;
};

} // namespace lg
