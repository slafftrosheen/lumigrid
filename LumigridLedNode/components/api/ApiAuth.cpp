// components/api/ApiAuth.cpp
#include "ApiAuth.hpp"
#include <cstring>
#include "esp_log.h"

namespace lg::api {
namespace {

static const char* TAG = "api_auth";
static char s_admin_token[32] = {0};
static char s_installer_token[32] = {0};
static char s_operator_token[32] = {0};

}

void update_tokens(const char* admin, const char* installer, const char* op) {
    if (admin) {
        std::strncpy(s_admin_token, admin, sizeof(s_admin_token) - 1);
        s_admin_token[sizeof(s_admin_token) - 1] = '\0';
    }
    if (installer) {
        std::strncpy(s_installer_token, installer, sizeof(s_installer_token) - 1);
        s_installer_token[sizeof(s_installer_token) - 1] = '\0';
    }
    if (op) {
        std::strncpy(s_operator_token, op, sizeof(s_operator_token) - 1);
        s_operator_token[sizeof(s_operator_token) - 1] = '\0';
    }
}

bool authorize(httpd_req_t* request, ApiRole minimum_role) {
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

    constexpr char prefix[] = "Bearer ";
    if (std::strncmp(header, prefix, sizeof(prefix) - 1) != 0) {
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Invalid factory token");
        return false;
    }

    char* provided_token = header + sizeof(prefix) - 1;
    
    // Trim trailing whitespace/newlines
    size_t len = std::strlen(provided_token);
    while (len > 0 && (provided_token[len - 1] == ' ' || provided_token[len - 1] == '\r' || provided_token[len - 1] == '\n')) {
        provided_token[len - 1] = '\0';
        len--;
    }

    ApiRole provided_role;

    if (s_admin_token[0] != '\0' && std::strcmp(provided_token, s_admin_token) == 0) {
        provided_role = ApiRole::Admin;
    } else if (s_installer_token[0] != '\0' && std::strcmp(provided_token, s_installer_token) == 0) {
        provided_role = ApiRole::Installer;
    } else if (s_operator_token[0] != '\0' && std::strcmp(provided_token, s_operator_token) == 0) {
        provided_role = ApiRole::Operator;
    } else {
        ESP_LOGW(TAG, "Auth failed! Received invalid token.");
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Invalid factory token");
        return false;
    }

    if (static_cast<uint8_t>(provided_role) < static_cast<uint8_t>(minimum_role)) {
        ESP_LOGW(TAG, "Auth failed! Insufficient role.");
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Insufficient permissions");
        return false;
    }

    return true;
}

} // namespace lg::api
