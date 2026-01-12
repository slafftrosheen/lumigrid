#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t storage_fs_init(void);

esp_err_t storage_fs_read(const char *path, uint8_t **out, size_t *len);
esp_err_t storage_fs_write(const char *path, const uint8_t *data, size_t len);

const char* storage_fs_mount_point(void);
const char* storage_fs_data_root(void);
bool storage_fs_path(char* out, size_t len, const char* relative);
bool storage_fs_mount_path(char* out, size_t len, const char* relative);

// Check if filesystem is mounted and available
bool storage_fs_is_mounted(void);
