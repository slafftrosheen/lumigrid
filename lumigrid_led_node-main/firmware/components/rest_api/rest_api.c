#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

#include "effect_control.h"
#include "rest_api.h"
#include "fx_playlist.h"
#include "effects.h"
#include "palettes.h"
#include "sync.h"
#include "audio_reactive.h"
#include "scheduler.h"
#include "layout.h"
#include "ota.h"
#include "storage_fs.h"
#include "mqttc.h"
#include "nodesync.h"
#include "graph.h"
#include "webdav.h"
#include "metrics.h"
#include "json_config.h"
#include "trigger_engine.h"
#include "channel_groups.h"
#include "channel_groups.h"

// Batch 26: DMX support is optional
#ifdef CONFIG_DMXI_ENABLED
#include "dmxi.h"
#endif

static const char *TAG = "REST";
static httpd_handle_t s_server = NULL;

// Buffer size for sensor update payloads
#define SENSOR_UPDATE_BUF_SIZE 512

// Batch 27: Embedded UI
// Symbol names are based on basename of embedded file (index.html), not full path
extern const unsigned char _binary_index_html_start[] asm("_binary_index_html_start");
extern const unsigned char _binary_index_html_end[]   asm("_binary_index_html_end");

// External declaration for state retrieval
extern effect_params_t effect_engine_get_params(int ch);

typedef struct {
    int channel;
    char effect_name[32];
    effect_params_t params;
    float brightness;
    esp_timer_handle_t timer;
} channel_backup_t;

static void restore_channel_state_cb(void *arg) {
    channel_backup_t *bkp = (channel_backup_t*)arg;
    if (!bkp) return;

    ESP_LOGI(TAG, "Restoring channel %d state: %s", bkp->channel, bkp->effect_name);
    // Note: fx_set_channel_by_name calls into effect engine which should be thread-safe enough for this use case
    fx_set_channel_by_name(bkp->channel, bkp->effect_name, &bkp->params);
    fx_set_channel_brightness(bkp->channel, bkp->brightness);

    if (bkp->timer) {
        esp_timer_delete(bkp->timer);
    }
    free(bkp);
}

static esp_err_t apply_preset_from_fs(const char *name, int ch, uint32_t dur_ms) {
    // 1. Read preset file
    char path[160];
    if (!storage_fs_mount_path(path, sizeof(path), "presets")) return ESP_FAIL;
    size_t base_len = strlen(path);
    snprintf(path + base_len, sizeof(path) - base_len, "/%s.json", name);

    uint8_t *buf = NULL; size_t len = 0;
    if (storage_fs_read(path, &buf, &len) != ESP_OK) {
        ESP_LOGE(TAG, "Preset '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *root = cJSON_ParseWithLength((const char*)buf, len);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse preset '%s'", name);
        return ESP_FAIL;
    }

    // 2. Validate/Extract
    cJSON *eid = cJSON_GetObjectItem(root, "effect_id");
    if (!eid || !cJSON_IsNumber(eid)) {
         cJSON_Delete(root);
         ESP_LOGE(TAG, "Preset '%s' missing effect_id", name);
         return ESP_FAIL;
    }
    int effect_id_val = eid->valueint;

    // 3. Resolve Effect Name
    char fx_name[32];
    // For now, assume IDs like 1101 map to "1101" string
    snprintf(fx_name, sizeof(fx_name), "%d", effect_id_val);

    // 4. Extract Params
    effect_params_t p = { .speed=1.0f, .intensity=0.5f, .color=0xFFFFFF, .palette_id=0, .brightness=1.0f };
    cJSON *jv;
    if ((jv = cJSON_GetObjectItem(root, "speed")) && cJSON_IsNumber(jv)) p.speed = (float)jv->valuedouble;
    if ((jv = cJSON_GetObjectItem(root, "intensity")) && cJSON_IsNumber(jv)) p.intensity = (float)jv->valuedouble;
    if ((jv = cJSON_GetObjectItem(root, "palette_id")) && cJSON_IsNumber(jv)) p.palette_id = (uint8_t)jv->valueint;
    if ((jv = cJSON_GetObjectItem(root, "brightness")) && cJSON_IsNumber(jv)) p.brightness = (float)jv->valuedouble;

    // Color handling
    cJSON *c1 = cJSON_GetObjectItem(root, "color1");
    if (c1 && cJSON_IsArray(c1) && cJSON_GetArraySize(c1) >= 3) {
        int r = cJSON_GetArrayItem(c1, 0)->valueint;
        int g = cJSON_GetArrayItem(c1, 1)->valueint;
        int b = cJSON_GetArrayItem(c1, 2)->valueint;
        p.color = ((r&0xFF)<<16) | ((g&0xFF)<<8) | (b&0xFF);
    }

    cJSON_Delete(root);

    // 5. Backup if temporary
    if (dur_ms > 0) {
        channel_backup_t *bkp = calloc(1, sizeof(channel_backup_t));
        if (bkp) {
            bkp->channel = ch;
            const char *cur_name = fx_current_name(ch);
            if (cur_name) strncpy(bkp->effect_name, cur_name, sizeof(bkp->effect_name)-1);
            bkp->params = effect_engine_get_params(ch);
            bkp->brightness = fx_get_brightness(ch);

            esp_timer_create_args_t timer_args = {
                .callback = restore_channel_state_cb,
                .arg = bkp,
                .name = "restore_fx"
            };
            esp_timer_handle_t timer;
            if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
                bkp->timer = timer;
                esp_timer_start_once(timer, dur_ms * 1000ULL);
            } else {
                free(bkp); // Failed to create timer
            }
        }
    }

    // 6. Apply
    esp_err_t err = fx_set_channel_by_name(ch, fx_name, &p);
    if (err != ESP_OK) {
         ESP_LOGW(TAG, "Failed to apply effect '%s' (ID %d)", fx_name, effect_id_val);
    } else {
         fx_set_channel_brightness(ch, p.brightness);
    }

    return err;
}

httpd_handle_t rest_api_get_handle(void) {
  return s_server;
}

