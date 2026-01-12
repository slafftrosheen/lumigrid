#include "task_effect_engine.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "aled_rmt.h"
#include "board_pinmap.h"
#include "effects.h"
#include "effects_registry.h"
#include "effect_pixels.h"
#include "gamma.h"
#include "fx_playlist.h"
#include "sync.h"
#include "audio_reactive.h"
#include "graph.h"
#include "power.h"

#define CH_MAX                   4
#define DEFAULT_PIXELS          120
#define DEFAULT_FRAME_INTERVAL   16U

typedef struct {
  px_rgba_t           *fb;
  int                  npx;
  const effect_desc_t *fx_curr;
  effect_state_t      *fx_state_curr;
  effect_params_t      fx_params_curr;

  // Transition (next)
  const effect_desc_t *fx_next;
  effect_state_t      *fx_state_next;
  effect_params_t      fx_params_next;
  bool transitioning;
  uint32_t tr_start_ms;
  uint32_t tr_dur_ms;
  px_rgba_t *scratch; // npx sized, for next effect frame

  float                brightness;
  gamma_ctx_t          gamma_ctx;
  float                gamma_brightness;
  led_type_t           type;
  color_order_t        order;
  bool                 rmt_ready;
  bool                 blackout;
  float                saved_brightness;
} channel_ctx_t;

static channel_ctx_t g_ch[CH_MAX];
static StaticTask_t s_task_buf;
static StackType_t  s_task_stack[4096];
static const char  *TAG = "EFFECT_ENGINE";

