/**
 * @file mqttc.c
 * @brief Home Assistant MQTT Discovery integration
 * 
 * This component provides Home Assistant-specific MQTT integration with:
 * - Automatic device discovery via MQTT Discovery protocol
 * - JSON-based state publishing compatible with HA light schema
 * - Effect list integration with Home Assistant
 * 
 * This component operates independently from mqtt_wrapper:
 * - mqttc: Home Assistant MQTT client (this file)
 * - mqtt_wrapper: Generic MQTT client for custom integrations
 * 
 * Both can be active simultaneously for different use cases.
 */

#include "mqttc.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "palettes.h"
#include "effect_control.h"
#include "cJSON.h"
#include "esp_mac.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage_fs.h"

static const char* TAG = "MQTTC";
static esp_mqtt_client_handle_t s_client = NULL;
static char s_cmd_topic[64];
static char s_state_topic[64];
static bool s_connected = false;

static void format_id(char* out, size_t len) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(out, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void publish_ha_config(int ch) {
  if (!s_client) {
    return;
  }
  char id[16];
  format_id(id, sizeof(id));
  char topic[128];
  snprintf(topic, sizeof(topic), "homeassistant/light/lumigrid_%s_%d/config", id, ch);
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", "LumiGrid Channel");
  char uniq[32];
  snprintf(uniq, sizeof(uniq), "%s-%d", id, ch);
  cJSON_AddStringToObject(root, "uniq_id", uniq);
  cJSON_AddStringToObject(root, "cmd_t", s_cmd_topic);
  cJSON_AddStringToObject(root, "stat_t", s_state_topic);
  cJSON_AddStringToObject(root, "schema", "json");
  cJSON_AddBoolToObject(root, "brightness", true);
  cJSON_AddBoolToObject(root, "effect", true);
  cJSON* effects = cJSON_CreateArray();
  size_t count = fx_count();
  for (size_t i = 0; i < count; ++i) {
    const char* name = fx_name(i);
    if (name) {
      cJSON_AddItemToArray(effects, cJSON_CreateString(name));
    }
  }
  cJSON_AddItemToObject(root, "effect_list", effects);
  char* txt = cJSON_PrintUnformatted(root);
  if (txt) {
    esp_mqtt_client_publish(s_client, topic, txt, 0, 1, true);
    cJSON_free(txt);
  }
  cJSON_Delete(root);
}

static void publish_state(void) {
  if (!s_client) {
    return;
  }
  cJSON* root = cJSON_CreateObject();
  const palette_t* pal = palettes_active();
  cJSON_AddStringToObject(root, "palette", pal ? pal->name : "none");
  cJSON* arr = cJSON_AddArrayToObject(root, "channels");
  for (int ch = 0; ch < 4; ++ch) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "ch", ch);
    const char* name = fx_current_name(ch);
    cJSON_AddStringToObject(obj, "effect", name ? name : "");
    int bright = (int)(fx_get_brightness(ch) * 255.f + 0.5f);
    if (bright < 0) bright = 0;
    if (bright > 255) bright = 255;
    cJSON_AddNumberToObject(obj, "brightness", bright);
    cJSON_AddNumberToObject(obj, "speed", fx_get_speed(ch));
    cJSON_AddNumberToObject(obj, "intensity", fx_get_intensity(ch));
    cJSON_AddItemToArray(arr, obj);
  }
  char* txt = cJSON_PrintUnformatted(root);
  if (txt) {
    esp_mqtt_client_publish(s_client, s_state_topic, txt, 0, 1, true);
    cJSON_free(txt);
  }
  cJSON_Delete(root);
}