static esp_err_t fx_list_handler(httpd_req_t *req){
  const char **names = NULL; size_t n = 0;
  if (fx_list(&names, &n) != ESP_OK || n == 0){
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fx list error");
    return ESP_FAIL;
  }
  cJSON *root = cJSON_CreateArray();
  if (!root){
    free((void*)names);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  for (size_t i = 0; i < n; ++i) {
    cJSON *item = cJSON_CreateString(names[i]);
    if (!item) {
      cJSON_Delete(root);
      free((void*)names);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
      return ESP_FAIL;
    }
    cJSON_AddItemToArray(root, item);
  }
  char *out = cJSON_PrintUnformatted(root);
  if (!out){
    cJSON_Delete(root);
    free((void*)names);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, out);
  free(out);
  cJSON_Delete(root);
  free((void*)names);
  return ESP_OK;
}

static uint32_t parse_hex_rgb(const char *s){
  if (!s || strlen(s) != 7 || s[0] != '#') return 0;
  return (uint32_t)strtol(s + 1, NULL, 16);
}

static esp_err_t recv_body(httpd_req_t *req, char *buf, size_t buf_len, size_t *out_len){
  if (!req || !buf || buf_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t content_len = req->content_len;
  if (content_len >= buf_len) {
    return ESP_ERR_INVALID_SIZE;
  }

  size_t total = 0;
  while (total < content_len) {
    size_t to_read = content_len - total;
    size_t space = buf_len - 1 - total;
    if (to_read > space) {
      to_read = space;
    }
    int ret = httpd_req_recv(req, buf + total, to_read);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (ret <= 0) {
      return ESP_FAIL;
    }
    total += (size_t)ret;
  }

  buf[total] = '\0';
  if (out_len) {
    *out_len = total;
  }

  return ESP_OK;
}

static esp_err_t fx_apply_handler(httpd_req_t *req){
  char buf[512]; size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if (err == ESP_ERR_INVALID_SIZE) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "payload too large");
    return ESP_FAIL;
  }
  if (err != ESP_OK || total == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  cJSON *root = cJSON_Parse(buf);
  if (!root){ httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }
  int ch = 0;
  cJSON *jc = cJSON_GetObjectItem(root, "channel");
  if (jc && cJSON_IsNumber(jc)) ch = jc->valueint;
  const char *name = NULL;
  cJSON *jn = cJSON_GetObjectItem(root, "name");
  if (jn && cJSON_IsString(jn)) name = jn->valuestring;
  effect_params_t p = { .speed=1.0f, .intensity=0.5f, .color=0xFFFFFF, .palette_id=0 };
  cJSON *jp = cJSON_GetObjectItem(root, "params");
  if (jp && cJSON_IsObject(jp)){
    cJSON *jv;
    if ((jv = cJSON_GetObjectItem(jp, "speed")) && cJSON_IsNumber(jv))     p.speed = (float)jv->valuedouble;
    if ((jv = cJSON_GetObjectItem(jp, "intensity")) && cJSON_IsNumber(jv)) p.intensity = (float)jv->valuedouble;
    if ((jv = cJSON_GetObjectItem(jp, "color")) && cJSON_IsString(jv))     p.color = parse_hex_rgb(jv->valuestring);
    if ((jv = cJSON_GetObjectItem(jp, "palette_id")) && cJSON_IsNumber(jv)) p.palette_id = (uint8_t)jv->valueint;
  }
  cJSON *jb = cJSON_GetObjectItem(root, "brightness");
  if (name){
    if (fx_set_channel_by_name(ch, name, &p) != ESP_OK){
      cJSON_Delete(root);
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "fx not found");
      return ESP_FAIL;
    }
  }
  if (jb && cJSON_IsNumber(jb)) fx_set_channel_brightness(ch, (float)jb->valuedouble);
  cJSON_Delete(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// POST /api/fx/transition { "channel":0, "name":"fire2012", "duration_ms":1200, "params":{...}, "brightness":0.9 }
static esp_err_t fx_transition_handler(httpd_req_t *req){
 char buf[512]; int total = httpd_req_recv(req, buf, sizeof(buf)-1);
 if (total <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
 buf[total] = 0;
 cJSON *root = cJSON_Parse(buf); if (!root){ httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }
 int ch = cJSON_GetObjectItem(root,"channel") ? cJSON_GetObjectItem(root,"channel")->valueint : 0;
 const char *name = cJSON_GetObjectItem(root,"name") ? cJSON_GetObjectItem(root,"name")->valuestring : NULL;
 uint32_t dur = cJSON_GetObjectItem(root,"duration_ms") ? (uint32_t)cJSON_GetObjectItem(root,"duration_ms")->valueint : 800;
 cJSON *jp = cJSON_GetObjectItem(root,"params");
 effect_params_t p = { .speed=1.0f,.intensity=0.5f,.color=0xFFFFFF,.palette_id=0 };
 if (jp){
 cJSON *v;
 if ((v=cJSON_GetObjectItem(jp,"speed"))) p.speed=(float)v->valuedouble;
 if ((v=cJSON_GetObjectItem(jp,"intensity"))) p.intensity=(float)v->valuedouble;
 if ((v=cJSON_GetObjectItem(jp,"color"))) p.color=(uint32_t)strtol(v->valuestring+1,NULL,16);
 if ((v=cJSON_GetObjectItem(jp,"palette_id"))) p.palette_id=(uint8_t)v->valueint;
 }
 cJSON *jb = cJSON_GetObjectItem(root, "brightness");
 if (name) (void)fx_transition_to(ch, name, dur, &p);
 if (jb) (void)fx_set_channel_brightness(ch, (float)jb->valuedouble);
 cJSON_Delete(root);
 httpd_resp_set_type(req, "application/json");
 httpd_resp_sendstr(req, "{\"ok\":true}");
 return ESP_OK;
}

// POST /api/playlist/load { "channel":0, "path":"/spiffs/playlist.json", "transition_ms":800 }
static esp_err_t pl_load_handler(httpd_req_t *req){
 char buf[256]; int n = httpd_req_recv(req, buf, sizeof(buf)-1); if (n<=0){ httpd_resp_send_err(req, 400, "empty"); return ESP_FAIL; }
 buf[n]=0; cJSON *root=cJSON_Parse(buf); if(!root){ httpd_resp_send_err(req,400,"json"); return ESP_FAIL; }
 int ch = cJSON_GetObjectItem(root,"channel") ? cJSON_GetObjectItem(root,"channel")->valueint : 0;
 const cJSON *jp_path = cJSON_GetObjectItem(root,"path");
 const char *user_path = (jp_path && cJSON_IsString(jp_path)) ? jp_path->valuestring : NULL;
 char resolved_path[160];
 const char *path = user_path;
 if (!path || path[0] == '\0') {
   if (storage_fs_mount_path(resolved_path, sizeof(resolved_path), "playlist.json")) {
     path = resolved_path;
   }
 } else if (path[0] != '/') {
   if (storage_fs_mount_path(resolved_path, sizeof(resolved_path), path)) {
     path = resolved_path;
   }
 }
 uint32_t t = cJSON_GetObjectItem(root,"transition_ms") ? (uint32_t)cJSON_GetObjectItem(root,"transition_ms")->valueint : 800;
 if (!path || path[0] == '\0') {
   cJSON_Delete(root);
   httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path");
   return ESP_FAIL;
 }
 esp_err_t e = fx_playlist_load(path, ch, t);
 cJSON_Delete(root);
 if (e!=ESP_OK){ httpd_resp_send_err(req,500,"load failed"); return ESP_FAIL; }
 httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}

// POST /api/playlist/start { "channel":0 }
static esp_err_t pl_start_handler(httpd_req_t *req){
 char buf[64]; int n = httpd_req_recv(req, buf, sizeof(buf)-1);
 if (n <= 0) { httpd_resp_send_err(req, 400, "empty"); return ESP_FAIL; }
 buf[n]=0; cJSON *r=cJSON_Parse(buf);
 if (!r) { httpd_resp_send_err(req, 400, "json"); return ESP_FAIL; }
 int ch=cJSON_GetObjectItem(r,"channel")?cJSON_GetObjectItem(r,"channel")->valueint:0;
 fx_playlist_start(ch);
 cJSON_Delete(r);
 httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}
// POST /api/playlist/stop { "channel":0 }
static esp_err_t pl_stop_handler(httpd_req_t *req){
 char buf[64]; int n = httpd_req_recv(req, buf, sizeof(buf)-1);
 if (n <= 0) { httpd_resp_send_err(req, 400, "empty"); return ESP_FAIL; }
 buf[n]=0; cJSON *r=cJSON_Parse(buf);
 if (!r) { httpd_resp_send_err(req, 400, "json"); return ESP_FAIL; }
 int ch=cJSON_GetObjectItem(r,"channel")?cJSON_GetObjectItem(r,"channel")->valueint:0;
 fx_playlist_stop(ch);
 cJSON_Delete(r);
 httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}

static esp_err_t palette_list_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "application/json");
  cJSON *arr = cJSON_CreateArray();
  if (!arr){
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
    return ESP_FAIL;
  }
  int count = palettes_count();
  for (int i = 0; i < count; ++i){
    const palette_t* p = palettes_get(i);
    if (!p) {
      continue;
    }
    cJSON *obj = cJSON_CreateObject();
    if (!obj){
      cJSON_Delete(arr);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
      return ESP_FAIL;
    }
    cJSON_AddStringToObject(obj, "name", p->name);
    cJSON_AddNumberToObject(obj, "stops", p->count);
    cJSON_AddItemToArray(arr, obj);
  }
  char *txt = cJSON_PrintUnformatted(arr);
  if (!txt){
    cJSON_Delete(arr);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
    return ESP_FAIL;
  }
  httpd_resp_sendstr(req, txt);
  cJSON_free(txt);
  cJSON_Delete(arr);
  return ESP_OK;
}

static esp_err_t palette_set_handler(httpd_req_t *req){
  size_t len = req->content_len;
  if (len == 0 || len >= 32){
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name");
    return ESP_FAIL;
  }
  char name[32];
  int received = httpd_req_recv(req, name, sizeof(name) - 1);
  if (received <= 0){
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  name[received] = '\0';
  while (received > 0 && (name[received-1] == '\n' || name[received-1] == '\r' || name[received-1] == ' ')) {
    name[--received] = '\0';
  }
  if (!palettes_find(name)){
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "palette");
    return ESP_FAIL;
  }
  palettes_set_active(name);
  mqttc_publish_state_now();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t ota_url_handler(httpd_req_t *req){
  if (req->content_len == 0 || req->content_len >= 512){
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "url");
    return ESP_FAIL;
  }
  char buf[512];
  int n = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (n <= 0){
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  buf[n] = '\0';
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  ota_http_url(buf);
  return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req){
  if (ota_begin_upload() != ESP_OK){
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota");
    return ESP_FAIL;
  }
  char buf[2048];
  int remaining = req->content_len;
  while (remaining > 0){
    int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
    int r = httpd_req_recv(req, buf, to_read);
    if (r <= 0){
      ota_abort();
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "stream");
      return ESP_FAIL;
    }
    remaining -= r;
    if (ota_write_chunk((const uint8_t*)buf, r) != ESP_OK){
      ota_abort();
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota");
      return ESP_FAIL;
    }
  }
  if (ota_end_and_commit() != ESP_OK){
    ota_abort();
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  esp_restart();
  return ESP_OK;
}

// POST /api/sync/set { "bpm": 123.4, "running": true }
static esp_err_t sync_set_handler(httpd_req_t *req){
  char buf[128]; int n=httpd_req_recv(req,buf,sizeof(buf)-1); if(n<=0){httpd_resp_send_err(req,400,"empty");return ESP_FAIL;}
  buf[n]=0; cJSON* r=cJSON_Parse(buf); if(!r){httpd_resp_send_err(req,400,"json");return ESP_FAIL;}
  cJSON* v;
  if ((v=cJSON_GetObjectItem(r,"bpm"))) sync_set_bpm((float)v->valuedouble);
  if ((v=cJSON_GetObjectItem(r,"running"))) ((v->type&cJSON_True)?sync_start():sync_stop());
  cJSON_Delete(r);
  httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}

// POST /api/sync/pulse { }
static esp_err_t sync_pulse_handler(httpd_req_t *req){
  (void)req; uint32_t now_ms = (uint32_t)(esp_timer_get_time()/1000ULL); sync_pulse(now_ms);
  httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}

// POST /api/audio_reactive/set { "enable":true, "mode":"adc", "adc_unit":1, "adc_channel":6, "sample_hz":11025 }
// or { "enable":true, "mode":"i2s", "i2s_port":0, "sample_hz":22050 }
static esp_err_t ar_set_handler(httpd_req_t *req){
  char buf[256]; int n=httpd_req_recv(req,buf,sizeof(buf)-1); if(n<=0){httpd_resp_send_err(req,400,"empty");return ESP_FAIL;}
  buf[n]=0; cJSON *r=cJSON_Parse(buf); if(!r){httpd_resp_send_err(req,400,"json");return ESP_FAIL;}
  cJSON *v; bool enable=false;
  if ((v=cJSON_GetObjectItem(r,"enable"))) enable = (v->type & cJSON_True);
  if (!enable) { ar_disable(); cJSON_Delete(r); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK; }
  const char* mode = cJSON_GetObjectItem(r,"mode") ? cJSON_GetObjectItem(r,"mode")->valuestring : "adc";
  int sample_hz = cJSON_GetObjectItem(r,"sample_hz") ? cJSON_GetObjectItem(r,"sample_hz")->valueint : 11025;
  if (mode && strcmp(mode,"i2s")==0){
    int port = cJSON_GetObjectItem(r,"i2s_port") ? cJSON_GetObjectItem(r,"i2s_port")->valueint : 0;
    ar_enable_i2s(port, sample_hz);
  } else {
    int unit = cJSON_GetObjectItem(r,"adc_unit") ? cJSON_GetObjectItem(r,"adc_unit")->valueint : 1;
    int ch   = cJSON_GetObjectItem(r,"adc_channel") ? cJSON_GetObjectItem(r,"adc_channel")->valueint : 6;
    ar_enable_adc(unit, ch, sample_hz);
  }
  cJSON_Delete(r);
  httpd_resp_set_type(req,"application/json"); httpd_resp_sendstr(req,"{\"ok\":true}"); return ESP_OK;
}

// GET /api/schedule
static esp_err_t sched_get(httpd_req_t *req){
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req, scheduler_get_json());
  return ESP_OK;
}

// POST /api/schedule (raw JSON array)
static esp_err_t sched_post(httpd_req_t *req){
  char buf[2048]; int n=httpd_req_recv(req,buf,sizeof(buf)-1); 
  if(n<=0){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"empty");return ESP_FAIL;}
  buf[n]=0; 
  esp_err_t rc = scheduler_save_json(buf,n);
  if(rc!=ESP_OK){ httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"save"); return ESP_FAIL; }
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}");
  return ESP_OK;
}

