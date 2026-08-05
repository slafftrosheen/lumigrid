// components/api/include/ApiAuth.hpp
#pragma once
#include "esp_http_server.h"

namespace lg::api {

enum class ApiRole : uint8_t {
    Operator = 0,
    Installer = 1,
    Admin = 2
};

// Validates Bearer token from Authorization header against NVS storage
bool authorize(httpd_req_t* request, ApiRole minimum_role = ApiRole::Admin);

// Updates the cached tokens in memory after an NVS write
void update_tokens(const char* admin, const char* installer, const char* op);

} // namespace lg::api