static inline float clamp01(float v){ return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static inline uint8_t u8_lerp(uint8_t a, uint8_t b, uint8_t t){ return (uint8_t)(a + (((uint16_t)(b-a)*t)>>8)); }
static void blend_additive(px_rgba_t *dst, const px_rgba_t *a, const px_rgba_t *b, uint8_t t, int n){
 // linear crossfade: dst = lerp(a,b,t)
 for (int i=0;i<n;++i){
 dst[i].r = u8_lerp(a[i].r, b[i].r, t);
 dst[i].g = u8_lerp(a[i].g, b[i].g, t);
 dst[i].b = u8_lerp(a[i].b, b[i].b, t);
 dst[i].w = 0;
 }
}

static void free_state(effect_state_t **st_ptr){
  if (!st_ptr || !*st_ptr){
    return;
  }
  if ((*st_ptr)->destroy){
    (*st_ptr)->destroy(*st_ptr);
  }
  free(*st_ptr);
  *st_ptr = NULL;
}

static void init_effect(const effect_desc_t* fx, effect_state_t **st, int npx, const effect_params_t *p, int channel){
  free_state(st);
  if (fx && fx->init){
    fx->init(st, npx, p);
    if (st && *st){
      fx_registry_state_set_channel(*st, channel);
    }
  }
}

static void init_channel(channel_ctx_t *c, int idx){
  memset(c, 0, sizeof(*c));
  c->npx = DEFAULT_PIXELS;
  c->brightness = 1.0f;
  c->gamma_brightness = -1.0f;
  c->type = LED_WS2812B;
  c->order = ORDER_GRB;
  c->blackout = false;
  c->saved_brightness = 1.0f;
  c->fb = heap_caps_calloc(c->npx, sizeof(px_rgba_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!c->fb){
    ESP_LOGE(TAG, "ch%d framebuffer alloc failed", idx);
  }
  c->scratch = heap_caps_calloc(DEFAULT_PIXELS, sizeof(px_rgba_t), MALLOC_CAP_DEFAULT);
  gamma_build(&c->gamma_ctx, 2.2f, c->brightness);
  c->gamma_brightness = c->brightness;
  if (idx < (int)(sizeof(ALED_GPIO)/sizeof(ALED_GPIO[0]))){
    if (aled_rmt_init_chan(idx, ALED_GPIO[idx]) == ESP_OK){
      c->rmt_ready = true;
    } else {
      ESP_LOGW(TAG, "RMT init failed for channel %d", idx);
    }
  }
  const effect_desc_t *fx = led_fx_get_by_name("rainbow_flow");
  if (fx){
    c->fx_curr = fx;
    c->fx_params_curr = (effect_params_t){ .speed = 1.0f, .intensity = 0.5f, .color = 0xFFA000, .palette_id = 0, .brightness = c->brightness };
    init_effect(c->fx_curr, &c->fx_state_curr, c->npx, &c->fx_params_curr, idx);
  }
}

static void render_channel(channel_ctx_t *c, int idx, uint32_t now_ms){
  if (!c->fb || c->npx <= 0) {
    return;
  }

  float brightness_target = clamp01(c->blackout ? 0.f : c->brightness);
  render_ctx_t ctx = {
    .framebuf = c->fb,
    .count = c->npx,
    .channel = idx,
    .now_ms = now_ms,
    .brightness = brightness_target
  };

  bool handled = graph_render_channel(idx, &ctx);

  if (!handled) {
    if (!c->fx_curr || !c->fx_curr->render) {
      return;
    }
    c->fx_params_curr.brightness = brightness_target;
    c->fx_curr->render(c->fx_state_curr, now_ms, &c->fx_params_curr, c->fb, c->npx);

    if (c->transitioning && c->fx_next && c->fx_next->render && c->scratch){
      c->fx_params_next.brightness = brightness_target;
      c->fx_next->render(c->fx_state_next, now_ms, &c->fx_params_next, c->scratch, c->npx);
      uint32_t elapsed = now_ms - c->tr_start_ms;
      uint8_t t = (c->tr_dur_ms==0) ? 255 : (uint8_t)((elapsed >= c->tr_dur_ms) ? 255 : (elapsed * 255u / c->tr_dur_ms));
      blend_additive(c->fb, c->fb, c->scratch, t, c->npx);
      if (t == 255){
        free_state(&c->fx_state_curr);
        c->fx_curr = c->fx_next;
        c->fx_state_curr = c->fx_state_next;
        c->fx_params_curr= c->fx_params_next;
        c->fx_next = NULL; c->fx_state_next=NULL;
        c->transitioning=false;
      }
    }
  }

  if (fabsf(brightness_target - c->gamma_brightness) > 0.001f){
    gamma_build(&c->gamma_ctx, 2.2f, brightness_target);
    c->gamma_brightness = brightness_target;
  }
  gamma_apply(&c->gamma_ctx, c->fb, c->npx);

  if (c->npx > 0) {
    uint8_t avg = ep_estimate_avg_brightness(c->fb, c->npx);
    int estimate = power_estimate_ma(c->npx, avg);
    float scale = power_scale_for_channel(idx, estimate);
    if (scale < 0.999f) {
      ep_scale_rgb8(c->fb, c->npx, scale);
    }
  }

  if (c->rmt_ready && c->npx > 0){
    aled_rmt_write(idx, c->fb, c->npx, c->type, c->order);
  }
}

static void effect_task(void *arg){
  (void)arg;
  ESP_LOGI(TAG, "effect engine task start");
  for (int i = 0; i < CH_MAX; ++i){
    init_channel(&g_ch[i], i);
  }
  const TickType_t period = pdMS_TO_TICKS(DEFAULT_FRAME_INTERVAL);
  while (1){
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    sync_tick(now_ms);
    ar_tick(now_ms);
    fx_playlist_tick(now_ms);
    power_on_frame_begin();
    for (int ch = 0; ch < CH_MAX; ++ch){
      render_channel(&g_ch[ch], ch, now_ms);
    }
    vTaskDelay(period);
  }
}

void task_effect_engine_start(void){
  static bool started = false;
  if (started){
    return;
  }
  started = true;
  xTaskCreateStatic(effect_task, "fx", sizeof(s_task_stack)/sizeof(StackType_t), NULL, 5, s_task_stack, &s_task_buf);
}

void effect_engine_set_fx_params(int ch, const effect_desc_t *fx, const effect_params_t *p){
  if (ch < 0 || ch >= CH_MAX){
    return;
  }
  channel_ctx_t *c = &g_ch[ch];
  if (p){
    c->fx_params_curr = *p;
  }
  c->fx_curr = fx;
  init_effect(c->fx_curr, &c->fx_state_curr, c->npx, &c->fx_params_curr, ch);
}

void effect_engine_set_fx(int ch, const effect_desc_t *fx){
  effect_engine_set_fx_params(ch, fx, NULL);
}

void effect_engine_set_params(int ch, const effect_params_t *p){
  if (ch < 0 || ch >= CH_MAX || !p){
    return;
  }
  channel_ctx_t *c = &g_ch[ch];
  c->fx_params_curr = *p;
  init_effect(c->fx_curr, &c->fx_state_curr, c->npx, &c->fx_params_curr, ch);
}

void effect_engine_transition_to(int ch, const effect_desc_t *fx, uint32_t dur_ms, const effect_params_t *p){
  if (ch < 0 || ch >= CH_MAX || !fx) return;
  channel_ctx_t *c = &g_ch[ch];
  if (c->transitioning && c->fx_state_next) {
    free_state(&c->fx_state_next);
  }
  c->fx_next = fx;
  c->fx_params_next = p ? *p : c->fx_params_curr;
  init_effect(c->fx_next, &c->fx_state_next, c->npx, &c->fx_params_next, ch);
  c->transitioning = true;
  c->tr_start_ms = (uint32_t)(esp_timer_get_time()/1000ULL);
  c->tr_dur_ms = dur_ms;
}

void effect_engine_set_brightness(int ch, float brightness){
  if (ch < 0 || ch >= CH_MAX){
    return;
  }
  channel_ctx_t *c = &g_ch[ch];
  float b = clamp01(brightness);
  if (c->blackout) {
    c->saved_brightness = b;
  } else {
    c->brightness = b;
    c->saved_brightness = b;
  }
}

const effect_desc_t* effect_engine_get_fx(int ch){
  if (ch < 0 || ch >= CH_MAX){
    return NULL;
  }
  return g_ch[ch].fx_curr;
}

effect_params_t effect_engine_get_params(int ch){
  if (ch < 0 || ch >= CH_MAX){
    return (effect_params_t){0};
  }
  return g_ch[ch].fx_params_curr;
}

float effect_engine_get_brightness(int ch){
  if (ch < 0 || ch >= CH_MAX){
    return 0.f;
  }
  channel_ctx_t *c = &g_ch[ch];
  return c->blackout ? 0.f : c->brightness;
}

void effect_engine_set_speed(int ch, float speed){
  if (ch < 0 || ch >= CH_MAX){
    return;
  }
  if (speed < 0.f) speed = 0.f;
  channel_ctx_t *c = &g_ch[ch];
  c->fx_params_curr.speed = speed;
  if (c->fx_curr){
    init_effect(c->fx_curr, &c->fx_state_curr, c->npx, &c->fx_params_curr, ch);
  }
}

void effect_engine_set_intensity(int ch, float intensity){
  if (ch < 0 || ch >= CH_MAX){
    return;
  }
  channel_ctx_t *c = &g_ch[ch];
  c->fx_params_curr.intensity = clamp01(intensity);
  if (c->fx_curr){
    init_effect(c->fx_curr, &c->fx_state_curr, c->npx, &c->fx_params_curr, ch);
  }
}

void effect_engine_blackout(int ch, bool enable){
  if (ch < 0 || ch >= CH_MAX){
    return;
  }
  channel_ctx_t *c = &g_ch[ch];
  if (enable){
    if (!c->blackout){
      c->saved_brightness = c->brightness;
      c->blackout = true;
    }
  } else {
    if (c->blackout){
      c->blackout = false;
      c->brightness = c->saved_brightness;
    }
  }
}