// GET /api/layout
static esp_err_t layout_get_handler(httpd_req_t* req){
  cJSON* r = cJSON_CreateObject();
  const layout_t* L = layout_get();
  cJSON* arr = cJSON_AddArrayToObject(r,"channels");
  for (int i=0;i<4;i++){
    cJSON* ch = cJSON_CreateObject();
    cJSON_AddNumberToObject(ch,"type",L->ch[i].type);
    cJSON_AddNumberToObject(ch,"order",L->ch[i].order);
    cJSON_AddNumberToObject(ch,"length",L->ch[i].length);
    cJSON_AddNumberToObject(ch,"pwr_pwm",L->ch[i].pwr_pwm);
    cJSON* segs = cJSON_AddArrayToObject(ch,"segments");
    for (int k=0;k<L->ch[i].segments_count;k++){
      cJSON* sg = cJSON_CreateObject();
      cJSON_AddNumberToObject(sg,"start",L->ch[i].segments[k].start);
      cJSON_AddNumberToObject(sg,"len",  L->ch[i].segments[k].len);
      cJSON_AddBoolToObject  (sg,"reverse",L->ch[i].segments[k].reverse);
      cJSON_AddNumberToObject(sg,"mirror",L->ch[i].segments[k].mirror);
      cJSON_AddItemToArray(segs,sg);
    }
    cJSON_AddItemToArray(arr,ch);
  }
  char* txt = cJSON_PrintUnformatted(r);
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req, txt);
  cJSON_free(txt); cJSON_Delete(r);
  return ESP_OK;
}

// POST /api/layout
static esp_err_t layout_post_handler(httpd_req_t* req){
  char* buf = malloc(4096); 
  if(!buf){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"oom");return ESP_FAIL;}
  int n=httpd_req_recv(req,buf,4095); 
  if(n<=0){free(buf); httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"empty"); return ESP_FAIL;}
  buf[n]=0;
  // write then reload
  char layout_path[128];
  if (!storage_fs_path(layout_path, sizeof(layout_path), "layout.json")){
    free(buf);
    httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"path");
    return ESP_FAIL;
  }
  FILE* f=fopen(layout_path,"w");
  if(!f){ free(buf); httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"fs"); return ESP_FAIL; }
  fwrite(buf,1,n,f);
  fclose(f);
  layout_load(layout_path);
  free(buf);
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}");
  return ESP_OK;
}

// Batch 27: UI root handler
static esp_err_t ui_root(httpd_req_t* req){
  httpd_resp_set_type(req,"text/html; charset=utf-8");
  const char* p=(const char*)_binary_index_html_start;
  size_t n = (size_t)(_binary_index_html_end - _binary_index_html_start);
  return httpd_resp_send(req, p, n);
}

// Batch 27: State snapshot for UI
static esp_err_t api_state(httpd_req_t* req){
  httpd_resp_set_type(req,"application/json");
  char buf[256];
  bool dmx_active = false;
#ifdef CONFIG_DMXI_ENABLED
  dmx_active = dmxi_active() ? true : false;
#endif
  snprintf(buf,sizeof(buf),
    "{\"dmx_active\":%s,\"master\":%s,\"bpm\":%.1f,\"mqtt\":true}",
    dmx_active?"true":"false",
    nodesync_is_master()?"true":"false", 
    sync_get_bpm());
  httpd_resp_sendstr(req, buf); 
  return ESP_OK;
}

// GET /api/status - Get system status
static esp_err_t status_get_handler(httpd_req_t* req){
  char *json_str = rest_api_status_json_alloc();
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  free(json_str);
  return ESP_OK;
}

