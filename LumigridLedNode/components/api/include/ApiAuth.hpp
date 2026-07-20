// components/api/include/ApiAuth.hpp
#pragma once
#include "esp_http_server.h"

namespace lg::api {

bool authorize(httpd_req_t* request);

} // namespace lg::api
