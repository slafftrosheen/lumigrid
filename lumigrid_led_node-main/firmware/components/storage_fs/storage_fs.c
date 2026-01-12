#include "esp_littlefs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include "storage_fs.h"

static const char* TAG = "storage_fs";
static const char* STORAGE_MOUNT = "/spiffs";
static const char* STORAGE_DATA = "/spiffs/storage";
static bool s_mounted = false;

esp_err_t storage_fs_init(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_MOUNT,        // keep path consistent with your UI server
        .partition_label = "storage",
        .format_if_mount_failed = true,  // first line of defense
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_OK) {
        size_t total=0, used=0;
        esp_littlefs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "LittleFS mounted: %u / %u bytes used", (unsigned)used, (unsigned)total);
        s_mounted = true;
        return ESP_OK;
    }

    // Belt & suspenders: try a one-shot explicit format + remount
    ESP_LOGW(TAG, "Mount failed (%s). Formatting partition and retrying...",
             esp_err_to_name(err));
    esp_err_t f = esp_littlefs_format(conf.partition_label);
    if (f != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(f));
        return err;
    }
    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount after format failed: %s", esp_err_to_name(err));
        s_mounted = false;
        return err;
    }
    ESP_LOGW(TAG, "LittleFS formatted and mounted clean.");
    s_mounted = true;
    return ESP_OK;
}

esp_err_t storage_fs_read(const char *path, uint8_t **out, size_t *len){
    *out = NULL; *len = 0;
    FILE *f = fopen(path, "rb");
    if(!f) return ESP_FAIL;
    if(fseek(f, 0, SEEK_END)!=0){ fclose(f); return ESP_FAIL; }
    long L = ftell(f); if(L < 0){ fclose(f); return ESP_FAIL; }
    rewind(f);
    uint8_t *buf = (uint8_t*)malloc((size_t)L);
    if(!buf){ fclose(f); return ESP_ERR_NO_MEM; }
    size_t r = fread(buf, 1, (size_t)L, f);
    fclose(f);
    if(r != (size_t)L){ free(buf); return ESP_FAIL; }
    *out = buf; *len = (size_t)L; return ESP_OK;
}

esp_err_t storage_fs_write(const char *path, const uint8_t *data, size_t len){
    FILE *f = fopen(path, "wb");
    if(!f) return ESP_FAIL;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w==len) ? ESP_OK : ESP_FAIL;
}

const char* storage_fs_mount_point(void){
    return STORAGE_MOUNT;
}

const char* storage_fs_data_root(void){
    return STORAGE_DATA;
}

static bool build_path(char* out, size_t len, const char* base, const char* relative){
    if (!out || len == 0 || !base){
        return false;
    }
    const char* rel = relative ? relative : "";
    while (*rel == '/'){
        rel++;
    }
    int written;
    if (*rel == '\0'){
        written = snprintf(out, len, "%s", base);
    } else {
        written = snprintf(out, len, "%s/%s", base, rel);
    }
    return (written > 0 && written < (int)len);
}

bool storage_fs_path(char* out, size_t len, const char* relative){
    return build_path(out, len, STORAGE_DATA, relative);
}

bool storage_fs_mount_path(char* out, size_t len, const char* relative){
    return build_path(out, len, STORAGE_MOUNT, relative);
}

bool storage_fs_is_mounted(void){
    return s_mounted;
}
