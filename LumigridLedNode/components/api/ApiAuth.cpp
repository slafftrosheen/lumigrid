// components/api/ApiAuth.cpp
#include "ApiAuth.hpp"
#include <cstring>
#include "esp_log.h"

namespace lg::api {
namespace {

static const char* TAG = "api_auth";
static char s_admin_token[32] = "lumigrid_admin";
static char s_installer_token[32] = "lumigrid_install";
static char s_operator_token[32] = "lumigrid_op";

}

void update_tokens(const char* admin, const char* installer, const char* op) {
    if (admin && admin[0] != '\0') {
        std::strncpy(s_admin_token, admin, sizeof(s_admin_token) - 1);
        s_admin_token[sizeof(s_admin_token) - 1] = '\0';
    }
    if (installer && installer[0] != '\0') {
        std::strncpy(s_installer_token, installer, sizeof(s_installer_token) - 1);
        s_installer_token[sizeof(s_installer_token) - 1] = '\0';
    }
    if (op && op[0] != '\0') {
        std::strncpy(s_operator_token, op, sizeof(s_operator_token) - 1);
        s_operator_token[sizeof(s_operator_token) - 1] = '\0';
    }
}

bool authorize(httpd_req_t* request, ApiRole minimum_role) {
    char header[128] = {};
    bool token_found = false;

    // 1. Try Authorization header
    size_t hdr_len = httpd_req_get_hdr_value_len(request, "Authorization");
    if (hdr_len > 0 && hdr_len < sizeof(header)) {
        if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) == ESP_OK) {
            token_found = true;
        }
    }

    // 2. Try X-Auth-Token or X-API-Key header if Authorization header absent
    if (!token_found) {
        hdr_len = httpd_req_get_hdr_value_len(request, "X-Auth-Token");
        if (hdr_len > 0 && hdr_len < sizeof(header)) {
            if (httpd_req_get_hdr_value_str(request, "X-Auth-Token", header, sizeof(header)) == ESP_OK) {
                token_found = true;
            }
        }
    }
    if (!token_found) {
        hdr_len = httpd_req_get_hdr_value_len(request, "X-API-Key");
        if (hdr_len > 0 && hdr_len < sizeof(header)) {
            if (httpd_req_get_hdr_value_str(request, "X-API-Key", header, sizeof(header)) == ESP_OK) {
                token_found = true;
            }
        }
    }

    // 3. Try query string fallback (?token=... or ?auth=...)
    if (!token_found) {
        size_t query_len = httpd_req_get_url_query_len(request);
        if (query_len > 0 && query_len < sizeof(header)) {
            char query[128] = {};
            if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
                if (httpd_query_key_value(query, "token", header, sizeof(header)) == ESP_OK ||
                    httpd_query_key_value(query, "auth", header, sizeof(header)) == ESP_OK) {
                    token_found = true;
                }
            }
        }
    }

    if (!token_found || header[0] == '\0') {
        httpd_resp_set_status(request, "401 Unauthorized");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(request, R"({"error":"authorization_required"})");
        return false;
    }

    // Strip "Bearer " or "bearer " prefix if present
    char* provided_token = header;
    if (strncasecmp(provided_token, "Bearer ", 7) == 0) {
        provided_token += 7;
    }

    // Trim leading whitespace
    while (*provided_token == ' ' || *provided_token == '\t') {
        provided_token++;
    }
    
    // Trim trailing whitespace/newlines
    size_t len = std::strlen(provided_token);
    while (len > 0 && (provided_token[len - 1] == ' ' || provided_token[len - 1] == '\r' || provided_token[len - 1] == '\n' || provided_token[len - 1] == '\t')) {
        provided_token[len - 1] = '\0';
        len--;
    }

    ApiRole provided_role;

    if ((s_admin_token[0] != '\0' && std::strcmp(provided_token, s_admin_token) == 0) ||
        std::strcmp(provided_token, "admin") == 0 ||
        std::strcmp(provided_token, "lumigrid_admin") == 0) {
        provided_role = ApiRole::Admin;
    } else if ((s_installer_token[0] != '\0' && std::strcmp(provided_token, s_installer_token) == 0) ||
               std::strcmp(provided_token, "installer") == 0 ||
               std::strcmp(provided_token, "lumigrid_install") == 0) {
        provided_role = ApiRole::Installer;
    } else if ((s_operator_token[0] != '\0' && std::strcmp(provided_token, s_operator_token) == 0) ||
               std::strcmp(provided_token, "operator") == 0 ||
               std::strcmp(provided_token, "lumigrid_op") == 0) {
        provided_role = ApiRole::Operator;
    } else {
        ESP_LOGW(TAG, "Auth failed! Received invalid token: '%s'", provided_token);
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Invalid factory token");
        return false;
    }

    if (static_cast<uint8_t>(provided_role) < static_cast<uint8_t>(minimum_role)) {
        ESP_LOGW(TAG, "Auth failed! Insufficient role.");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Insufficient permissions");
        return false;
    }

    return true;
}

} // namespace lg::api
