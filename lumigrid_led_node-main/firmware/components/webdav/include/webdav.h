#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void webdav_register(httpd_handle_t server);  // call after webserver start

#ifdef __cplusplus
}
#endif
