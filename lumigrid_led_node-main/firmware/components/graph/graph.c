#include "graph.h"
#include "cJSON.h"
#include "esp_log.h"
#include "effects.h"
#include "effects_registry.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "storage_fs.h"

static const char* TAG = "GRAPH";

typedef enum { NODE_SRC = 1, NODE_BLEND = 2, NODE_OUT = 3 } node_type_t;
typedef enum { BLEND_ADD = 0, BLEND_MULTIPLY = 1, BLEND_SCREEN = 2, BLEND_ALPHA = 3 } blend_t;

typedef struct {
  char  effect[32];
  float speed;
  float intensity;
  float brightness;
  float phase;
} node_src_t;

typedef struct {
  int a;
  int b;
  blend_t mode;
  float opacity;
} node_blend_t;

typedef struct {
  int node_idx;
  int channel;
} node_out_t;

typedef struct {
  node_type_t type;
  union {
    node_src_t src;
    node_blend_t blend;
    node_out_t out;
  } data;
  const effect_desc_t* desc;
  effect_state_t* state;
  px_rgba_t* fb;
  int fb_len;
  int state_npx;
} node_t;

static node_t* s_nodes = NULL;
static int s_node_count = 0;
static bool s_ready = false;

static void free_node(node_t* n) {
  if (!n) {
    return;
  }
  if (n->state) {
    if (n->state->destroy) {
      n->state->destroy(n->state);
    }
    free(n->state);
    n->state = NULL;
  }
  if (n->fb) {
    free(n->fb);
    n->fb = NULL;
  }
  n->fb_len = 0;
  n->state_npx = 0;
  n->desc = NULL;
}

static void free_all_nodes(void) {
  if (!s_nodes) {
    return;
  }
  for (int i = 0; i < s_node_count; ++i) {
    free_node(&s_nodes[i]);
  }
  free(s_nodes);
  s_nodes = NULL;
  s_node_count = 0;
}

static px_rgba_t* ensure_fb(node_t* n, int npx) {
  if (!n) {
    return NULL;
  }
  if (npx <= 0) {
    npx = 1;
  }
  if (n->fb_len != npx) {
    const int old_len = n->fb_len;
    size_t bytes = (size_t)npx * sizeof(px_rgba_t);
    px_rgba_t* new_fb = (px_rgba_t*)realloc(n->fb, bytes);
    if (!new_fb) {
      ESP_LOGE(TAG, "fb alloc %d failed", npx);
      return NULL;
    }
    if (npx > old_len) {
      size_t delta = (size_t)(npx - old_len) * sizeof(px_rgba_t);
      memset(new_fb + old_len, 0, delta);
    }
    n->fb = new_fb;
    n->fb_len = npx;
  }
  return n->fb;
}

static void ensure_state(node_t* n, int npx, const effect_params_t* params) {
  if (!n || !n->desc || !n->desc->init) {
    return;
  }
  if (npx <= 0) {
    npx = 1;
  }
  if (n->state_npx != npx) {
    if (n->state) {
      if (n->state->destroy) {
        n->state->destroy(n->state);
      }
      free(n->state);
      n->state = NULL;
    }
    n->state_npx = npx;
  }
  if (!n->state) {
    n->desc->init(&n->state, n->fb_len, params);
    if (n->state) {
      n->state_npx = npx;
    }
  }
}

static uint8_t clamp_u8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static px_rgba_t blend_px(px_rgba_t a, px_rgba_t b, blend_t mode, float opacity) {
  float ar = (float)a.r;
  float ag = (float)a.g;
  float ab = (float)a.b;
  float br = (float)b.r;
  float bg = (float)b.g;
  float bb = (float)b.b;
  float out_r = 0.f, out_g = 0.f, out_b = 0.f;
  switch (mode) {
    case BLEND_MULTIPLY:
      out_r = (ar / 255.f) * br;
      out_g = (ag / 255.f) * bg;
      out_b = (ab / 255.f) * bb;
      break;
    case BLEND_SCREEN:
      out_r = 255.f - (1.f - ar / 255.f) * (255.f - br);
      out_g = 255.f - (1.f - ag / 255.f) * (255.f - bg);
      out_b = 255.f - (1.f - ab / 255.f) * (255.f - bb);
      break;
    case BLEND_ALPHA:
      out_r = (1.f - opacity) * ar + opacity * br;
      out_g = (1.f - opacity) * ag + opacity * bg;
      out_b = (1.f - opacity) * ab + opacity * bb;
      break;
    case BLEND_ADD:
    default:
      out_r = ar + br;
      out_g = ag + bg;
      out_b = ab + bb;
      break;
  }
  px_rgba_t out;
  out.r = clamp_u8((int)(out_r + 0.5f));
  out.g = clamp_u8((int)(out_g + 0.5f));
  out.b = clamp_u8((int)(out_b + 0.5f));
  out.w = 0;
  return out;
}

