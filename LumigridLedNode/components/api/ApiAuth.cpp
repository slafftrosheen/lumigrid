// components/api/ApiAuth.cpp
#include "ApiAuth.hpp"
#include <cstring>

namespace lg::api {
namespace {

// Replace before any network exposure.
// Prefer a 32-byte random token, provisioned into encrypted NVS for production.
constexpr char kFactoryToken[] = "CHANGE-ME-LUMIGRID-FACTORY-TOKEN";

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

    constexpr char prefix[] = "Bearer ";
    const bool valid =
        std::strncmp(header, prefix, sizeof(prefix) - 1) == 0 &&
        std::strcmp(header + sizeof(prefix) - 1, kFactoryToken) == 0;

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
