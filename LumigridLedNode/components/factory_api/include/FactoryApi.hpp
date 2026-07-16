#pragma once

#include "FactoryRunner.hpp"
#include <esp_http_server.h>

namespace lg {

class FactoryApi {
public:
    explicit FactoryApi(FactoryRunner& runner);
    ~FactoryApi();

    esp_err_t start();
    void stop();

private:
    FactoryRunner& runner_;
    httpd_handle_t server_ = nullptr;

    static esp_err_t post_effect_handler(httpd_req_t *req);
    static esp_err_t post_stop_handler(httpd_req_t *req);
    static esp_err_t get_status_handler(httpd_req_t *req);
    static esp_err_t post_pwm_handler(httpd_req_t *req);
    static esp_err_t post_lane_handler(httpd_req_t *req);
    static esp_err_t post_burn_in_handler(httpd_req_t *req);
    static esp_err_t post_oe_handler(httpd_req_t *req);
};

} // namespace lg
