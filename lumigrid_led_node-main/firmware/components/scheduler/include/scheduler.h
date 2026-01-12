#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void scheduler_init(void);
esp_err_t scheduler_reload(void);      // read storage_fs_path("schedule.json")
esp_err_t scheduler_save_json(const char* json, int len);
const char* scheduler_get_json(void);  // cached
void scheduler_trigger(const char* trigger_name);

#ifdef __cplusplus
}
#endif