// Batch 27: Effect apply with simplified params
static esp_err_t fx_apply_new(httpd_req_t* req){
  char buf[512]; 
  size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if(err != ESP_OK || total == 0){ 
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"body"); 
    return ESP_FAIL; 
  }
  
  cJSON* j=cJSON_Parse(buf); 
  if(!j){ 
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"json"); 
    return ESP_FAIL; 
  }
  
  cJSON* ch_obj = cJSON_GetObjectItem(j,"ch");
  if(!ch_obj || !cJSON_IsNumber(ch_obj)) {
    cJSON_Delete(j);
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"missing ch");
    return ESP_FAIL;
  }
  int ch = ch_obj->valueint;
  
  cJSON* e=cJSON_GetObjectItem(j,"effect");
  if (e && cJSON_IsString(e)) {
    fx_set_channel_by_name(ch, e->valuestring, NULL);
  }
  
  cJSON* v;
  if ((v=cJSON_GetObjectItem(j,"speed")) && cJSON_IsNumber(v)) {
    fx_set_speed(ch, (float)v->valuedouble);
  }
  if ((v=cJSON_GetObjectItem(j,"intensity")) && cJSON_IsNumber(v)) {
    fx_set_intensity(ch, (float)v->valuedouble);
  }
  if ((v=cJSON_GetObjectItem(j,"brightness")) && cJSON_IsNumber(v)) {
    fx_set_channel_brightness(ch, (float)v->valuedouble);
  }
  
  cJSON_Delete(j); 
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}"); 
  return ESP_OK;
}

// Batch 27: Graph reload
static esp_err_t graph_reload_ep(httpd_req_t* req){ 
  graph_reload(); 
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}"); 
  return ESP_OK; 
}

// Batch 27: Sync master
static esp_err_t sync_master_ep(httpd_req_t* req){ 
  nodesync_set_master(true); 
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}"); 
  return ESP_OK; 
}

// Batch 27: Broadcast scene
static esp_err_t sync_scene_ep(httpd_req_t* req){
  char b[128]; 
  size_t total = 0;
  esp_err_t err = recv_body(req, b, sizeof(b), &total);
  if(err != ESP_OK || total == 0){ 
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"body"); 
    return ESP_FAIL; 
  }
  
  cJSON* j=cJSON_Parse(b); 
  if(!j){ 
    httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"json"); 
    return ESP_FAIL; 
  }
  
  cJSON* eff_obj = cJSON_GetObjectItem(j,"effect");
  cJSON* bpm_obj = cJSON_GetObjectItem(j,"bpm");
  if(eff_obj && cJSON_IsString(eff_obj) && bpm_obj && cJSON_IsNumber(bpm_obj)){
    const char* eff=eff_obj->valuestring; 
    float bpm=(float)bpm_obj->valuedouble;
    nodesync_broadcast_scene(eff,bpm);
  }
  
  cJSON_Delete(j); 
  httpd_resp_set_type(req,"application/json");
  httpd_resp_sendstr(req,"{\"ok\":true}"); 
  return ESP_OK;
}

// POST /api/config/restore-factory - Restore factory default configuration
static esp_err_t config_restore_factory_handler(httpd_req_t* req){
  ESP_LOGI(TAG, "Restoring factory configuration");
  
  // Load default config if available, otherwise create minimal config
  uint8_t *json_buf = NULL;
  size_t len = 0;
  char def_path[128];
  esp_err_t err = ESP_FAIL;
  
  if (storage_fs_mount_path(def_path, sizeof(def_path), "default_config.json") &&
      storage_fs_read(def_path, &json_buf, &len) == ESP_OK){
    // Write default config to config.json
    char cfg_path[128];
    if (storage_fs_mount_path(cfg_path, sizeof(cfg_path), "config.json")) {
      err = storage_fs_write(cfg_path, json_buf, len);
    }
    free(json_buf);
  } else {
    // Create minimal in-memory default config
    cJSON* default_cfg = cJSON_CreateObject();
    if (default_cfg) {
      cJSON_AddStringToObject(default_cfg, "node_name", "lumigrid-node");
      cJSON_AddNumberToObject(default_cfg, "brightness", 1.0);
      cJSON* strips = cJSON_AddArrayToObject(default_cfg, "strips");
      if (strips) {
        for (int i = 0; i < 4; i++) {
          cJSON* strip = cJSON_CreateObject();
          cJSON_AddNumberToObject(strip, "ch", i);
          cJSON_AddStringToObject(strip, "type", "WS2812B");
          cJSON_AddStringToObject(strip, "order", "GRB");
          cJSON_AddNumberToObject(strip, "length", 120);
          cJSON_AddItemToArray(strips, strip);
        }
      }
      
      char* json_str = cJSON_PrintUnformatted(default_cfg);
      if (json_str) {
        char cfg_path[128];
        if (storage_fs_mount_path(cfg_path, sizeof(cfg_path), "config.json")) {
          err = storage_fs_write(cfg_path, (const uint8_t*)json_str, strlen(json_str));
        }
        free(json_str);
      }
      cJSON_Delete(default_cfg);
    }
  }
  
  // Reload configuration
  config_init();
  
  if (err == ESP_OK) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Factory configuration restored. Please reboot.\"}");
    ESP_LOGI(TAG, "Factory configuration restored successfully");
  } else {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to restore factory config");
    ESP_LOGE(TAG, "Failed to restore factory configuration");
  }
  
  return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

#define PRESET_NAME_MAX_LEN 256

// Validate preset name to prevent path traversal attacks
static bool is_valid_preset_name(const char *name) {
  if (!name || !name[0]) return false;
  
  // Check for path traversal sequences
  if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
    return false;
  }
  
  // Check for only alphanumeric, dash, underscore
  for (const char *p = name; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
      return false;
    }
  }
  
  return true;
}

// Validate RGB color array [R, G, B] where each value is 0-255
static bool validate_color_array(cJSON *color, char *error_buf, size_t error_buf_len) {
  if (!color) return true; // Optional field
  
  if (!cJSON_IsArray(color)) {
    snprintf(error_buf, error_buf_len, "color must be an array");
    return false;
  }
  
  int size = cJSON_GetArraySize(color);
  if (size != 3) {
    snprintf(error_buf, error_buf_len, "color must have exactly 3 elements [R,G,B]");
    return false;
  }
  
  for (int i = 0; i < 3; i++) {
    cJSON *val = cJSON_GetArrayItem(color, i);
    if (!cJSON_IsNumber(val)) {
      snprintf(error_buf, error_buf_len, "color[%d] must be a number", i);
      return false;
    }
    // Use valuedouble and explicitly check it's an integer
    double dv = val->valuedouble;
    int v = (int)dv;
    if (dv != (double)v) {
      snprintf(error_buf, error_buf_len, "color[%d]=%.2f must be an integer", i, dv);
      return false;
    }
    if (v < 0 || v > 255) {
      snprintf(error_buf, error_buf_len, "color[%d]=%d out of range [0,255]", i, v);
      return false;
    }
  }
  
  return true;
}

