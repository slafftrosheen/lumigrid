#include "task_pwm_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pca9685_driver.h"
#include <math.h>
#include <string.h>

static const char *TAG = "PWM_DRIVER";

typedef enum {
    PWM_MODE_STATIC,
    PWM_MODE_BREATH,
    PWM_MODE_CANDLE,
    PWM_MODE_WARMDIM
} pwm_mode_t;

typedef struct {
    uint8_t ch;
    pwm_mode_t mode;
    float target;
    float current;
    uint32_t t0;
    uint32_t duration_ms;
    bool log_curve;
    float breath_min;
    float breath_max;
    float breath_period_ms;
    float candle_base;
    float candle_flick;
    uint32_t seed;
} pwm_anim_t;

static pwm_anim_t s_anims[8] = {0};
static pwm_group_t s_groups[8];
static int s_groups_len = 0;
static float s_last_duty[8] = {0};

static inline void set_channel(int ch, float value){
    if (ch >= 0 && ch < 8){
        if (value < 0.f) value = 0.f;
        if (value > 1.f) value = 1.f;
        s_last_duty[ch] = value;
        pca9685_set_duty((uint8_t)ch, value);
    }
}

void pwm_set_mode_static(uint8_t ch, float duty) {
    if (ch >= 8) return;
    s_anims[ch].mode = PWM_MODE_STATIC;
    s_anims[ch].target = duty;
    set_channel(ch, duty);
}

void pwm_set_mode_breath(uint8_t ch, float min_val, float max_val, float period_ms) {
    if (ch >= 8) return;
    s_anims[ch].mode = PWM_MODE_BREATH;
    s_anims[ch].breath_min = min_val;
    s_anims[ch].breath_max = max_val;
    s_anims[ch].breath_period_ms = period_ms > 0 ? period_ms : 2000.f;
}

void pwm_set_mode_candle(uint8_t ch, float base, float flicker, uint32_t seed) {
    if (ch >= 8) return;
   s_anims[ch].mode = PWM_MODE_CANDLE;
   s_anims[ch].candle_base = base;
   s_anims[ch].candle_flick = flicker;
   s_anims[ch].seed = seed;
}

void pwm_set_mode_warmdim(uint8_t ch, float duty) {
    if (ch >= 8) return;
    s_anims[ch].mode = PWM_MODE_WARMDIM;
    s_anims[ch].target = duty;
    float clamped = duty;
    if (clamped < 0.f) clamped = 0.f;
    if (clamped > 1.f) clamped = 1.f;
    set_channel(ch, powf(clamped, 2.0f));
}

#include "json_config.h"
#include "cJSON.h"

static int json_get_int(const cJSON *o, const char *k, int def){
    const cJSON *x = cJSON_GetObjectItem(o, k);
    return (x && cJSON_IsNumber(x)) ? x->valueint : def;
}

static const char *json_get_str(const cJSON *o, const char *k, const char *def){
    const cJSON *x = cJSON_GetObjectItem(o, k);
    return (x && cJSON_IsString(x)) ? x->valuestring : def;
}

void pwm_groups_init_from_config(void){
    s_groups_len = 0;

    const cJSON *root = config_root();
    if (!root){
        return;
    }

    const cJSON *groups = cJSON_GetObjectItem(root, "pwm_groups");
    if (!groups || !cJSON_IsArray(groups)){
        return;
    }

    cJSON *it = NULL;
    cJSON_ArrayForEach(it, groups){
        if (!cJSON_IsObject(it) || s_groups_len >= (int)(sizeof(s_groups)/sizeof(s_groups[0]))){
            continue;
        }

        pwm_group_t *g = &s_groups[s_groups_len++];
        memset(g, 0, sizeof(*g));

        const char *name = json_get_str(it, "name", "group");
        strncpy(g->name, name, sizeof(g->name) - 1);
        g->name[sizeof(g->name) - 1] = '\0';

        const char *kind = json_get_str(it, "kind", "RGB");
        g->kind = (strcmp(kind, "RGBW") == 0) ? PWMG_RGBW : PWMG_RGB;

        const cJSON *map = cJSON_GetObjectItem(it, "map");
        if (map && cJSON_IsObject(map)){
            g->map_r = json_get_int(map, "r", -1);
            g->map_g = json_get_int(map, "g", -1);
            g->map_b = json_get_int(map, "b", -1);
            g->map_w = json_get_int(map, "w", -1);
        } else {
            g->map_r = g->map_g = g->map_b = g->map_w = -1;
        }

        if (g->map_r < 0 || g->map_r >= 8) g->map_r = -1;
        if (g->map_g < 0 || g->map_g >= 8) g->map_g = -1;
        if (g->map_b < 0 || g->map_b >= 8) g->map_b = -1;
        if (g->map_w < 0 || g->map_w >= 8) g->map_w = -1;
    }
}

