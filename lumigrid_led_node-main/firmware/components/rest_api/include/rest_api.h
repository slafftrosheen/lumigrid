#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Public start/stop for REST server
httpd_handle_t rest_api_get_handle(void);  // Get server handle (for captive portal, etc.)
esp_err_t      rest_api_start(void);
void           rest_api_stop(void);

char          *rest_api_status_json_alloc(void);
char          *rest_api_status_event_alloc(void);
bool           rest_api_extract_preset_name(const char *uri, char *out, size_t out_len);
esp_err_t      rest_api_delete_preset(const char *name);

// Preset validation (exposed for testing)
bool           rest_api_validate_preset_json(cJSON *root, char *error_buf, size_t error_buf_len);

// Register API modules
void           rest_api_register_sequencer(httpd_handle_t server);
void           rest_api_register_pwm(httpd_handle_t server);
void           rest_api_setup_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
