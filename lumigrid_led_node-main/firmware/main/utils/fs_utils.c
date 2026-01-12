// SPDX-License-Identifier: MIT
#include "fs_utils.h"
#include "storage_fs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <sys/unistd.h>
#include <errno.h>

static const char *TAG = "fs_utils";

static void ensure_dir(const char* path){
    if (!path || !path[0]){
        return;
    }
    struct stat st;
    if (stat(path, &st) != 0){
        if (mkdir(path, 0755) != 0){
            ESP_LOGE(TAG, "mkdir(%s) failed: %d", path, errno);
        }
    }
}

esp_err_t fsu_ensure_dirs(void){
    esp_err_t err = storage_fs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Filesystem init failed: %s. System will run with in-memory defaults.", 
                 esp_err_to_name(err));
        return err;
    }
    
    if (!storage_fs_is_mounted()) {
        ESP_LOGW(TAG, "Filesystem not mounted. System will run with in-memory defaults.");
        return ESP_FAIL;
    }
    
    ensure_dir(storage_fs_data_root());
    char path[128];
    if (storage_fs_mount_path(path, sizeof(path), "presets")){
        ensure_dir(path);
    }
    if (storage_fs_path(path, sizeof(path), "palettes")){
        ensure_dir(path);
    }
    return ESP_OK;
}
