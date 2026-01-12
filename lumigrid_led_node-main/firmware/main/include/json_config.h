#pragma once
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>

esp_err_t config_init(void);
const cJSON* config_root(void);
cJSON* config_mutable_root(void);
esp_err_t config_save(void);
void config_merge(const cJSON* src);

// Lock/unlock functions for thread-safe config modifications
// Use these when calling config_mutable_root() and modifying the config
bool config_lock(void);
void config_unlock(void);

// Check if this is a first boot (no valid config exists)
bool config_is_first_boot(void);

// Mark that initial setup has been completed
esp_err_t config_mark_setup_complete(void);
