#pragma once
#include "esp_err.h"
esp_err_t cfg_apply_all(void);   // re-read /spiffs/config.json and apply to runtime
