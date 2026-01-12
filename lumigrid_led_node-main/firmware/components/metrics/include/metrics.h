#pragma once

#include "esp_http_server.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void metrics_register(httpd_handle_t server);
void metrics_note_frame_us(uint32_t us);  // call from effect render if you have it
void metrics_pulse_led_drop(int ch);      // call if aled dropped a frame

#ifdef __cplusplus
}
#endif