static void handle_command(const char* payload, int len) {
  if (!payload || len <= 0) {
    return;
  }
  char* buf = (char*)malloc((size_t)len + 1);
  if (!buf) {
    return;
  }
  memcpy(buf, payload, (size_t)len);
  buf[len] = '\0';
  cJSON* root = cJSON_Parse(buf);
  free(buf);
  if (!root) {
    return;
  }
  int ch = 0;
  cJSON* item = cJSON_GetObjectItem(root, "ch");
  if (cJSON_IsNumber(item)) {
    ch = item->valueint;
  }
  cJSON* st = cJSON_GetObjectItem(root, "state");
  if (cJSON_IsString(st)) {
    if (strcmp(st->valuestring, "OFF") == 0) {
      fx_blackout(ch, true);
    } else if (strcmp(st->valuestring, "ON") == 0) {
      fx_blackout(ch, false);
    }
  }
  item = cJSON_GetObjectItem(root, "brightness");
  if (cJSON_IsNumber(item)) {
    fx_set_channel_brightness(ch, (float)item->valuedouble / 255.f);
  }
  item = cJSON_GetObjectItem(root, "speed");
  if (cJSON_IsNumber(item)) {
    fx_set_speed(ch, (float)item->valuedouble);
  }
  item = cJSON_GetObjectItem(root, "intensity");
  if (cJSON_IsNumber(item)) {
    fx_set_intensity(ch, (float)item->valuedouble);
  }
  cJSON* eff = cJSON_GetObjectItem(root, "effect");
  if (cJSON_IsString(eff)) {
    fx_set_channel_by_name(ch, eff->valuestring, NULL);
  }
  cJSON* pal = cJSON_GetObjectItem(root, "palette");
  if (cJSON_IsString(pal)) {
    palettes_set_active(pal->valuestring);
  }
  cJSON_Delete(root);
  mqttc_publish_state_now();
}

static void mqtt_event(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  esp_mqtt_event_handle_t event = event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
      s_connected = true;
      ESP_LOGI(TAG, "MQTT connected");
      char id[16];
      format_id(id, sizeof(id));
      snprintf(s_cmd_topic, sizeof(s_cmd_topic), "lumigrid/%s/cmd", id);
      snprintf(s_state_topic, sizeof(s_state_topic), "lumigrid/%s/state", id);
      esp_mqtt_client_subscribe(s_client, s_cmd_topic, 1);
      for (int ch = 0; ch < 4; ++ch) {
        publish_ha_config(ch);
      }
      mqttc_publish_state_now();
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      s_connected = false;
      ESP_LOGW(TAG, "MQTT disconnected");
      break;
    case MQTT_EVENT_DATA:
      handle_command(event->data, event->data_len);
      break;
    default:
      break;
  }
}

void mqttc_publish_state_now(void) {
  if (!s_connected) {
    return;
  }
  publish_state();
}

void mqttc_init(void) {
  if (s_client) {
    return;
  }
  char host[64] = "";
  int port = 1883;
  char user[32] = "";
  char pass[32] = "";
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "mqtt.json")) {
    path[0] = '\0';
  }
  FILE* f = path[0] ? fopen(path, "r") : NULL;
  if (f) {
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON* root = cJSON_Parse(buf);
    if (root) {
      cJSON* item = cJSON_GetObjectItem(root, "host");
      if (cJSON_IsString(item)) {
        strncpy(host, item->valuestring, sizeof(host) - 1);
      }
      item = cJSON_GetObjectItem(root, "port");
      if (cJSON_IsNumber(item)) {
        port = item->valueint;
      }
      item = cJSON_GetObjectItem(root, "user");
      if (cJSON_IsString(item)) {
        strncpy(user, item->valuestring, sizeof(user) - 1);
      }
      item = cJSON_GetObjectItem(root, "pass");
      if (cJSON_IsString(item)) {
        strncpy(pass, item->valuestring, sizeof(pass) - 1);
      }
      cJSON_Delete(root);
    }
  }
  if (host[0] == '\0') {
    ESP_LOGW(TAG, "MQTT disabled (no host)");
    return;
  }
  esp_mqtt_client_config_t cfg = {
    .broker.address.hostname = host,
    .broker.address.port = port,
    .credentials.username = user[0] ? user : NULL,
    .credentials.authentication.password = pass[0] ? pass : NULL
  };
  s_client = esp_mqtt_client_init(&cfg);
  if (!s_client) {
    ESP_LOGE(TAG, "mqtt init failed");
    return;
  }
  esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
  esp_mqtt_client_start(s_client);
}