// Validate preset JSON according to schema (firmware/schemas/preset-led.schema.json)
bool rest_api_validate_preset_json(cJSON *root, char *error_buf, size_t error_buf_len) {
  if (!root || !cJSON_IsObject(root)) {
    snprintf(error_buf, error_buf_len, "preset must be a JSON object");
    return false;
  }
  
  // Required field: preset_name
  cJSON *preset_name = cJSON_GetObjectItem(root, "preset_name");
  if (!preset_name || !cJSON_IsString(preset_name) || !preset_name->valuestring || !preset_name->valuestring[0]) {
    snprintf(error_buf, error_buf_len, "preset_name is required (string, non-empty)");
    return false;
  }
  
  // Validate preset_name pattern and length
  if (strlen(preset_name->valuestring) > 64) {
    snprintf(error_buf, error_buf_len, "preset_name too long (max 64 chars)");
    return false;
  }
  
  if (!is_valid_preset_name(preset_name->valuestring)) {
    snprintf(error_buf, error_buf_len, "preset_name contains invalid characters (use alphanumeric, -, _)");
    return false;
  }
  
  // Required field: effect_id
  cJSON *effect_id = cJSON_GetObjectItem(root, "effect_id");
  if (!effect_id || !cJSON_IsNumber(effect_id)) {
    snprintf(error_buf, error_buf_len, "effect_id is required (integer)");
    return false;
  }
  
  int eid = effect_id->valueint;
  if (eid < 1000 || eid > 9999) {
    snprintf(error_buf, error_buf_len, "effect_id=%d out of range [1000,9999]", eid);
    return false;
  }
  
  // Optional field: description
  cJSON *description = cJSON_GetObjectItem(root, "description");
  if (description) {
    if (!cJSON_IsString(description)) {
      snprintf(error_buf, error_buf_len, "description must be a string");
      return false;
    }
    if (description->valuestring && strlen(description->valuestring) > 256) {
      snprintf(error_buf, error_buf_len, "description too long (max 256 chars)");
      return false;
    }
  }
  
  // Optional numeric fields with ranges
  cJSON *speed = cJSON_GetObjectItem(root, "speed");
  if (speed) {
    if (!cJSON_IsNumber(speed)) {
      snprintf(error_buf, error_buf_len, "speed must be a number");
      return false;
    }
    double s = speed->valuedouble;
    if (s < 0.0 || s > 5.0) {
      snprintf(error_buf, error_buf_len, "speed=%.2f out of range [0.0,5.0]", s);
      return false;
    }
  }
  
  cJSON *intensity = cJSON_GetObjectItem(root, "intensity");
  if (intensity) {
    if (!cJSON_IsNumber(intensity)) {
      snprintf(error_buf, error_buf_len, "intensity must be a number");
      return false;
    }
    double i = intensity->valuedouble;
    if (i < 0.0 || i > 1.0) {
      snprintf(error_buf, error_buf_len, "intensity=%.2f out of range [0.0,1.0]", i);
      return false;
    }
  }
  
  cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
  if (brightness) {
    if (!cJSON_IsNumber(brightness)) {
      snprintf(error_buf, error_buf_len, "brightness must be a number");
      return false;
    }
    double b = brightness->valuedouble;
    if (b < 0.0 || b > 1.0) {
      snprintf(error_buf, error_buf_len, "brightness=%.2f out of range [0.0,1.0]", b);
      return false;
    }
  }
  
  cJSON *palette_id = cJSON_GetObjectItem(root, "palette_id");
  if (palette_id) {
    if (!cJSON_IsNumber(palette_id)) {
      snprintf(error_buf, error_buf_len, "palette_id must be a number");
      return false;
    }
    int pid = palette_id->valueint;
    if (pid < 0 || pid > 255) {
      snprintf(error_buf, error_buf_len, "palette_id=%d out of range [0,255]", pid);
      return false;
    }
  }
  
  // Validate color arrays
  cJSON *color1 = cJSON_GetObjectItem(root, "color1");
  if (color1 && !validate_color_array(color1, error_buf, error_buf_len)) {
    return false;
  }
  
  cJSON *color2 = cJSON_GetObjectItem(root, "color2");
  if (color2 && !validate_color_array(color2, error_buf, error_buf_len)) {
    return false;
  }
  
  cJSON *color3 = cJSON_GetObjectItem(root, "color3");
  if (color3 && !validate_color_array(color3, error_buf, error_buf_len)) {
    return false;
  }

  // Check for unknown properties (additionalProperties: false)
  const char *valid_keys[] = {
    "preset_name", "description", "effect_id", "speed", "intensity",
    "brightness", "palette_id", "color1", "color2", "color3", NULL
  };

  cJSON *child = root->child;
  while (child) {
    bool found = false;
    for (int i = 0; valid_keys[i] != NULL; i++) {
        if (strcmp(child->string, valid_keys[i]) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        snprintf(error_buf, error_buf_len, "unknown property: %s", child->string);
        return false;
    }
    child = child->next;
  }
  
  return true;
}

// POST /api/presets - Save a preset to LittleFS
static esp_err_t presets_post_handler(httpd_req_t *req){
  char buf[2048];
  size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if (err == ESP_ERR_INVALID_SIZE) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "payload too large");
    return ESP_FAIL;
  }
  if (err != ESP_OK || total == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  
  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
    return ESP_FAIL;
  }

  
  // Validate preset JSON against schema
  char error_msg[256];
  if (!rest_api_validate_preset_json(root, error_msg, sizeof(error_msg))) {
    cJSON_Delete(root);
    ESP_LOGW(TAG, "Preset validation failed: %s", error_msg);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error_msg);
    return ESP_FAIL;
  }
  
  // Extract preset name (validated above, guaranteed to exist and be non-empty)
  cJSON *jname = cJSON_GetObjectItem(root, "preset_name");
  const char *name = jname->valuestring;  // Safe due to validation
  
  // Build preset file path
  char path[160];
  if (!storage_fs_mount_path(path, sizeof(path), "presets")) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs unavailable");
    return ESP_FAIL;
  }
  size_t base_len = strlen(path);
  int written = snprintf(path + base_len, sizeof(path) - base_len, "/%s.json", name);
  if (written <= 0 || (size_t)written >= sizeof(path) - base_len) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name too long");
    return ESP_FAIL;
  }
  
  // Save preset to file using atomic write
  char tmp_path[180];
  int tmp_written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  if (tmp_written <= 0 || (size_t)tmp_written >= sizeof(tmp_path)) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "path too long");
    return ESP_FAIL;
  }
  
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  
  FILE *f = fopen(tmp_path, "w");
  if (!f) {
    free(json_str);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "file open");
    return ESP_FAIL;
  }
  
  size_t len = strlen(json_str);
  size_t w = fwrite(json_str, 1, len, f);
  fclose(f);
  free(json_str);
  
  if (w != len) {
    unlink(tmp_path);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "file write");
    return ESP_FAIL;
  }
  
  // Atomic rename
  if (rename(tmp_path, path) != 0) {
    unlink(tmp_path);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "file rename");
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// GET /api/presets - List all presets
static esp_err_t presets_get_handler(httpd_req_t *req){
  char dir[160];
  if (!storage_fs_mount_path(dir, sizeof(dir), "presets")) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs unavailable");
    return ESP_FAIL;
  }
  
  DIR *d = opendir(dir);
  if (!d) {
    // Directory doesn't exist yet - return empty array
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "[]");
    return ESP_OK;
  }
  
  cJSON *arr = cJSON_CreateArray();
  if (!arr) {
    closedir(d);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  
  struct dirent *e = NULL;
  while ((e = readdir(d)) != NULL) {
    const char *ext = strrchr(e->d_name, '.');
    if (!ext || strcasecmp(ext, ".json") != 0) {
      continue;
    }
    
    // Extract name without extension
    char name[PRESET_NAME_MAX_LEN];
    size_t name_len = ext - e->d_name;
    if (name_len >= sizeof(name)) {
      continue;
    }
    strncpy(name, e->d_name, name_len);
    name[name_len] = '\0';
    
    cJSON *obj = cJSON_CreateObject();
    if (obj) {
      cJSON_AddStringToObject(obj, "name", name);
      cJSON_AddItemToArray(arr, obj);
    }
  }
  closedir(d);
  
  char *txt = cJSON_PrintUnformatted(arr);
  cJSON_Delete(arr);
  
  if (!txt) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, txt);
  cJSON_free(txt);
  return ESP_OK;
}

// DELETE /api/presets/{name} - Delete a preset
static esp_err_t presets_delete_handler(httpd_req_t *req){
  char name[PRESET_NAME_MAX_LEN];
  if (!rest_api_extract_preset_name(req->uri, name, sizeof(name))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid preset name");
    return ESP_FAIL;
  }
  
  // Validate preset name to prevent path traversal
  if (!is_valid_preset_name(name)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid name");
    return ESP_FAIL;
  }
  
  esp_err_t err = rest_api_delete_preset(name);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "preset not found");
    return ESP_FAIL;
  }
  
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// GET /api/presets/{name} - Load a preset
static esp_err_t presets_get_by_name_handler(httpd_req_t *req){
  char name[PRESET_NAME_MAX_LEN];
  if (!rest_api_extract_preset_name(req->uri, name, sizeof(name))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid preset name");
    return ESP_FAIL;
  }
  
  // Validate preset name to prevent path traversal
  if (!is_valid_preset_name(name)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid name");
    return ESP_FAIL;
  }
  
  char path[160];
  if (!storage_fs_mount_path(path, sizeof(path), "presets")) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fs unavailable");
    return ESP_FAIL;
  }
  size_t base_len = strlen(path);
  int written = snprintf(path + base_len, sizeof(path) - base_len, "/%s.json", name);
  if (written <= 0 || (size_t)written >= sizeof(path) - base_len) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name too long");
    return ESP_FAIL;
  }
  
  uint8_t *buf = NULL;
  size_t len = 0;
  esp_err_t err = storage_fs_read(path, &buf, &len);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "preset not found");
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, (const char *)buf, len);
  free(buf);
  return ESP_OK;
}

