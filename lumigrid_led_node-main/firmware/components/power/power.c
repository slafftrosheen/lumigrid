#include "power.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#include "storage_fs.h"

static const char* TAG = "POWER";
static power_conf_t s_cfg = {
  .max_ma_total = 4000,
  .max_ma_ch = {1500, 1500, 1500, 1500}
};
static int s_used_total = 0;

void power_init(void) {
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "power.json")) {
    ESP_LOGW(TAG, "power path build failed");
    return;
  }
  FILE* f = fopen(path, "r");
  if (!f) {
    ESP_LOGW(TAG, "%s not found, using defaults", path);
    return;
  }
  char buf[256];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  cJSON* root = cJSON_Parse(buf);
  if (!root) {
    ESP_LOGW(TAG, "invalid power.json");
    return;
  }
  cJSON* t = cJSON_GetObjectItem(root, "max_ma_total");
  if (cJSON_IsNumber(t)) {
    s_cfg.max_ma_total = t->valueint;
  }
  cJSON* arr = cJSON_GetObjectItem(root, "max_ma_ch");
  if (cJSON_IsArray(arr)) {
    int count = cJSON_GetArraySize(arr);
    for (int i = 0; i < count && i < 4; ++i) {
      cJSON* item = cJSON_GetArrayItem(arr, i);
      if (cJSON_IsNumber(item)) {
        s_cfg.max_ma_ch[i] = item->valueint;
      }
    }
  }
  cJSON_Delete(root);
  ESP_LOGI(TAG, "Power limits total=%d mA ch=%d/%d/%d/%d", s_cfg.max_ma_total,
           s_cfg.max_ma_ch[0], s_cfg.max_ma_ch[1], s_cfg.max_ma_ch[2], s_cfg.max_ma_ch[3]);
}

void power_on_frame_begin(void) {
  s_used_total = 0;
}

const power_conf_t* power_cfg(void) {
  return &s_cfg;
}

int power_estimate_ma(int pixels, uint8_t avg_brightness) {
  if (pixels <= 0 || avg_brightness == 0) {
    return 0;
  }
  float frac = (float)avg_brightness / 255.f;
  float per = 60.f * frac;
  int est = (int)(per * (float)pixels);
  if (est < 0) {
    est = 0;
  }
  return est;
}

float power_scale_for_channel(int ch, int est_ma) {
  if (ch < 0 || ch >= 4 || est_ma <= 0) {
    return 1.0f;
  }
  int cap_ch = s_cfg.max_ma_ch[ch] > 0 ? s_cfg.max_ma_ch[ch] : est_ma;
  float scale_ch = cap_ch > 0 ? (float)cap_ch / (float)est_ma : 1.0f;
  if (scale_ch > 1.f) {
    scale_ch = 1.f;
  }
  int remain = s_cfg.max_ma_total - s_used_total;
  float scale_total = remain > 0 ? (float)remain / (float)est_ma : 0.f;
  if (scale_total > 1.f) {
    scale_total = 1.f;
  }
  float scale = scale_ch < scale_total ? scale_ch : scale_total;
  if (scale < 0.f) {
    scale = 0.f;
  }
  s_used_total += (int)((float)est_ma * scale);
  return scale;
}
