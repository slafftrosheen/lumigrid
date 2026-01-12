#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ota_http_url(const char* url);
esp_err_t ota_begin_upload(void);
esp_err_t ota_write_chunk(const uint8_t* data, int len);
esp_err_t ota_end_and_commit(void);
void ota_abort(void);
esp_err_t ota_mark_valid(void);
esp_err_t ota_check_rollback(void);

#ifdef __cplusplus
}
#endif
