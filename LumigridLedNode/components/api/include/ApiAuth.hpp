// components/api/include/ApiAuth.hpp
#pragma once
#include "esp_http_server.h"
#include "AuthTypes.hpp"
#include "AuthManager.hpp"

namespace lg::api {

using ApiRole = lg::auth::AuthRole;
using AuthMethod = lg::auth::AuthMethod;

struct AuthPrincipal {
    bool authenticated = false;
    bool factory_setup = false;
    ApiRole role = ApiRole::Operator;
    AuthMethod method = AuthMethod::None;
};

void set_auth_manager(lg::auth::AuthManager* manager);
lg::auth::AuthManager* get_auth_manager();

AuthPrincipal authenticate(httpd_req_t* request);
bool authorize(httpd_req_t* request, ApiRole minimum_role = ApiRole::Admin);

inline bool role_satisfies(ApiRole actual, ApiRole minimum) {
    return static_cast<uint8_t>(actual) >= static_cast<uint8_t>(minimum);
}

} // namespace lg::api
