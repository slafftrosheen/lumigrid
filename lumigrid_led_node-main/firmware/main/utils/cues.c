// SPDX-License-Identifier: MIT
#include "cues.h"
#include "aled_state.h"
#include "storage_fs.h"
#include "cJSON.h"
#include "effects.h"
#include "task_pwm_driver.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

typedef struct { uint64_t at_ms; uint32_t fade_ms; char name[48]; } cue_t;
static QueueHandle_t s_q;
static volatile uint32_t s_fade_remaining_ms;
static char s_active_preset[48];

static esp_err_t load_preset_json(const char *name, cJSON **out){
    char path[128];
    if (!storage_fs_mount_path(path, sizeof(path), "presets")) {
        return ESP_FAIL;
    }
    size_t base_len = strlen(path);
    int written = snprintf(path + base_len, sizeof(path) - base_len, "/%s.json", name);
    if (written <= 0 || (size_t)written >= sizeof(path) - base_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buf=NULL; size_t L=0;
    if (storage_fs_read(path,&buf,&L)!=ESP_OK) return ESP_FAIL;
    cJSON *root=cJSON_ParseWithLength((const char*)buf,L); free(buf);
    if (!root) return ESP_FAIL;
    *out=root;
    return ESP_OK;
}

static void apply_preset(const cJSON *preset, uint32_t fade_ms){
    // ALED
    const cJSON *aled = cJSON_GetObjectItem(preset,"aled");
    if (aled && cJSON_IsArray(aled)){
        int i=0; const cJSON *it=NULL;
        cJSON_ArrayForEach(it,aled){
            if (!cJSON_IsObject(it) || i>=aled_count()) { ++i; continue; }
            const cJSON *col=cJSON_GetObjectItem(it,"color");
            if (col && cJSON_IsArray(col) && cJSON_GetArraySize(col) >= 3){
                int n = aled_ch[i].n_pixels;
                for(int p=0;p<n;p++){
                    px_rgba_t c={0,0,0,0};
                    // expect [r,g,b(,w)?]
                    const cJSON *r_item = cJSON_GetArrayItem(col,0);
                    const cJSON *g_item = cJSON_GetArrayItem(col,1);
                    const cJSON *b_item = cJSON_GetArrayItem(col,2);
                    if (r_item && cJSON_IsNumber(r_item)) c.r = (uint8_t)r_item->valueint;
                    if (g_item && cJSON_IsNumber(g_item)) c.g = (uint8_t)g_item->valueint;
                    if (b_item && cJSON_IsNumber(b_item)) c.b = (uint8_t)b_item->valueint;
                    if (cJSON_GetArraySize(col)>3) {
                        const cJSON *w_item = cJSON_GetArrayItem(col,3);
                        if (w_item && cJSON_IsNumber(w_item)) c.w=(uint8_t)w_item->valueint;
                    }
                    aled_ch[i].framebuf[p]=c;
                }
            }
            ++i;
        }
    }
    // PWM
    const cJSON *pg = cJSON_GetObjectItem(preset,"pwm_groups");
    if (pg && cJSON_IsArray(pg)){
        const cJSON *it=NULL;
        cJSON_ArrayForEach(it,pg){
            const cJSON *name_item = cJSON_GetObjectItem(it,"name");
            const cJSON *lev = cJSON_GetObjectItem(it,"level");
            if (!name_item || !cJSON_IsString(name_item) || !lev || !cJSON_IsObject(lev)) continue;
            const char *name = name_item->valuestring;
            
            const cJSON *r_item = cJSON_GetObjectItem(lev,"r");
            const cJSON *g_item = cJSON_GetObjectItem(lev,"g");
            const cJSON *b_item = cJSON_GetObjectItem(lev,"b");
            if (!r_item || !cJSON_IsNumber(r_item) || 
                !g_item || !cJSON_IsNumber(g_item) || 
                !b_item || !cJSON_IsNumber(b_item)) continue;
            
            float r = (float)r_item->valuedouble;
            float g = (float)g_item->valuedouble;
            float b = (float)b_item->valuedouble;
            const cJSON *wJ = cJSON_GetObjectItem(lev,"w");
            if (wJ && cJSON_IsNumber(wJ)) pwm_group_set_rgbw(name,r,g,b,(float)wJ->valuedouble);
            else    pwm_group_set_rgb (name,r,g,b);
        }
    }
    // naive fade timer published to SSE (optional)
    s_fade_remaining_ms = fade_ms;
}

static void cue_task(void *arg){
    for(;;){
        cue_t c;
        if (xQueueReceive(s_q,&c,portMAX_DELAY)!=pdTRUE) continue;
        uint64_t now = (uint64_t)esp_timer_get_time()/1000ULL;
        if (c.at_ms > now){
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(c.at_ms - now)));
        }
        cJSON *preset=NULL;
        if (load_preset_json(c.name,&preset)==ESP_OK){
            strncpy(s_active_preset, c.name, sizeof(s_active_preset)-1);
            apply_preset(preset, c.fade_ms);
            cJSON_Delete(preset);
        }
        // simple fade countdown
        while (s_fade_remaining_ms>0){
            vTaskDelay(pdMS_TO_TICKS(50));
            if (s_fade_remaining_ms >= 50) s_fade_remaining_ms -= 50;
            else s_fade_remaining_ms = 0;
        }
    }
}

void cues_init(void){
    if (!s_q){
        s_q = xQueueCreate(8,sizeof(cue_t));
        xTaskCreate(cue_task,"cue_task",4096,NULL,5,NULL);
    }
}

void cues_schedule(const char *preset, uint64_t at_ms, uint32_t fade_ms){
    cue_t c={.at_ms=at_ms,.fade_ms=fade_ms};
    strncpy(c.name,preset,sizeof(c.name)-1);
    xQueueSend(s_q,&c,0);
}

void cues_active(char *out_name, size_t out_len, uint32_t *fade_remain_ms){
    if (out_name && out_len){ strncpy(out_name, s_active_preset, out_len-1); out_name[out_len-1]=0; }
    if (fade_remain_ms) *fade_remain_ms = s_fade_remaining_ms;
}
