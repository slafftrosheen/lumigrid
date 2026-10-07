// components/api/ApiAuth.cpp
#include "ApiAuth.hpp"
#include <cstring>
#include "esp_log.h"

namespace lg::api {
namespace {

static const char* TAG = "api_auth";
static lg::auth::AuthManager* s_auth_manager = nullptr;

} // namespace

void set_auth_manager(lg::auth::AuthManager* manager) {
    s_auth_manager = manager;
}

lg::auth::AuthManager* get_auth_manager() {
    return s_auth_manager;
}

AuthPrincipal authenticate(httpd_req_t* request) {
    AuthPrincipal principal{};
    if (!s_auth_manager) {
        return principal;
    }

    char header[128] = {};

    // 1. Try Authorization header: Bearer <session-token>
    size_t hdr_len = httpd_req_get_hdr_value_len(request, "Authorization");
    if (hdr_len > 0 && hdr_len < sizeof(header)) {
        if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) == ESP_OK) {
            char* token = header;
            if (strncasecmp(token, "Bearer ", 7) == 0) {
                token += 7;
            }
            while (*token == ' ' || *token == '\t') {
                token++;
            }
            size_t len = std::strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' || token[len - 1] == '\n' || token[len - 1] == '\t')) {
                token[len - 1] = '\0';
                len--;
            }

            auto res = s_auth_manager->validate_session(token);
            if (res.valid) {
                principal.authenticated = true;
                principal.factory_setup = res.factory_setup;
                principal.role = res.role;
                principal.method = AuthMethod::Session;
                return principal;
            }
        }
    }

    // 2. Try X-API-Key header (explicit automation / long-lived credential)
    hdr_len = httpd_req_get_hdr_value_len(request, "X-API-Key");
    if (hdr_len > 0 && hdr_len < sizeof(header)) {
        if (httpd_req_get_hdr_value_str(request, "X-API-Key", header, sizeof(header)) == ESP_OK) {
            char* token = header;
            while (*token == ' ' || *token == '\t') {
                token++;
            }
            size_t len = std::strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' || token[len - 1] == '\n' || token[len - 1] == '\t')) {
                token[len - 1] = '\0';
                len--;
            }

            lg::auth::AuthRole role = lg::auth::AuthRole::Operator;
            bool verified = s_auth_manager->verify_api_key(token, &role);
            if (verified) {
                principal.authenticated = true;
                principal.role = role;
                principal.method = AuthMethod::ApiKey;
                return principal;
            }
        }
    }

    // Explicit rejection: persistent credentials are NOT accepted via query strings or X-Auth-Token.
    return principal;
}

bool authorize(httpd_req_t* request, ApiRole minimum_role) {
    if (!s_auth_manager) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_sendstr(request, R"({"error":"auth_unavailable"})");
        return false;
    }

    // Check if session token was provided but expired
    char auth_hdr[128] = {};
    size_t hdr_len = httpd_req_get_hdr_value_len(request, "Authorization");
    bool session_expired = false;
    if (hdr_len > 0 && hdr_len < sizeof(auth_hdr)) {
        if (httpd_req_get_hdr_value_str(request, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
            char* token = auth_hdr;
            if (strncasecmp(token, "Bearer ", 7) == 0) token += 7;
            while (*token == ' ' || *token == '\t') token++;
            size_t len = std::strlen(token);
            while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\r' || token[len - 1] == '\n' || token[len - 1] == '\t')) {
                token[len - 1] = '\0';
                len--;
            }
            auto res = s_auth_manager->inspect_session(token);
            if (res.expired) {
                session_expired = true;
            }
        }
    }

    AuthPrincipal principal = authenticate(request);

    // All normal APIs (including network, OTA, restore and WS tickets) require provisioning.
    if (!s_auth_manager->is_provisioned() &&
        std::strcmp(request->uri, "/api/v1/auth/session") != 0 &&
        std::strcmp(request->uri, "/api/v1/auth/bootstrap") != 0) {
        httpd_resp_set_status(request, "403 Forbidden");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(request, R"({"error":"admin_setup_required"})");
        return false;
    }

    if (!principal.authenticated) {
        httpd_resp_set_status(request, "401 Unauthorized");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
        if (session_expired) {
            httpd_resp_sendstr(request, R"({"error":"session_expired"})");
        } else {
            httpd_resp_sendstr(request, R"({"error":"authorization_required"})");
        }
        return false;
    }

    if (static_cast<uint8_t>(principal.role) < static_cast<uint8_t>(minimum_role)) {
        ESP_LOGW(TAG, "Auth failed: Insufficient role (%d < %d)",
                 static_cast<int>(principal.role), static_cast<int>(minimum_role));
        httpd_resp_set_status(request, "403 Forbidden");
        httpd_resp_set_type(request, "application/json");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", "Authorization, Content-Type, X-API-Key");
        httpd_resp_sendstr(request, R"({"error":"insufficient_permissions"})");
        return false;
    }

    return true;
}

} // namespace lg::api
