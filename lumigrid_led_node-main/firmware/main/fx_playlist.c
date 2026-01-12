#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "fx_playlist.h"
#include "effect_control.h"

static const char *TAG = "FXPLAY";
static fx_playlist_t g_plists[4]; // up to CH_MAX

static char* load_all(const char *path){
FILE *fp = fopen(path, "rb"); if (!fp) return NULL;
fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
char *buf = (char*)malloc(sz+1); if (!buf){ fclose(fp); return NULL; }
fread(buf,1,sz,fp); buf[sz]=0; fclose(fp); return buf;
}

static void free_list(fx_playlist_t *pl){
if (pl->slots){ free(pl->slots); pl->slots=NULL; }
pl->count=0; pl->index=0; pl->running=false;
}

esp_err_t fx_playlist_load(const char *path, int channel, uint32_t default_transition_ms){
if (channel < 0 || channel >= 4) return ESP_ERR_INVALID_ARG;
char *buf = load_all(path);
if (!buf){ ESP_LOGE(TAG, "open %s", path); return ESP_FAIL; }
cJSON *root = cJSON_Parse(buf); free(buf);
if (!root){ ESP_LOGE(TAG, "json"); return ESP_FAIL; }
cJSON *arr = cJSON_GetObjectItem(root, "slots");
if (!cJSON_IsArray(arr)){ cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
int n = cJSON_GetArraySize(arr);
fx_slot_t *slots = (fx_slot_t*)calloc(n, sizeof(fx_slot_t));
if (!slots){ cJSON_Delete(root); return ESP_ERR_NO_MEM; }

for (int i=0;i<n;++i){
cJSON *it = cJSON_GetArrayItem(arr, i);
if (!it) continue;
cJSON* name_item = cJSON_GetObjectItem(it,"name");
if (!name_item || !name_item->valuestring) {
  ESP_LOGE(TAG, "playlist slot %d missing name", i);
  continue;
}
const char *name = name_item->valuestring;
strncpy(slots[i].name, name, sizeof(slots[i].name)-1);
slots[i].duration_ms = (uint32_t)(cJSON_GetObjectItem(it,"duration_ms") ? cJSON_GetObjectItem(it,"duration_ms")->valueint : 5000);
slots[i].brightness = (float)(cJSON_GetObjectItem(it,"brightness") ? cJSON_GetObjectItem(it,"brightness")->valuedouble : 1.0);
cJSON *jp = cJSON_GetObjectItem(it,"params");
slots[i].params.speed = jp && cJSON_GetObjectItem(jp,"speed") ? (float)cJSON_GetObjectItem(jp,"speed")->valuedouble : 1.0f;
slots[i].params.intensity = jp && cJSON_GetObjectItem(jp,"intensity") ? (float)cJSON_GetObjectItem(jp,"intensity")->valuedouble : 0.5f;
slots[i].params.palette_id = (uint8_t)(jp && cJSON_GetObjectItem(jp,"palette_id") ? cJSON_GetObjectItem(jp,"palette_id")->valueint : 0);
const char *col = (jp && cJSON_GetObjectItem(jp,"color")) ? cJSON_GetObjectItem(jp,"color")->valuestring : NULL;
slots[i].params.color = (uint32_t)(col ? strtol(col+1,NULL,16) : 0xFFFFFF);
}

fx_playlist_t *pl = &g_plists[channel];
free_list(pl);
pl->channel = channel;
pl->slots = slots;
pl->count = n;
pl->index = 0;
pl->t_start = 0;
pl->running = false;
pl->transition_ms = default_transition_ms;
cJSON_Delete(root);
ESP_LOGI(TAG, "loaded %d slots for ch%d", n, channel);
return ESP_OK;
}

void fx_playlist_start(int channel){
if (channel < 0 || channel >= 4) return;
fx_playlist_t *pl = &g_plists[channel];
if (!pl->slots || pl->count==0) return;
pl->index = 0;
pl->t_start = 0;
pl->running = true;
}

void fx_playlist_stop(int channel){
if (channel < 0 || channel >= 4) return;
g_plists[channel].running = false;
}

void fx_playlist_tick(uint32_t now_ms){
for (int ch=0; ch<4; ++ch){
fx_playlist_t *pl = &g_plists[ch];
if (!pl->running || !pl->slots || pl->count==0) continue;
if (pl->t_start == 0){
// kick first slot with transition 0
fx_slot_t *s = &pl->slots[pl->index];
effect_params_t p = s->params;
fx_set_channel_by_name(ch, s->name, &p);
fx_set_channel_brightness(ch, s->brightness);
pl->t_start = now_ms;
continue;
}
fx_slot_t *cur = &pl->slots[pl->index];
uint32_t elapsed = now_ms - pl->t_start;
if (elapsed + pl->transition_ms >= cur->duration_ms){
// prepare next
int next_i = (pl->index + 1) % pl->count;
fx_slot_t *nx = &pl->slots[next_i];
fx_transition_to(ch, nx->name, pl->transition_ms, &nx->params);
fx_set_channel_brightness(ch, nx->brightness);
// advance when we cross the slot duration
if (elapsed >= cur->duration_ms){
pl->index = next_i;
pl->t_start = now_ms;
}
}
}
}