static int load_graph_file(const char* path) {
  FILE* f = fopen(path, "r");
  if (!f) {
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  char* buf = (char*)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';

  cJSON* root = cJSON_Parse(buf);
  free(buf);
  if (!root) {
    return -1;
  }
  cJSON* nodes = cJSON_GetObjectItem(root, "nodes");
  if (!cJSON_IsArray(nodes)) {
    cJSON_Delete(root);
    return -1;
  }

  free_all_nodes();
  int count = cJSON_GetArraySize(nodes);
  if (count <= 0) {
    cJSON_Delete(root);
    return -1;
  }
  s_nodes = (node_t*)calloc(count, sizeof(node_t));
  if (!s_nodes) {
    cJSON_Delete(root);
    return -1;
  }
  s_node_count = count;

  int idx = 0;
  cJSON* it = NULL;
  cJSON_ArrayForEach(it, nodes) {
    node_t* n = &s_nodes[idx++];
    memset(n, 0, sizeof(*n));
    const cJSON* jtype = cJSON_GetObjectItem(it, "type");
    if (!cJSON_IsString(jtype)) {
      continue;
    }
    if (strcmp(jtype->valuestring, "src") == 0) {
      n->type = NODE_SRC;
      const cJSON* je = cJSON_GetObjectItem(it, "effect");
      if (cJSON_IsString(je)) {
        strncpy(n->data.src.effect, je->valuestring, sizeof(n->data.src.effect) - 1);
        n->desc = led_fx_get_by_name(n->data.src.effect);
      }
      n->data.src.speed = cJSON_GetObjectItem(it, "speed") ? (float)cJSON_GetObjectItem(it, "speed")->valuedouble : 1.0f;
      n->data.src.intensity = cJSON_GetObjectItem(it, "intensity") ? (float)cJSON_GetObjectItem(it, "intensity")->valuedouble : 0.5f;
      n->data.src.brightness = cJSON_GetObjectItem(it, "brightness") ? (float)cJSON_GetObjectItem(it, "brightness")->valuedouble : 1.0f;
      n->data.src.phase = cJSON_GetObjectItem(it, "phase") ? (float)cJSON_GetObjectItem(it, "phase")->valuedouble : 0.f;
    } else if (strcmp(jtype->valuestring, "blend") == 0) {
      n->type = NODE_BLEND;
      n->data.blend.a = cJSON_GetObjectItem(it, "a") ? cJSON_GetObjectItem(it, "a")->valueint : 0;
      n->data.blend.b = cJSON_GetObjectItem(it, "b") ? cJSON_GetObjectItem(it, "b")->valueint : 0;
      n->data.blend.mode = (blend_t)(cJSON_GetObjectItem(it, "mode") ? cJSON_GetObjectItem(it, "mode")->valueint : 0);
      n->data.blend.opacity = cJSON_GetObjectItem(it, "opacity") ? (float)cJSON_GetObjectItem(it, "opacity")->valuedouble : 1.0f;
    } else if (strcmp(jtype->valuestring, "out") == 0) {
      n->type = NODE_OUT;
      n->data.out.node_idx = cJSON_GetObjectItem(it, "node") ? cJSON_GetObjectItem(it, "node")->valueint : 0;
      n->data.out.channel = cJSON_GetObjectItem(it, "channel") ? cJSON_GetObjectItem(it, "channel")->valueint : 0;
    }
  }

  cJSON_Delete(root);
  return s_node_count;
}

void graph_init(void) {
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "graph.json")) {
    s_ready = false;
    return;
  }
  s_ready = load_graph_file(path) > 0;
  if (s_ready) {
    ESP_LOGI(TAG, "graph loaded with %d nodes", s_node_count);
  }
}

