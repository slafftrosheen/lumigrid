#pragma once
#include <stdint.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void capport_start(uint32_t ap_ip);  // ap_ip in network byte order (e.g., inet_addr("192.168.4.1"))
void capport_stop(void);
void capport_register_endpoints(httpd_handle_t server);  // Register HTTP endpoints

#ifdef __cplusplus
}
#endif