void pwm_groups_replace(const pwm_group_t *groups, int count){
    if (!groups || count <= 0){
        s_groups_len = 0;
        return;
    }
    if (count > 8){
        count = 8;
    }
    for (int i = 0; i < count; ++i){
        s_groups[i] = groups[i];
        s_groups[i].name[sizeof(s_groups[i].name) - 1] = '\0';
        if (s_groups[i].map_r < 0 || s_groups[i].map_r >= 8) s_groups[i].map_r = -1;
        if (s_groups[i].map_g < 0 || s_groups[i].map_g >= 8) s_groups[i].map_g = -1;
        if (s_groups[i].map_b < 0 || s_groups[i].map_b >= 8) s_groups[i].map_b = -1;
        if (s_groups[i].map_w < 0 || s_groups[i].map_w >= 8) s_groups[i].map_w = -1;
    }
    s_groups_len = count;
}

int pwm_groups_count(void){
    return s_groups_len;
}

const pwm_group_t* pwm_groups_get(int index){
    if (index < 0 || index >= s_groups_len){
        return NULL;
    }
    return &s_groups[index];
}

static pwm_group_t* find_group(const char *name){
    if (!name){
        return NULL;
    }
    for (int i = 0; i < s_groups_len; ++i){
        if (strncmp(s_groups[i].name, name, sizeof(s_groups[i].name)) == 0){
            return &s_groups[i];
        }
    }
    return NULL;
}

void pwm_group_set_rgb(const char* name, float r, float g, float b){
    pwm_group_t *gptr = find_group(name);
    if (!gptr || gptr->kind != PWMG_RGB){
        return;
    }
    set_channel(gptr->map_r, r);
    set_channel(gptr->map_g, g);
    set_channel(gptr->map_b, b);
}

void pwm_group_set_rgbw(const char* name, float r, float g, float b, float w){
    pwm_group_t *gptr = find_group(name);
    if (!gptr){
        return;
    }
    if (gptr->kind == PWMG_RGB){
        set_channel(gptr->map_r, r);
        set_channel(gptr->map_g, g);
        set_channel(gptr->map_b, b);
    } else {
        set_channel(gptr->map_r, r);
        set_channel(gptr->map_g, g);
        set_channel(gptr->map_b, b);
        set_channel(gptr->map_w, w);
    }
}

static void pwm_update_channel(pwm_anim_t* anim, uint32_t t_ms) {
    float duty = 0.0f;
    
    switch (anim->mode) {
        case PWM_MODE_BREATH: {
            float period = anim->breath_period_ms > 10.f ? anim->breath_period_ms : 1000.f;
            float phase = fmodf((float)t_ms / period, 1.0f);
            float s = 0.5f - 0.5f * cosf(phase * 6.2831853f);
            duty = anim->breath_min + (anim->breath_max - anim->breath_min) * s;
            break;
        }

        case PWM_MODE_CANDLE: {
            uint32_t x = (t_ms * 1664525u + anim->seed * 1013904223u);
            float n = ((x >> 8) & 0xFFFF) / 65535.0f;
            duty = anim->candle_base + (n - 0.5f) * anim->candle_flick;
            if (duty < 0) duty = 0;
            if (duty > 1) duty = 1;
            break;
        }

        case PWM_MODE_WARMDIM: {
            float clamped = anim->target;
            if (clamped < 0.f) clamped = 0.f;
            if (clamped > 1.f) clamped = 1.f;
            duty = powf(clamped, 2.0f);
            break;
        }

        case PWM_MODE_STATIC:
        default:
            duty = anim->target;
            break;
    }
    
    set_channel(anim->ch, duty);
}

float pwm_max_configured_duty(void){
    float max = 0.f;
    for (int i = 0; i < 8; ++i){
        if (s_last_duty[i] > max){
            max = s_last_duty[i];
        }
    }
    return max;
}

static void pwm_driver_task(void *arg) {
    ESP_LOGI(TAG, "PWM driver task started");
    
    for (int i = 0; i < 8; i++) {
        s_anims[i].ch = i;
        s_anims[i].mode = PWM_MODE_STATIC;
        s_anims[i].target = 0.0f;
    }
    
    while (1) {
        uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000);
        
        for (int i = 0; i < 8; i++) {
            if (s_anims[i].mode != PWM_MODE_STATIC) {
                pwm_update_channel(&s_anims[i], t_ms);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void task_pwm_driver_start(void) {
    pwm_groups_init_from_config();
    xTaskCreate(pwm_driver_task, "pwm_drv", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "PWM driver task spawned");
}

void pwm_group_theater(const char* name, float level, uint32_t t_ms){
  const pwm_group_t* g=NULL; for(int i=0;i<s_groups_len;i++) if(strcmp(s_groups[i].name,name)==0){g=&s_groups[i];break;}
  if(!g) return;
  int phase=(t_ms/120)%3; float on=level, off=0.02f;
  set_channel(g->map_r, (phase==0)?on:off); set_channel(g->map_g,(phase==1)?on:off); set_channel(g->map_b,(phase==2)?on:off);
  if(g->kind==PWMG_RGBW) set_channel(g->map_w, off);
}