void graph_reload(void) {
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "graph.json")) {
    s_ready = false;
    return;
  }
  s_ready = load_graph_file(path) > 0;
}

bool graph_enabled(void) {
  return s_ready;
}

static void render_src(node_t* n, int npx, uint32_t now_ms, int channel, float channel_brightness) {
  if (!n) {
    return;
  }
  px_rgba_t* fb = ensure_fb(n, npx);
  if (!fb) {
    return;
  }
  if (!n->desc || !n->desc->render) {
    memset(fb, 0, (size_t)npx * sizeof(px_rgba_t));
    return;
  }
  float brightness = n->data.src.brightness * channel_brightness;
  if (brightness < 0.f) brightness = 0.f;
  if (brightness > 1.f) brightness = 1.f;
  effect_params_t params = {
    .speed = n->data.src.speed,
    .intensity = n->data.src.intensity,
    .color = 0xFFFFFFu,
    .palette_id = 0,
    .brightness = brightness
  };
  ensure_state(n, npx, &params);
  if (n->state) {
    fx_registry_state_set_channel(n->state, channel);
  }
  if (n->state) {
    uint32_t render_time = now_ms;
    if (n->data.src.phase != 0.f) {
      int32_t offset_ms = (int32_t)(n->data.src.phase * 1000.f);
      int64_t combined = (int64_t)(int32_t)now_ms + offset_ms;
      if (combined < 0) {
        combined = 0;
      }
      render_time = (uint32_t)combined;
    }
    n->desc->render(n->state, render_time, &params, fb, npx);
  } else {
    memset(fb, 0, (size_t)npx * sizeof(px_rgba_t));
  }
}

static void render_blend(node_t* n, int npx) {
  if (!n) {
    return;
  }
  px_rgba_t* fb = ensure_fb(n, npx);
  if (!fb) {
    return;
  }
  memset(fb, 0, (size_t)npx * sizeof(px_rgba_t));
  if (n->data.blend.a < 0 || n->data.blend.a >= s_node_count) {
    return;
  }
  if (n->data.blend.b < 0 || n->data.blend.b >= s_node_count) {
    return;
  }
  px_rgba_t* A = s_nodes[n->data.blend.a].fb;
  px_rgba_t* B = s_nodes[n->data.blend.b].fb;
  int lenA = s_nodes[n->data.blend.a].fb_len;
  int lenB = s_nodes[n->data.blend.b].fb_len;
  if (!A && !B) {
    return;
  }
  float opacity = n->data.blend.opacity;
  if (opacity < 0.f) opacity = 0.f;
  if (opacity > 1.f) opacity = 1.f;
  for (int i = 0; i < npx; ++i) {
    px_rgba_t a = (A && i < lenA) ? A[i] : (px_rgba_t){0};
    px_rgba_t b = (B && i < lenB) ? B[i] : (px_rgba_t){0};
    fb[i] = blend_px(a, b, n->data.blend.mode, opacity);
  }
}

bool graph_render_channel(int ch, render_ctx_t* ctx) {
  if (!s_ready || !ctx || !ctx->framebuf) {
    return false;
  }
  int npx = ctx->count;
  if (npx <= 0) {
    return false;
  }

  bool handled = false;
  for (int i = 0; i < s_node_count; ++i) {
    node_t* n = &s_nodes[i];
    switch (n->type) {
      case NODE_SRC:
        render_src(n, npx, ctx->now_ms, ch, ctx->brightness);
        break;
      case NODE_BLEND:
        render_blend(n, npx);
        break;
      case NODE_OUT:
        if (n->data.out.channel == ch) {
          if (n->data.out.node_idx >= 0 && n->data.out.node_idx < s_node_count) {
            px_rgba_t* src = s_nodes[n->data.out.node_idx].fb;
            if (src) {
              memcpy(ctx->framebuf, src, (size_t)npx * sizeof(px_rgba_t));
              handled = true;
            }
          }
        }
        break;
      default:
        break;
    }
  }
  return handled;
}
