#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "effects_types.h"

// Effect parameter block (shared schema; effects may use subsets)
typedef struct {
  float     speed;        // logical beats/sec-ish; 0.0..5.0, default 1.0
  float     intensity;    // arbitrary 0..1 control (e.g., density/heat)
  uint32_t  color;        // 0xRRGGBB base
  uint8_t   palette_id;   // reserved for future palette sets
  float     brightness;   // global brightness 0..1
} effect_params_t;

typedef struct effect_state_s {
  void (*destroy)(struct effect_state_s *state);
} effect_state_t;

typedef void (*effect_init_fn)(effect_state_t **state, int npx, const effect_params_t *p);
typedef void (*effect_render_fn)(effect_state_t *state, uint32_t now_ms, const effect_params_t *p, px_rgba_t *fb, int npx);

typedef struct {
  const char       *name;
  effect_init_fn    init;
  effect_render_fn  render;
} effect_desc_t;

// Registry
const effect_desc_t *led_fx_get_by_name(const char *name);
const effect_desc_t *const *led_fx_list(size_t *count_out);

// Helpers
uint8_t led_fx_u8_lerp(uint8_t a, uint8_t b, uint8_t t); // t: 0..255
void    led_fx_fill_rgb(px_rgba_t *fb, int npx, uint8_t r, uint8_t g, uint8_t b);
