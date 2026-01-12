// SPDX-License-Identifier: MIT
#include "config_apply.h"
#include "aled_state.h"
#include "storage_fs.h"
#include "cJSON.h"
#include "effects.h"
#include "task_pwm_driver.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG="cfg_apply";

static int JINT(const cJSON* o,const char*k,int d){ const cJSON* x=cJSON_GetObjectItem(o,k); return x&&cJSON_IsNumber(x)?x->valueint:d; }
static const char* JSTR(const cJSON* o,const char*k){ const cJSON* x=cJSON_GetObjectItem(o,k); return x&&cJSON_IsString(x)?x->valuestring:NULL; }

static led_type_t parse_type(const char *s){
    if (!s) return LED_WS2812B;
    if (strcmp(s,"SK6812_RGBW")==0) return LED_SK6812_RGBW;
    return LED_WS2812B;
}
static color_order_t parse_order(const char *s){
    if (!s) return ORDER_GRB;
    if (!strcmp(s,"RGB")) return ORDER_RGB;
    if (!strcmp(s,"RGBW")) return ORDER_RGBW;
    if (!strcmp(s,"GRBW")) return ORDER_GRBW;
    return ORDER_GRB;
}

esp_err_t cfg_apply_all(void){
    uint8_t *buf=NULL; size_t L=0;
    char cfg_path[128];
    if (!storage_fs_mount_path(cfg_path, sizeof(cfg_path), "config.json")) {
        return ESP_OK;
    }
    if (storage_fs_read(cfg_path,&buf,&L)!=ESP_OK) return ESP_OK; // nothing to apply
    cJSON *root=cJSON_ParseWithLength((const char*)buf,L); free(buf);
    if (!root) return ESP_OK;

    // --- ALED channels ---
    const cJSON *aled = cJSON_GetObjectItem(root,"aled");
    if (aled && cJSON_IsArray(aled)){
        int n = aled_count();
        int i=0; const cJSON *it=NULL;
        cJSON_ArrayForEach(it, aled){
            if (!cJSON_IsObject(it) || i>=n) { ++i; continue; }
            aled_ch[i].n_pixels = JINT(it,"pixels", aled_ch[i].n_pixels);
            aled_ch[i].type  = parse_type(JSTR(it,"type"));
            aled_ch[i].order = parse_order(JSTR(it,"order"));
            ++i;
        }
    }

    // --- PWM groups ---
    pwm_groups_init_from_config();

    cJSON_Delete(root);
    ESP_LOGI(TAG,"config applied to runtime");
    return ESP_OK;
}
