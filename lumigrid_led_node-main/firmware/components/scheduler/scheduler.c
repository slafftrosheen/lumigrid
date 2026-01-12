#include "scheduler.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "scene_loader.h"
#include "storage_fs.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include <time.h>

static const char* TAG="SCHED";
static char* s_cached = NULL;

typedef enum {
  MODE_CONTINUOUS,
  MODE_CALENDAR,
  MODE_TRIGGER
} schedule_mode_t;

typedef struct {
  char preset_name[64];
  char start_time[16];
  int duration;
  char group[32];
} timeline_event_t;

typedef struct {
  char timeline_name[64];
  schedule_mode_t mode;
  time_t start_date;
  time_t end_date;
  char repeat[16];
  char trigger_name[64];
  timeline_event_t events[16];
  int num_events;
} timeline_t;

static timeline_t s_timelines[16];
static int s_timeline_count = 0;

static bool sched_path(char* out, size_t len){
  return storage_fs_path(out, len, "schedule.json");
}

static void apply_preset(const char* preset_name) {
    ESP_LOGI(TAG, "Applying preset: %s", preset_name);
    // Placeholder for preset application logic
}

static void tick(void* arg){
    (void)arg;
    time_t now = time(NULL);
    if (now < 1000) return;
    struct tm lt;
    localtime_r(&now, &lt);

    static int last_min = -1;
    if (lt.tm_min == last_min) return;
    last_min = lt.tm_min;

    for (int i = 0; i < s_timeline_count; i++) {
        timeline_t* timeline = &s_timelines[i];
        if (timeline->mode != MODE_CALENDAR) continue;

        bool is_active = now >= timeline->start_date && (timeline->end_date == 0 || now <= timeline->end_date);
        if (!is_active) continue;

        bool repeat_today = false;
        if (strcmp(timeline->repeat, "daily") == 0) {
            repeat_today = true;
        } else if (strcmp(timeline->repeat, "weekly") == 0) {
            struct tm start_tm;
            localtime_r(&timeline->start_date, &start_tm);
            if (start_tm.tm_wday == lt.tm_wday) {
                repeat_today = true;
            }
        } else if (strcmp(timeline->repeat, "monthly") == 0) {
            struct tm start_tm;
            localtime_r(&timeline->start_date, &start_tm);
            if (start_tm.tm_mday == lt.tm_mday) {
                repeat_today = true;
            }
        } else { // "none"
            repeat_today = true;
        }

        if (!repeat_today) continue;

        for (int j = 0; j < timeline->num_events; j++) {
            timeline_event_t* event = &timeline->events[j];
            int hour, min;
            if (sscanf(event->start_time, "%d:%d", &hour, &min) == 2) {
                if (lt.tm_hour == hour && lt.tm_min == min) {
                    apply_preset(event->preset_name);
                }
            }
        }
    }
}

static void cache_set(const char* json){
  if (s_cached){ free(s_cached); s_cached=NULL; }
  if (json){ s_cached = strdup(json); }
}

static esp_timer_handle_t s_tmr;

void scheduler_init(void){
  const esp_timer_create_args_t a = { .callback = tick, .name="sched" };
  esp_timer_create(&a, &s_tmr);
  esp_timer_start_periodic(s_tmr, 15*1000ULL*1000ULL); // 15s guard; executes once per minute internally
  scheduler_reload();
}