// GET /api/config - Read current configuration
static esp_err_t config_get_handler(httpd_req_t* req){
  if (!config_lock()) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config lock timeout");
    return ESP_FAIL;
  }
  
  const cJSON *root = config_root();
  if (!root) {
    config_unlock();
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    return ESP_FAIL;
  }
  
  char *json_str = cJSON_PrintUnformatted(root);
  config_unlock();
  
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc");
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_free(json_str);
  return ESP_OK;
}

// POST /api/config - Update configuration (partial updates allowed)
static esp_err_t config_post_handler(httpd_req_t* req){
  char buf[2048];
  size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if (err == ESP_ERR_INVALID_SIZE) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "payload too large");
    return ESP_FAIL;
  }
  if (err != ESP_OK || total == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  
  cJSON *updates = cJSON_Parse(buf);
  if (!updates) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
    return ESP_FAIL;
  }
  
  if (!cJSON_IsObject(updates)) {
    cJSON_Delete(updates);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "config must be JSON object");
    return ESP_FAIL;
  }

  // Validate config JSON against schema
  char error_msg[256];
  if (!rest_api_validate_config_json(updates, error_msg, sizeof(error_msg))) {
    cJSON_Delete(updates);
    ESP_LOGW(TAG, "Config validation failed: %s", error_msg);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error_msg);
    return ESP_FAIL;
  }
  
  // Merge updates into current config
  config_merge(updates);
  cJSON_Delete(updates);
  
  // Save to filesystem
  err = config_save();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save config: %d", err);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    return ESP_FAIL;
  }
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  ESP_LOGI(TAG, "Configuration updated successfully");
  return ESP_OK;
}

// POST /api/trigger - One-shot action or bounded override
static esp_err_t trigger_handler(httpd_req_t* req){
  char buf[1024];
  size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if (err == ESP_ERR_INVALID_SIZE) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "payload too large");
    return ESP_FAIL;
  }
  if (err != ESP_OK || total == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  
  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
    return ESP_FAIL;
  }

  // Validate trigger JSON against schema
  char error_msg[256];
  if (!rest_api_validate_trigger_json(root, error_msg, sizeof(error_msg))) {
    cJSON_Delete(root);
    ESP_LOGW(TAG, "Trigger validation failed: %s", error_msg);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error_msg);
    return ESP_FAIL;
  }
  
  // Extract trigger parameters
  cJSON *action = cJSON_GetObjectItem(root, "action");
  cJSON *duration = cJSON_GetObjectItem(root, "duration_ms");
  cJSON *preset = cJSON_GetObjectItem(root, "preset");
  cJSON *channel = cJSON_GetObjectItem(root, "channel");
  
  // Log the trigger action for debugging
  if (action && cJSON_IsString(action)) {
    ESP_LOGI(TAG, "Trigger action: %s", action->valuestring);
    
    // Handle common trigger actions
    if (strcmp(action->valuestring, "flash") == 0) {
      // Trigger a brief flash effect using the strobe trigger
      uint32_t dur_ms = (duration && cJSON_IsNumber(duration)) ? (uint32_t)duration->valueint : 500;
      
      trigger_strobe(dur_ms);
      ESP_LOGI(TAG, "Flash trigger for %" PRIu32 " ms", dur_ms);
    } 
    else if (strcmp(action->valuestring, "pulse") == 0) {
      // Trigger a pulse effect - use strobe with longer duration
      uint32_t dur_ms = (duration && cJSON_IsNumber(duration)) ? (uint32_t)duration->valueint : 1000;
      trigger_strobe(dur_ms);
      ESP_LOGI(TAG, "Pulse trigger for %" PRIu32 " ms", dur_ms);
    }
    else if (strcmp(action->valuestring, "apply_preset") == 0) {
      // Apply a preset temporarily
      if (preset && cJSON_IsString(preset)) {
        int ch = (channel && cJSON_IsNumber(channel)) ? channel->valueint : 0;
        uint32_t dur_ms = (duration && cJSON_IsNumber(duration)) ? (uint32_t)duration->valueint : 0;
        ESP_LOGI(TAG, "Applying preset '%s' on channel %d for %" PRIu32 " ms", 
                 preset->valuestring, ch, dur_ms);

        apply_preset_from_fs(preset->valuestring, ch, dur_ms);
      }
    }
    else if (strcmp(action->valuestring, "blackout") == 0) {
      // Immediate blackout - turn off all channels
      trigger_blackout();
      ESP_LOGI(TAG, "Blackout trigger");
    }
    else if (strcmp(action->valuestring, "play_preset") == 0) {
      if (preset && cJSON_IsString(preset)) {
        int ch = (channel && cJSON_IsNumber(channel)) ? channel->valueint : 0;
        ESP_LOGI(TAG, "Applying preset '%s' on channel %d", preset->valuestring, ch);
        // TODO: Implement preset loading and temporary application
      }
      // Immediate blackout - turn off all channels
      trigger_blackout();
      ESP_LOGI(TAG, "Blackout trigger");
    }
  }
  
  cJSON_Delete(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// GET /api/discover/needs_sensor_data - Indicate if this node accepts sensor data
static esp_err_t discover_needs_sensor_data_handler(httpd_req_t* req){
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"needs_data\":true,\"types\":[\"temperature\",\"humidity\",\"motion\",\"light\"]}");
  return ESP_OK;
}

