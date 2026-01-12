#include "palettes.h"
#include "cJSON.h"
#include "esp_log.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "storage_fs.h"

static const char* TAG = "PALETTES";
static palette_t s_palettes[16];
static int s_palette_count = 0;
static const palette_t* s_active = NULL;

static uint8_t clamp_u8(float v) {
  if (v < 0.f) v = 0.f;
  if (v > 255.f) v = 255.f;
  return (uint8_t)(v + 0.5f);
}

static void sort_stops(palette_t* p) {
  for (int i = 0; i < p->count - 1; ++i) {
    for (int j = i + 1; j < p->count; ++j) {
      if (p->stops[j].pos < p->stops[i].pos) {
        pal_stop_t t = p->stops[i];
        p->stops[i] = p->stops[j];
        p->stops[j] = t;
      }
    }
  }
}

static void build_lut(palette_t* p) {
  if (!p) {
    return;
  }
  if (p->count == 0) {
    for (int i = 0; i < 256; ++i) {
      p->lut[i] = 0xFFFFFFFFu;
    }
    p->lut_ready = true;
    return;
  }
  sort_stops(p);
  for (int idx = 0; idx < 256; ++idx) {
    int pos = idx;
    const pal_stop_t* a = &p->stops[0];
    const pal_stop_t* b = &p->stops[p->count - 1];
    for (int k = 0; k < p->count - 1; ++k) {
      if (pos >= p->stops[k].pos && pos <= p->stops[k + 1].pos) {
        a = &p->stops[k];
        b = &p->stops[k + 1];
        break;
      }
    }
    float span = (float)(b->pos - a->pos);
    float t = (span == 0.f) ? 0.f : (float)(pos - a->pos) / span;
    uint8_t r = (uint8_t)((1.f - t) * a->r + t * b->r);
    uint8_t g = (uint8_t)((1.f - t) * a->g + t * b->g);
    uint8_t bcol = (uint8_t)((1.f - t) * a->b + t * b->b);
    p->lut[idx] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | bcol;
  }
  p->lut_ready = true;
}

static int load_palette_file(const char* path) {
  if (!path) {
    return -1;
  }
  FILE* f = fopen(path, "r");
  if (!f) {
    ESP_LOGW(TAG, "open fail: %s", path);
    return -1;
  }
  if (s_palette_count >= (int)(sizeof(s_palettes)/sizeof(s_palettes[0]))) {
    ESP_LOGW(TAG, "palette limit reached");
    fclose(f);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fclose(f);
    return -1;
  }
  char* text = (char*)malloc((size_t)size + 1);
  if (!text) {
    fclose(f);
    return -1;
  }
  size_t n = fread(text, 1, (size_t)size, f);
  fclose(f);
  text[n] = '\0';

  cJSON* root = cJSON_Parse(text);
  free(text);
  if (!root) {
    ESP_LOGW(TAG, "json parse fail: %s", path);
    return -1;
  }
  cJSON* jname = cJSON_GetObjectItem(root, "name");
  cJSON* jstops = cJSON_GetObjectItem(root, "stops");
  if (!cJSON_IsString(jname) || !cJSON_IsArray(jstops)) {
    cJSON_Delete(root);
    return -1;
  }
  palette_t* pal = &s_palettes[s_palette_count];
  memset(pal, 0, sizeof(*pal));
  strncpy(pal->name, jname->valuestring, sizeof(pal->name) - 1);

  int stop_idx = 0;
  cJSON* item = NULL;
  cJSON_ArrayForEach(item, jstops) {
    if (stop_idx >= 16) {
      break;
    }
    cJSON* jr = cJSON_GetObjectItem(item, "r");
    cJSON* jg = cJSON_GetObjectItem(item, "g");
    cJSON* jb = cJSON_GetObjectItem(item, "b");
    cJSON* jpos = cJSON_GetObjectItem(item, "pos");
    if (cJSON_IsNumber(jr) && cJSON_IsNumber(jg) && cJSON_IsNumber(jb) && cJSON_IsNumber(jpos)) {
      pal->stops[stop_idx].r = clamp_u8((float)jr->valuedouble);
      pal->stops[stop_idx].g = clamp_u8((float)jg->valuedouble);
      pal->stops[stop_idx].b = clamp_u8((float)jb->valuedouble);
      int p = (int)jpos->valuedouble;
      if (p < 0) p = 0;
      if (p > 255) p = 255;
      pal->stops[stop_idx].pos = (uint8_t)p;
      ++stop_idx;
    }
  }
  pal->count = (uint8_t)stop_idx;
  build_lut(pal);
  ++s_palette_count;
  ESP_LOGI(TAG, "Loaded palette '%s' (%d stops)", pal->name, pal->count);
  cJSON_Delete(root);
  return 0;
}

void palettes_init(void) {
  s_palette_count = 0;
  s_active = NULL;
  char dir[128];
  if (!storage_fs_path(dir, sizeof(dir), "palettes")) {
    ESP_LOGW(TAG, "palette path build failed");
    return;
  }
  palettes_reload_dir(dir);
  if (s_palette_count > 0) {
    s_active = &s_palettes[0];
  }
}

int palettes_reload_dir(const char* dir) {
  if (!dir) {
    return -1;
  }
  char active_name[sizeof(s_palettes[0].name)];
  active_name[0] = '\0';
  if (s_active) {
    strncpy(active_name, s_active->name, sizeof(active_name) - 1);
    active_name[sizeof(active_name) - 1] = '\0';
  }
  DIR* d = opendir(dir);
  if (!d) {
    ESP_LOGW(TAG, "dir missing: %s", dir);
    return -1;
  }
  s_palette_count = 0;
  struct dirent* e = NULL;
  while ((e = readdir(d)) != NULL) {
    // d_name is an array in the struct, always non-null
    const char* ext = strrchr(e->d_name, '.');
    if (!ext || strcasecmp(ext, ".json") != 0) {
      continue;
    }
    char path[512];  // Increased buffer size to handle long paths
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    load_palette_file(path);
  }
  closedir(d);
  if (active_name[0] != '\0') {
    const palette_t* restored = palettes_find(active_name);
    if (restored) {
      s_active = restored;
    }
  }
  if (!s_active && s_palette_count > 0) {
    s_active = &s_palettes[0];
  }
  if (s_palette_count == 0) {
    s_active = NULL;
  }
  return s_palette_count;
}

int palettes_count(void) {
  return s_palette_count;
}

const palette_t* palettes_get(int idx) {
  if (idx < 0 || idx >= s_palette_count) {
    return NULL;
  }
  return &s_palettes[idx];
}

const palette_t* palettes_find(const char* name) {
  if (!name) {
    return NULL;
  }
  for (int i = 0; i < s_palette_count; ++i) {
    if (strcmp(s_palettes[i].name, name) == 0) {
      return &s_palettes[i];
    }
  }
  return NULL;
}

uint32_t palette_sample_argb(const palette_t* p, float t01) {
  if (!p) {
    return 0xFFFFFFFFu;
  }
  if (!p->lut_ready) {
    build_lut((palette_t*)p);
  }
  if (t01 < 0.f) t01 = 0.f;
  if (t01 > 1.f) t01 = 1.f;
  int idx = (int)(t01 * 255.f + 0.5f);
  if (idx < 0) idx = 0;
  if (idx > 255) idx = 255;
  return p->lut[idx];
}

void palettes_set_active(const char* name) {
  const palette_t* p = palettes_find(name);
  if (p) {
    s_active = p;
  }
}

const palette_t* palettes_active(void) {
  return s_active;
}
