#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "effects.h"
#include "led_palettes.h"
#include "sync.h"
#include "audio_reactive.h"

typedef struct {
  effect_state_t base;
} rainbow_state_t;

typedef struct {
  effect_state_t base;
  uint32_t seed;
} twinkle_state_t;

typedef struct {
  effect_state_t base;
  int head;
} meteor_state_t;

typedef struct {
  effect_state_t base;
  int heat_len;
  uint8_t *heat;
} fire_state_t;

static void fire_destroy(effect_state_t *base){
  fire_state_t *state = (fire_state_t *)base;
  if (!state){
    return;
  }
  free(state->heat);
  state->heat = NULL;
}

// ===== Utilities
static inline uint8_t qadd8(uint8_t a, uint8_t b){ uint16_t s = a + b; return (s > 255) ? 255 : (uint8_t)s; }
static inline uint8_t qsub8(uint8_t a, uint8_t b){ return (a > b) ? (a - b) : 0; }
static inline void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b){
  // simple HSV to RGB (s, v 0..255)
  if (s == 0) { *r = *g = *b = v; return; }
  uint8_t region = h / 43;
  uint8_t remainder = (h - (region * 43)) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
  switch (region) {
    default:
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    case 5: *r = v; *g = p; *b = q; break;
  }
}

// ===== RAINBOW_FLOW
static void rf_init(effect_state_t **st, int npx, const effect_params_t *p){
  (void)npx; (void)p;
  rainbow_state_t *state = (rainbow_state_t *)calloc(1, sizeof(rainbow_state_t));
  if (!state){
    *st = NULL;
    return;
  }
  state->base.destroy = NULL;
  *st = &state->base;
}
static void rf_render(effect_state_t *st, uint32_t now, const effect_params_t *p, px_rgba_t *fb, int npx){
  (void)st;
  if (npx <= 0){
    return;
  }
  float spd = (p && p->speed > 0.0f) ? p->speed : 1.0f;
  float cycle_ms = 8.0f / spd;
  if (cycle_ms < 1.0f) {
    cycle_ms = 1.0f;
  }
  uint32_t step = (uint32_t)cycle_ms;
  if (step == 0) {
    step = 1;
  }
  uint8_t base_h = (uint8_t)((now / step) & 0xFF);
  // snap hue head a little on each beat
  float beat = sync_phase_01(now); // 0..1
  base_h += (uint8_t)(beat * 16);
  const palette16_t* pal = (p && p->palette_id) ? palette_get(p->palette_id) : NULL;
  for (int i = 0; i < npx; ++i){
    uint8_t t = base_h + (i * 256 / npx);
    uint8_t r,g,b;
    if (pal) { palette_sample_u8(pal, t, &r,&g,&b); }
    else { hsv2rgb(t, 255, 255, &r,&g,&b); }
    fb[i].r = r; fb[i].g = g; fb[i].b = b; fb[i].w = 0;
  }
}
const effect_desc_t FX_RAINBOW_FLOW = { .name="rainbow_flow", .init=rf_init, .render=rf_render };

// ===== TWINKLE
static uint8_t prng8(uint32_t *s){ *s = *s * 1664525u + 1013904223u; return (uint8_t)(*s >> 24); }

static void tw_init(effect_state_t **st, int npx, const effect_params_t *p){
  (void)npx; (void)p;
  twinkle_state_t *state = (twinkle_state_t *)calloc(1, sizeof(twinkle_state_t));
  if (!state){
    *st = NULL;
    return;
  }
  state->base.destroy = NULL;
  state->seed = 0x1234abcd;
  *st = &state->base;
}
static void tw_render(effect_state_t *st, uint32_t now, const effect_params_t *p, px_rgba_t *fb, int npx){
  (void)now;
  twinkle_state_t *state = (twinkle_state_t *)st;
  if (!state || !fb || npx <= 0){
    return;
  }
  float dens = (p ? p->intensity : 0.25f); if (dens < 0) dens = 0; if (dens > 1) dens = 1;
  // fade existing
  for (int i = 0; i < npx; ++i){
    fb[i].r = qsub8(fb[i].r, 8);
    fb[i].g = qsub8(fb[i].g, 8);
    fb[i].b = qsub8(fb[i].b, 8);
    fb[i].w = 0;
  }
  // spawn sparkles
  int spawns = (int)(dens * (float)npx * 0.05f) + 1;
  // small beat pop
  if (sync_phase_01(now) < 0.08f) spawns += 2;
  const palette16_t* pal = (p && p->palette_id) ? palette_get(p->palette_id) : NULL;
  for (int k = 0; k < spawns; ++k){
    int idx = prng8(&state->seed) % npx;
    uint8_t r,g,b;
    if (pal) { palette_sample_u8(pal, prng8(&state->seed), &r,&g,&b); }
    else { r = prng8(&state->seed); g = prng8(&state->seed); b = prng8(&state->seed); }
    fb[idx].r = qadd8(fb[idx].r, r);
    fb[idx].g = qadd8(fb[idx].g, g);
    fb[idx].b = qadd8(fb[idx].b, b);
  }
}
const effect_desc_t FX_TWINKLE = { .name="twinkle", .init=tw_init, .render=tw_render };