esp_err_t scheduler_reload(void){
    char path[128];
    if (!sched_path(path, sizeof(path))) {
        s_timeline_count = 0;
        cache_set("[]");
        return ESP_OK;
    }
    FILE* f = fopen(path, "r");
    if (!f) {
        s_timeline_count = 0;
        cache_set("[]");
        return ESP_OK;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    cache_set(buf);

    cJSON* root = cJSON_Parse(buf);
    if (!cJSON_IsArray(root)) {
        free(buf);
        s_timeline_count = 0;
        cJSON_Delete(root);
        return ESP_OK;
    }

    s_timeline_count = 0;
    cJSON* timeline_json;
    cJSON_ArrayForEach(timeline_json, root) {
        if (s_timeline_count >= 16) break;
        timeline_t* timeline = &s_timelines[s_timeline_count];
        memset(timeline, 0, sizeof(timeline_t));

        cJSON* name = cJSON_GetObjectItem(timeline_json, "timeline_name");
        if (name) strncpy(timeline->timeline_name, name->valuestring, sizeof(timeline->timeline_name) - 1);

        cJSON* schedule = cJSON_GetObjectItem(timeline_json, "schedule");
        if (schedule) {
            cJSON* mode = cJSON_GetObjectItem(schedule, "mode");
            if (mode) {
                if (strcmp(mode->valuestring, "calendar") == 0) {
                    timeline->mode = MODE_CALENDAR;
                    cJSON* calendar = cJSON_GetObjectItem(schedule, "calendar");
                    if (calendar) {
                        cJSON* start_date = cJSON_GetObjectItem(calendar, "start_date");
                        if (start_date) timeline->start_date = (time_t)start_date->valueint;
                        cJSON* end_date = cJSON_GetObjectItem(calendar, "end_date");
                        if (end_date) timeline->end_date = (time_t)end_date->valueint;
                        cJSON* repeat = cJSON_GetObjectItem(calendar, "repeat");
                        if (repeat) strncpy(timeline->repeat, repeat->valuestring, sizeof(timeline->repeat) - 1);
                    }
                } else if (strcmp(mode->valuestring, "trigger") == 0) {
                    timeline->mode = MODE_TRIGGER;
                    cJSON* trigger = cJSON_GetObjectItem(schedule, "trigger");
                    if (trigger) strncpy(timeline->trigger_name, trigger->valuestring, sizeof(timeline->trigger_name) - 1);
                } else {
                    timeline->mode = MODE_CONTINUOUS;
                }
            }
        }

        cJSON* events = cJSON_GetObjectItem(timeline_json, "events");
        if (events) {
            cJSON* event_json;
            timeline->num_events = 0;
            cJSON_ArrayForEach(event_json, events) {
                if (timeline->num_events >= 16) break;
                timeline_event_t* event = &timeline->events[timeline->num_events];
                cJSON* preset_name = cJSON_GetObjectItem(event_json, "preset_name");
                if (preset_name) strncpy(event->preset_name, preset_name->valuestring, sizeof(event->preset_name) - 1);
                cJSON* start_time = cJSON_GetObjectItem(event_json, "start_time");
                if (start_time) strncpy(event->start_time, start_time->valuestring, sizeof(event->start_time) - 1);
                timeline->num_events++;
            }
        }
        s_timeline_count++;
    }

    free(buf);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t scheduler_save_json(const char* json, int len){
  char path[128];
  if (!sched_path(path, sizeof(path))){
    return ESP_FAIL;
  }
  FILE* f = fopen(path,"w");
  if(!f) return ESP_FAIL;
  fwrite(json,1,len,f);
  fclose(f);
  cache_set(NULL); cache_set(json);
  return scheduler_reload();
}

const char* scheduler_get_json(void){ return s_cached ? s_cached : "[]"; }

void scheduler_trigger(const char* trigger_name) {
    ESP_LOGI(TAG, "Received trigger: %s", trigger_name);
    for (int i = 0; i < s_timeline_count; i++) {
        if (s_timelines[i].mode == MODE_TRIGGER && strcmp(s_timelines[i].trigger_name, trigger_name) == 0) {
            ESP_LOGI(TAG, "Starting timeline: %s", s_timelines[i].timeline_name);
            // In a real implementation, you would start the timeline here.
            // For now, we'll just apply the first preset.
            if (s_timelines[i].num_events > 0) {
                apply_preset(s_timelines[i].events[0].preset_name);
            }
        }
    }
}
