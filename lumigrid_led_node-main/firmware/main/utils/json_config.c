#include "storage_fs.h"
#include "json_config.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static cJSON *s_config = NULL;
static SemaphoreHandle_t s_config_mutex = NULL;

static void merge_obj(cJSON *dst, const cJSON *src){
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, src){
        if (!it || !it->string){
            continue;
        }

        cJSON *here = cJSON_GetObjectItem(dst, it->string);
        if (!here){
            cJSON_AddItemToObject(dst, it->string, cJSON_Duplicate(it, true));
        } else if (cJSON_IsObject(here) && cJSON_IsObject(it)){
            merge_obj(here, it);
        } else {
            cJSON_ReplaceItemInObject(dst, it->string, cJSON_Duplicate(it, true));
        }
    }
}

esp_err_t config_init(void) {
    // Create mutex if not already created
    if (!s_config_mutex) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (!s_config_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    storage_fs_init();

    if (xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_config){
        cJSON_Delete(s_config);
        s_config = NULL;
    }

    uint8_t *json_buf = NULL;
    size_t len = 0;
    char cfg_path[128];
    if (!storage_fs_mount_path(cfg_path, sizeof(cfg_path), "config.json")) {
        // Create minimal in-memory config if filesystem unavailable
        s_config = cJSON_CreateObject();
        xSemaphoreGive(s_config_mutex);
        return s_config ? ESP_OK : ESP_ERR_NO_MEM;
    }
    esp_err_t err = storage_fs_read(cfg_path, &json_buf, &len);
    if (err != ESP_OK){
        free(json_buf);
        json_buf = NULL;
        len = 0;
        char def_path[128];
        if (storage_fs_mount_path(def_path, sizeof(def_path), "default_config.json") &&
            storage_fs_read(def_path, &json_buf, &len) == ESP_OK){
            storage_fs_write(cfg_path, json_buf, len);
        }
    }

    if (json_buf){
        s_config = cJSON_ParseWithLength((const char*)json_buf, len);
        free(json_buf);
    }

    if (!s_config){
        s_config = cJSON_CreateObject();
    }

    xSemaphoreGive(s_config_mutex);
    return s_config ? ESP_OK : ESP_ERR_NO_MEM;
}

const cJSON* config_root(void) {
    // Read-only access - no mutex needed for const pointer
    return s_config;
}

cJSON* config_mutable_root(void) {
    // Caller must hold the mutex when modifying returned pointer
    // Use config_lock()/config_unlock() around modifications
    return s_config;
}

esp_err_t config_save(void) {
    if (!s_config_mutex || xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    char* json_str = cJSON_PrintUnformatted(s_config);
    if (!json_str) {
        xSemaphoreGive(s_config_mutex);
        return ESP_ERR_NO_MEM;
    }
    char cfg_path[128];
    esp_err_t err = ESP_FAIL;
    if (storage_fs_mount_path(cfg_path, sizeof(cfg_path), "config.json")) {
        err = storage_fs_write(cfg_path, (const uint8_t*)json_str, strlen(json_str));
    }
    free(json_str);
    xSemaphoreGive(s_config_mutex);
    return err;
}

void config_merge(const cJSON* src) {
    if (!s_config_mutex || xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }
    merge_obj(s_config, src);
    xSemaphoreGive(s_config_mutex);
}

bool config_lock(void) {
    if (!s_config_mutex) {
        return false;
    }
    return xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void config_unlock(void) {
    if (s_config_mutex) {
        xSemaphoreGive(s_config_mutex);
    }
}

bool config_is_first_boot(void) {
    if (!s_config) {
        return true;
    }
    
    // Check if "setup_complete" flag exists and is true
    const cJSON* setup_flag = cJSON_GetObjectItem(s_config, "setup_complete");
    if (!setup_flag || !cJSON_IsBool(setup_flag)) {
        return true;
    }
    
    return !cJSON_IsTrue(setup_flag);
}

esp_err_t config_mark_setup_complete(void) {
    if (!config_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    
    cJSON* root = config_mutable_root();
    if (!root) {
        config_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    
    // Set setup_complete flag
    cJSON_DeleteItemFromObject(root, "setup_complete");
    cJSON_AddBoolToObject(root, "setup_complete", true);
    
    config_unlock();
    
    // Save to storage
    return config_save();
}
