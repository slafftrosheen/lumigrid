#include "channel_groups.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = "CHAN_GROUP";

static channel_group_t* s_groups = NULL;
static int s_num_groups = 0;
static int s_capacity = 0;

void channel_groups_init(void) {
    s_groups = NULL;
    s_num_groups = 0;
    s_capacity = 0;
}

esp_err_t channel_groups_add(const char* name, const uint8_t* channels, int num_channels) {
    if (s_num_groups == s_capacity) {
        int new_capacity = s_capacity == 0 ? 4 : s_capacity * 2;
        channel_group_t* new_groups = realloc(s_groups, new_capacity * sizeof(channel_group_t));
        if (!new_groups) {
            ESP_LOGE(TAG, "Failed to allocate memory for channel groups");
            return ESP_ERR_NO_MEM;
        }
        s_groups = new_groups;
        s_capacity = new_capacity;
    }

    channel_group_t* group = &s_groups[s_num_groups];
    strncpy(group->name, name, sizeof(group->name) - 1);
    group->name[sizeof(group->name) - 1] = '\0';
    memcpy(group->channels, channels, num_channels * sizeof(uint8_t));
    group->num_channels = num_channels;

    s_num_groups++;
    return ESP_OK;
}

esp_err_t channel_groups_remove(const char* name) {
    for (int i = 0; i < s_num_groups; i++) {
        if (strcmp(s_groups[i].name, name) == 0) {
            for (int j = i; j < s_num_groups - 1; j++) {
                s_groups[j] = s_groups[j + 1];
            }
            s_num_groups--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

const channel_group_t* channel_groups_get(const char* name) {
    for (int i = 0; i < s_num_groups; i++) {
        if (strcmp(s_groups[i].name, name) == 0) {
            return &s_groups[i];
        }
    }
    return NULL;
}