// ===== METEOR
static void mt_init(effect_state_t **st, int npx, const effect_params_t *p){
  (void)npx; (void)p;
  meteor_state_t *state = (meteor_state_t *)calloc(1, sizeof(meteor_state_t));
  if (!state){
    *st = NULL;
    return;
  }
  state->base.destroy = NULL;
  state->head = 0;
  *st = &state->base;
}
static void mt_render(effect_state_t *st, uint32_t now, const effect_params_t *p, px_rgba_t *fb, int npx){
  meteor_state_t *state = (meteor_state_t *)st;
  if (!state || !fb || npx <= 0){
    return;
  }
  ar_state_t ar = ar_get();
  float boost = (ar.enabled ? (0.4f + 0.6f*(ar.level/255.0f)) : 1.0f);
  int tail = (int)((p->intensity * boost) * 10 + 4);

  // trail decay
  for (int i = 0; i < npx; ++i){
    fb[i].r = (uint8_t)((fb[i].r * 200) / 255);
    fb[i].g = (uint8_t)((fb[i].g * 200) / 255);
    fb[i].b = (uint8_t)((fb[i].b * 200) / 255);
  }
  // head color
  uint8_t r,g,b;
  const palette16_t* pal = (p && p->palette_id) ? palette_get(p->palette_id) : NULL;
  if (pal) { palette_sample_u8(pal, (uint8_t)(now >> 3), &r,&g,&b); }
  else { r = ((p && p->color)?(p->color>>16)&0xFF:255); g=((p&&p->color)?(p->color>>8)&0xFF:180); b=((p&&p->color)?p->color&0xFF:20); }
  int idx = state->head % npx;
  fb[idx].r = r; fb[idx].g = g; fb[idx].b = b; fb[idx].w = 0;
  float spd = (p && p->speed > 0) ? p->speed : 1.0f;
  float step_ms = 30.0f / spd;
  if (step_ms < 1.0f) {
    step_ms = 1.0f;
  }
  uint32_t advance = (uint32_t)step_ms;
  if (advance == 0) {
    advance = 1;
  }
  if (npx > 0 && (now % advance) == 0) {
    state->head = (state->head + 1) % npx;
  }
}
const effect_desc_t FX_METEOR = { .name="meteor", .init=mt_init, .render=mt_render };

// ===== FIRE2012 (classic)
static void f2_init(effect_state_t **st, int npx, const effect_params_t *p){
  (void)p;
  fire_state_t *state = (fire_state_t *)calloc(1, sizeof(fire_state_t));
  if (!state){
    *st = NULL;
    return;
  }
  state->heat_len = npx > 0 ? npx : 0;
  if (state->heat_len > 0){
    state->heat = (uint8_t *)calloc((size_t)state->heat_len, sizeof(uint8_t));
    if (!state->heat){
      free(state);
      *st = NULL;
      return;
    }
  } else {
    state->heat = NULL;
  }
  state->base.destroy = fire_destroy;
  *st = &state->base;
}
static void f2_render(effect_state_t *st, uint32_t now, const effect_params_t *p, px_rgba_t *fb, int npx){
  (void)now;
  fire_state_t *state = (fire_state_t *)st;
  if (!state || !state->heat || state->heat_len <= 0 || !fb){
    for (int i = 0; i < npx; ++i){
      fb[i].r = fb[i].g = fb[i].b = fb[i].w = 0;
    }
    return;
  }
  int N = state->heat_len < npx ? state->heat_len : npx;
  float cooling   = (p ? (1.0f - p->intensity) : 0.5f); // intensity -> hotter
  if (cooling < 0.1f) cooling = 0.1f;
  // cool
  ar_state_t ar = ar_get();
  uint8_t extra = ar.enabled ? (ar.bands[0] >> 4) : 0;
  for (int i = 0; i < N; ++i) {
    int cool = rand() % (int)(3 + (cooling * 10));
    state->heat[i] = qsub8(state->heat[i], cool + extra);
  }
  // diffuse
  for (int i = N - 1; i >= 2; --i) {
    state->heat[i] = (uint8_t)((state->heat[i - 1] + state->heat[i - 2] + state->heat[i - 2]) / 3);
  }
  // spark
  if ((rand() & 3) == 0) {
    int y = (N > 0) ? rand() % (N / 10 + 1) : 0;
    if (y < N) {
      state->heat[y] = qadd8(state->heat[y], (uint8_t)(160 + (rand() & 63)));
    }
  }
  // map to colors (heat palette or default)
  const palette16_t* pal = (p && p->palette_id) ? palette_get(p->palette_id) : NULL;
  for (int i = 0; i < N; ++i) {
    uint8_t t192 = (uint8_t)((state->heat[i] * 191) / 255);
    uint8_t heatramp = t192 & 63;
    heatramp <<= 2;
    uint8_t r, g, b;
    if (pal) {
      // sample using heat as index
      palette_sample_u8(pal, (uint8_t)((state->heat[i]*255)/255), &r,&g,&b);
    } else {
      if (t192 > 128) { r = 255; g = 255; b = heatramp; }
      else if (t192 > 64) { r = 255; g = heatramp; b = 0; }
      else { r = heatramp; g = 0; b = 0; }
    }
    fb[i].r = r; fb[i].g = g; fb[i].b = b; fb[i].w = 0;
  }
  for (int i = N; i < npx; ++i) {
    fb[i].r = fb[i].g = fb[i].b = fb[i].w = 0;
  }
}
const effect_desc_t FX_FIRE2012 = { .name="fire2012", .init=f2_init, .render=f2_render };
