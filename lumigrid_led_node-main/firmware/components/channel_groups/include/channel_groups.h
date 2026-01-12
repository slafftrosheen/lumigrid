#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    uint8_t channels[16];
    int num_channels;
} channel_group_t;

void channel_groups_init(void);
esp_err_t channel_groups_add(const char* name, const uint8_t* channels, int num_channels);
esp_err_t channel_groups_remove(const char* name);
const channel_group_t* channel_groups_get(const char* name);

#ifdef __cplusplus
}
#endif