// POST /api/sensor_update - Receive sensor data from sensor nodes
static esp_err_t sensor_update_handler(httpd_req_t* req){
  char buf[SENSOR_UPDATE_BUF_SIZE];
  size_t total = 0;
  esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
  if (err == ESP_ERR_INVALID_SIZE) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "payload too large");
    return ESP_FAIL;
  }
  if (err != ESP_OK || total == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    return ESP_FAIL;
  }
  
  cJSON *root = cJSON_Parse(buf);
  if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
    return ESP_FAIL;
  }
  
  // Extract sensor data fields
  const char *node_id = "unknown";
  cJSON *jn = cJSON_GetObjectItem(root, "node_id");
  if (jn && cJSON_IsString(jn) && jn->valuestring && jn->valuestring[0] != '\0') {
    node_id = jn->valuestring;
  }
  
  uint32_t timestamp = 0;
  cJSON *jt = cJSON_GetObjectItem(root, "timestamp");
  if (jt && cJSON_IsNumber(jt)) {
    timestamp = (uint32_t)jt->valuedouble;
  }
  
  cJSON *jr = cJSON_GetObjectItem(root, "readings");
  if (jr && cJSON_IsObject(jr)) {
    // Log the sensor readings
    ESP_LOGI(TAG, "Sensor update from %s (timestamp=%" PRIu32 "):", 
             node_id, timestamp);
    
    cJSON *jv;
    if ((jv = cJSON_GetObjectItem(jr, "temperature_c")) && cJSON_IsNumber(jv)) {
      ESP_LOGI(TAG, "  Temperature: %.2f°C", jv->valuedouble);
    }
    if ((jv = cJSON_GetObjectItem(jr, "humidity_percent")) && cJSON_IsNumber(jv)) {
      ESP_LOGI(TAG, "  Humidity: %.2f%%", jv->valuedouble);
    }
    if ((jv = cJSON_GetObjectItem(jr, "motion_detected")) && cJSON_IsBool(jv)) {
      ESP_LOGI(TAG, "  Motion: %s", cJSON_IsTrue(jv) ? "detected" : "not detected");
    }
    if ((jv = cJSON_GetObjectItem(jr, "light_level")) && cJSON_IsNumber(jv)) {
      ESP_LOGI(TAG, "  Light level: %.2f", jv->valuedouble);
    }
    
    // TODO: Integrate sensor-reactive effects:
    // - temperature_c: trigger temperature-reactive LED color changes (cool/warm palettes)
    // - humidity_percent: adjust effect intensity or speed based on humidity
    // - motion_detected: activate motion-triggered sequences or scenes
    // - light_level: adjust brightness based on ambient light (auto-brightness)
    // For now, we just log the data
  }
  
  cJSON_Delete(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

esp_err_t rest_api_start(void)
{
  if (s_server) {
    return ESP_OK;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    s_server = NULL;
    return ESP_FAIL;
  }
  static const httpd_uri_t uri_fx_list = {
    .uri = "/api/fx",
    .method = HTTP_GET,
    .handler = fx_list_handler,
    .user_ctx = NULL
  };
  static const httpd_uri_t uri_fx_apply = {
    .uri = "/api/fx/apply",
    .method = HTTP_POST,
    .handler = fx_apply_handler,
    .user_ctx = NULL
  };
  static const httpd_uri_t uri_fx_transition = {
    .uri="/api/fx/transition", .method=HTTP_POST, .handler=fx_transition_handler, .user_ctx=NULL
  };
  httpd_register_uri_handler(s_server, &uri_fx_list);
  httpd_register_uri_handler(s_server, &uri_fx_apply);
  httpd_register_uri_handler(s_server, &uri_fx_transition);
  static const httpd_uri_t uri_pl_load = { .uri="/api/playlist/load", .method=HTTP_POST, .handler=pl_load_handler, .user_ctx=NULL };
  static const httpd_uri_t uri_pl_start = { .uri="/api/playlist/start", .method=HTTP_POST, .handler=pl_start_handler, .user_ctx=NULL };
  static const httpd_uri_t uri_pl_stop = { .uri="/api/playlist/stop", .method=HTTP_POST, .handler=pl_stop_handler, .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_pl_load);
  httpd_register_uri_handler(s_server, &uri_pl_start);
  httpd_register_uri_handler(s_server, &uri_pl_stop);
  static const httpd_uri_t uri_palette_list = { .uri="/api/palettes", .method=HTTP_GET, .handler=palette_list_handler, .user_ctx=NULL };
  static const httpd_uri_t uri_palette_set  = { .uri="/api/palette",  .method=HTTP_POST, .handler=palette_set_handler,  .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_palette_list);
  httpd_register_uri_handler(s_server, &uri_palette_set);
  static const httpd_uri_t uri_sync_set   = { .uri="/api/sync/set",   .method=HTTP_POST, .handler=sync_set_handler,   .user_ctx=NULL };
  static const httpd_uri_t uri_sync_pulse = { .uri="/api/sync/pulse", .method=HTTP_POST, .handler=sync_pulse_handler, .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_sync_set);
  httpd_register_uri_handler(s_server, &uri_sync_pulse);
  static const httpd_uri_t uri_ar_set = { .uri="/api/audio_reactive/set", .method=HTTP_POST, .handler=ar_set_handler, .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_ar_set);
  static const httpd_uri_t uri_fx_list_new = { .uri="/api/fx/list", .method=HTTP_GET, .handler=fx_list_handler, .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_fx_list_new);
  
  static const httpd_uri_t uri_sched_get = { .uri="/api/schedule", .method=HTTP_GET, .handler=sched_get, .user_ctx=NULL };
  static const httpd_uri_t uri_sched_post= { .uri="/api/schedule", .method=HTTP_POST,.handler=sched_post,.user_ctx=NULL };
  httpd_register_uri_handler(s_server,&uri_sched_get);
  httpd_register_uri_handler(s_server,&uri_sched_post);
  
  static const httpd_uri_t uri_layout_get = { .uri="/api/layout", .method=HTTP_GET, .handler=layout_get_handler, .user_ctx=NULL };
  static const httpd_uri_t uri_layout_post= { .uri="/api/layout", .method=HTTP_POST,.handler=layout_post_handler,.user_ctx=NULL };
  httpd_register_uri_handler(s_server,&uri_layout_get);
  httpd_register_uri_handler(s_server,&uri_layout_post);

  static const httpd_uri_t uri_ota_url = { .uri="/api/ota/url", .method=HTTP_POST, .handler=ota_url_handler, .user_ctx=NULL };
  static const httpd_uri_t uri_ota_upload = { .uri="/api/ota/upload", .method=HTTP_POST, .handler=ota_upload_handler, .user_ctx=NULL };
  httpd_register_uri_handler(s_server, &uri_ota_url);
  httpd_register_uri_handler(s_server, &uri_ota_upload);

  // Batch 27: New endpoints for embedded UI
  static const httpd_uri_t u_root = { .uri="/", .method=HTTP_GET, .handler=ui_root, .user_ctx=NULL };
  static const httpd_uri_t u_state   ={ .uri="/api/state",        .method=HTTP_GET,  .handler=api_state, .user_ctx=NULL };
  static const httpd_uri_t u_fxapply ={ .uri="/api/fx/apply",     .method=HTTP_POST, .handler=fx_apply_new, .user_ctx=NULL };
  static const httpd_uri_t u_greload ={ .uri="/api/graph/reload", .method=HTTP_GET,  .handler=graph_reload_ep, .user_ctx=NULL };
  static const httpd_uri_t u_master  ={ .uri="/api/sync/master",  .method=HTTP_GET,  .handler=sync_master_ep, .user_ctx=NULL };
  static const httpd_uri_t u_scene   ={ .uri="/api/sync/scene",   .method=HTTP_POST, .handler=sync_scene_ep, .user_ctx=NULL };
  httpd_register_uri_handler(s_server,&u_root);
  httpd_register_uri_handler(s_server,&u_state);
  httpd_register_uri_handler(s_server,&u_fxapply);
  httpd_register_uri_handler(s_server,&u_greload);
  httpd_register_uri_handler(s_server,&u_master);
  httpd_register_uri_handler(s_server,&u_scene);

  // Status endpoint
  static const httpd_uri_t u_status_get = {
    .uri="/api/status",
    .method=HTTP_GET,
    .handler=status_get_handler,
    .user_ctx=NULL
  };
  httpd_register_uri_handler(s_server, &u_status_get);

  // Config management endpoints
  static const httpd_uri_t u_config_get = {
    .uri="/api/config",
    .method=HTTP_GET,
    .handler=config_get_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_config_post = {
    .uri="/api/config",
    .method=HTTP_POST,
    .handler=config_post_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_restore_factory = { 
    .uri="/api/config/restore-factory", 
    .method=HTTP_POST, 
    .handler=config_restore_factory_handler, 
    .user_ctx=NULL 
  };
  httpd_register_uri_handler(s_server, &u_config_get);
  httpd_register_uri_handler(s_server, &u_config_post);
  httpd_register_uri_handler(s_server, &u_restore_factory);

  // Trigger endpoint
  static const httpd_uri_t u_trigger = {
    .uri="/api/trigger",
    .method=HTTP_POST,
    .handler=trigger_handler,
    .user_ctx=NULL
  };
  httpd_register_uri_handler(s_server, &u_trigger);

  // Sensor data endpoints
  static const httpd_uri_t u_discover_needs_sensor = {
    .uri="/api/discover/needs_sensor_data",
    .method=HTTP_GET,
    .handler=discover_needs_sensor_data_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_sensor_update = {
    .uri="/api/sensor_update",
    .method=HTTP_POST,
    .handler=sensor_update_handler,
    .user_ctx=NULL
  };
  httpd_register_uri_handler(s_server, &u_discover_needs_sensor);
  httpd_register_uri_handler(s_server, &u_sensor_update);

  // Preset management endpoints
  static const httpd_uri_t u_presets_list = {
    .uri="/api/presets",
    .method=HTTP_GET,
    .handler=presets_get_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_presets_save = {
    .uri="/api/presets",
    .method=HTTP_POST,
    .handler=presets_post_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_presets_get_by_name = {
    .uri="/api/presets/*",
    .method=HTTP_GET,
    .handler=presets_get_by_name_handler,
    .user_ctx=NULL
  };
  static const httpd_uri_t u_presets_delete = {
    .uri="/api/presets/*",
    .method=HTTP_DELETE,
    .handler=presets_delete_handler,
    .user_ctx=NULL
  };
  httpd_register_uri_handler(s_server, &u_presets_list);
  httpd_register_uri_handler(s_server, &u_presets_save);
  httpd_register_uri_handler(s_server, &u_presets_get_by_name);
  httpd_register_uri_handler(s_server, &u_presets_delete);

  // Batch 29: WebDAV to /storage
  webdav_register(s_server);

  // Batch 32: Prometheus-style /metrics endpoint
  metrics_register(s_server);

  // Sequencer and PWM effects API
  rest_api_register_sequencer(s_server);
  rest_api_register_pwm(s_server);

  // Channel group endpoints
    static const httpd_uri_t uri_group_post = {
        .uri = "/api/groups",
        .method = HTTP_POST,
        .handler = group_post_handler,
        .user_ctx = NULL
    };
    static const httpd_uri_t uri_group_delete = {
        .uri = "/api/groups/*",
        .method = HTTP_DELETE,
        .handler = group_delete_handler,
        .user_ctx = NULL
    };
    static const httpd_uri_t uri_group_get = {
        .uri = "/api/groups/*",
        .method = HTTP_GET,
        .handler = group_get_handler,
        .user_ctx = NULL
    };
    static const httpd_uri_t uri_group_put = {
        .uri = "/api/groups/*",
        .method = HTTP_PUT,
        .handler = group_put_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &uri_group_post);
    httpd_register_uri_handler(s_server, &uri_group_delete);
    httpd_register_uri_handler(s_server, &uri_group_get);
    httpd_register_uri_handler(s_server, &uri_group_put);

  // Setup wizard API
  rest_api_setup_register(s_server);

  ESP_LOGI(TAG, "REST API started");
  return ESP_OK;
}

void rest_api_stop(void)
{
  if (s_server) {
    httpd_stop(s_server);
    s_server = NULL;
  }
}

char *rest_api_status_json_alloc(void)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "node_type", "lumigrid");
  cJSON_AddStringToObject(root, "role", "fx_node");
  cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000ULL));
  cJSON_AddNumberToObject(root, "heap_free_kb", (double)(esp_get_free_heap_size() / 1024));
  cJSON *fps = cJSON_CreateObject();
  cJSON_AddNumberToObject(fps, "ch0", 0);
  cJSON_AddItemToObject(root, "fps", fps);
  cJSON_AddNumberToObject(root, "pwm_max_duty", 0);
  cJSON_AddNullToObject(root, "last_error");
  cJSON_AddNumberToObject(root, "power_mA", 0);
  char *txt = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return txt;
}

char *rest_api_status_event_alloc(void)
{
  char *json = rest_api_status_json_alloc();
  if (!json) return NULL;
  size_t len = strlen(json) + 32;
  char *event = (char *)malloc(len);
  if (!event){
    free(json);
    return NULL;
  }
  snprintf(event, len, "event: status\ndata: %s\n\n", json);
  free(json);
  return event;
}

bool rest_api_extract_preset_name(const char *uri, char *out, size_t out_len)
{
  if (!uri || !out || out_len == 0) return false;
  const char *base = strstr(uri, "/api/presets/");
  if (!base) return false;
  base += strlen("/api/presets/");
  if (*base == '\0' || strchr(base, '/')) return false;
  size_t len = strlen(base);
  if (len == 0 || len >= out_len) return false;
  // Safe copy with explicit null termination
  size_t copy_len = len < (out_len - 1) ? len : (out_len - 1);
  memcpy(out, base, copy_len);
  out[copy_len] = '\0';
  char *dot = strrchr(out, '.');
  if (dot && strcmp(dot, ".json") == 0) *dot = '\0';
  return strlen(out) > 0;
}

esp_err_t rest_api_delete_preset(const char *name)
{
  if (!name) return ESP_ERR_INVALID_ARG;
  char path[160];
  if (!storage_fs_mount_path(path, sizeof(path), "presets")) {
    return ESP_FAIL;
  }
  size_t base_len = strlen(path);
  int written = snprintf(path + base_len, sizeof(path) - base_len, "/%s.json", name);
  if (written <= 0 || (size_t)written >= sizeof(path) - base_len) return ESP_FAIL;
  if (unlink(path) != 0) return ESP_FAIL;
  return ESP_OK;
}

// POST /api/groups - Create a new channel group
static esp_err_t group_post_handler(httpd_req_t *req) {
    char buf[512];
    size_t total = 0;
    esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
    if (err != ESP_OK || total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
        return ESP_FAIL;
    }

    cJSON *name_obj = cJSON_GetObjectItem(root, "name");
    cJSON *channels_obj = cJSON_GetObjectItem(root, "channels");

    if (!name_obj || !cJSON_IsString(name_obj) || !channels_obj || !cJSON_IsArray(channels_obj)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'name' or 'channels'");
        return ESP_FAIL;
    }

    uint8_t channels[16];
    int num_channels = 0;
    cJSON* channel_val;
    cJSON_ArrayForEach(channel_val, channels_obj) {
        if (cJSON_IsNumber(channel_val) && num_channels < 16) {
            channels[num_channels++] = channel_val->valueint;
        }
    }

    err = channel_groups_add(name_obj->valuestring, channels, num_channels);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create group");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// Validate config JSON against schema
bool rest_api_validate_config_json(cJSON *root, char *error_buf, size_t error_buf_len) {
    if (!root || !cJSON_IsObject(root)) {
        snprintf(error_buf, error_buf_len, "config must be a JSON object");
        return false;
    }

    cJSON *node_id = cJSON_GetObjectItem(root, "node_id");
    if (node_id && (!cJSON_IsString(node_id) || !node_id->valuestring || !node_id->valuestring[0])) {
        snprintf(error_buf, error_buf_len, "node_id must be a non-empty string");
        return false;
    }

    cJSON *node_name = cJSON_GetObjectItem(root, "node_name");
    if (node_name && !cJSON_IsString(node_name)) {
        snprintf(error_buf, error_buf_len, "node_name must be a string");
        return false;
    }

    return true;
}

// Validate trigger JSON against schema
bool rest_api_validate_trigger_json(cJSON *root, char *error_buf, size_t error_buf_len) {
    if (!root || !cJSON_IsObject(root)) {
        snprintf(error_buf, error_buf_len, "trigger must be a JSON object");
        return false;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!action || !cJSON_IsString(action) || !action->valuestring || !action->valuestring[0]) {
        snprintf(error_buf, error_buf_len, "action is required (string, non-empty)");
        return false;
    }

    return true;
}

// DELETE /api/groups/{name} - Delete a channel group
static esp_err_t group_delete_handler(httpd_req_t *req) {
    char name[64];
    if (sscanf(req->uri, "/api/groups/%63s", name) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid group name");
        return ESP_FAIL;
    }

    if (channel_groups_remove(name) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Group not found");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /api/groups/{name} - Get a channel group
static esp_err_t group_get_handler(httpd_req_t *req) {
    char name[64];
    if (sscanf(req->uri, "/api/groups/%63s", name) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid group name");
        return ESP_FAIL;
    }

    const channel_group_t* group = channel_groups_get(name);
    if (!group) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Group not found");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", group->name);
    cJSON *channels = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < group->num_channels; i++) {
        cJSON_AddItemToArray(channels, cJSON_CreateNumber(group->channels[i]));
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

// PUT /api/groups/{name} - Update a channel group
static esp_err_t group_put_handler(httpd_req_t *req) {
    char name[64];
    if (sscanf(req->uri, "/api/groups/%63s", name) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid group name");
        return ESP_FAIL;
    }

    char buf[512];
    size_t total = 0;
    esp_err_t err = recv_body(req, buf, sizeof(buf), &total);
    if (err != ESP_OK || total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json");
        return ESP_FAIL;
    }

    cJSON *channels_obj = cJSON_GetObjectItem(root, "channels");
    if (!channels_obj || !cJSON_IsArray(channels_obj)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'channels'");
        return ESP_FAIL;
    }

    uint8_t channels[16];
    int num_channels = 0;
    cJSON* channel_val;
    cJSON_ArrayForEach(channel_val, channels_obj) {
        if (cJSON_IsNumber(channel_val) && num_channels < 16) {
            channels[num_channels++] = channel_val->valueint;
        }
    }

    // A simple update is to remove the old group and add a new one
    channel_groups_remove(name);
    err = channel_groups_add(name, channels, num_channels);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update group");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}
