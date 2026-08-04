// components/api/ApiAuth.cpp
#include "ApiAuth.hpp"
#include <cstring>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

namespace lg::api {
namespace {

static const char* TAG = "api_auth";
static char s_api_token[64] = {0};
static bool s_token_loaded = false;

bool load_token() {
    if (s_token_loaded) return true;

    nvs_handle_t handle = 0;
    if (nvs_open("lumigrid", NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for API token");
        return false;
    }

    size_t len = sizeof(s_api_token);
    if (nvs_get_str(handle, "api_token", s_api_token, &len) != ESP_OK) {
        ESP_LOGE(TAG, "No api_token found in NVS. Device is locked down.");
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    s_token_loaded = true;
    return true;
}

}

bool authorize(httpd_req_t* request) {
    char header[128] = {};
    const size_t required = httpd_req_get_hdr_value_len(
        request,
        "Authorization"
    );

    if (required == 0 || required >= sizeof(header)) {
        httpd_resp_set_status(request, "401 Unauthorized");
        httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
        httpd_resp_sendstr(request, R"({"error":"authorization_required"})");
        return false;
    }

    if (httpd_req_get_hdr_value_str(
            request,
            "Authorization",
            header,
            sizeof(header)
        ) != ESP_OK) {
        httpd_resp_send_err(
            request,
            HTTPD_401_UNAUTHORIZED,
            "Invalid authorization header"
        );
        return false;
    }

    if (!load_token()) {
        httpd_resp_send_err(
            request,
            HTTPD_403_FORBIDDEN,
            "Server not provisioned"
        );
        return false;
    }

    constexpr char prefix[] = "Bearer ";
    const bool valid =
        std::strncmp(header, prefix, sizeof(prefix) - 1) == 0 &&
        std::strcmp(header + sizeof(prefix) - 1, s_api_token) == 0;

    if (!valid) {
        httpd_resp_send_err(
            request,
            HTTPD_403_FORBIDDEN,
            "Invalid factory token"
        );
    }

    return valid;
}

} // namespace lg::api
