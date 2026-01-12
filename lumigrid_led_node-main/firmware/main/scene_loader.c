#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "effect_control.h"
#include "scene_loader.h"

// Forward declaration to work around build order issue
extern esp_err_t fx_set_channel_brightness(int ch, float brightness);

static const char *TAG = "SCENES";

static uint32_t parse_hex_rgb(const char *s){
  if (!s || strlen(s) != 7 || s[0] != '#'){
    return 0;
  }
  return (uint32_t)strtol(s + 1, NULL, 16);
}

static char *load_all(const char *path){
  FILE *fp = fopen(path, "rb"); if (!fp) return NULL;
  fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  char *buf = malloc(sz + 1); if (!buf){ fclose(fp); return NULL; }
  fread(buf, 1, sz, fp); buf[sz] = 0; fclose(fp); return buf;
}

esp_err_t scene_apply_from_file(const char *path){
  char *buf = load_all(path);
  if (!buf){ ESP_LOGW(TAG, "open %s failed", path); return ESP_FAIL; }
  cJSON *root = cJSON_Parse(buf); free(buf);
  if (!root){ ESP_LOGE(TAG, "scene json parse error"); return ESP_FAIL; }
  cJSON *chs = cJSON_GetObjectItem(root, "channels");
  if (!cJSON_IsArray(chs)){ cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
  cJSON *it; cJSON_ArrayForEach(it, chs){
    cJSON *jc = cJSON_GetObjectItem(it, "ch");
    cJSON *jfx = cJSON_GetObjectItem(it, "fx");
    if (!jc || !jfx || !cJSON_IsNumber(jc) || !cJSON_IsString(jfx)) continue;
    int ch = jc->valueint;
    const char *fx = jfx->valuestring;
    effect_params_t p = { .speed=1.0f,.intensity=0.5f,.color=0xFFFFFF,.palette_id=0,.brightness=1.0f };
    cJSON *jp = cJSON_GetObjectItem(it, "params");
    if (jp && cJSON_IsObject(jp)){
      cJSON *v;
      if ((v=cJSON_GetObjectItem(jp,"speed")) && cJSON_IsNumber(v))     p.speed=(float)v->valuedouble;
      if ((v=cJSON_GetObjectItem(jp,"intensity")) && cJSON_IsNumber(v)) p.intensity=(float)v->valuedouble;
      if ((v=cJSON_GetObjectItem(jp,"color")) && cJSON_IsString(v))     p.color=parse_hex_rgb(v->valuestring);
      if ((v=cJSON_GetObjectItem(jp,"palette_id")) && cJSON_IsNumber(v)) p.palette_id=(uint8_t)v->valueint;
    }
    cJSON *jb = cJSON_GetObjectItem(it, "brightness");
    esp_err_t fx_err = fx_set_channel_by_name(ch, fx, &p);
    if (fx_err != ESP_OK){
      ESP_LOGW(TAG, "scene fx '%s' not found for channel %d", fx, ch);
      continue;
    }
    if (jb && cJSON_IsNumber(jb)) fx_set_channel_brightness(ch, (float)jb->valuedouble);
  }
  cJSON_Delete(root);
  ESP_LOGI(TAG, "scene applied from %s", path);
  return ESP_OK;
}
