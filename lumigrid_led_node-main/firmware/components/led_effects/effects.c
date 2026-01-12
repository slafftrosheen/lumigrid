#include <string.h>
#include <strings.h>
#include "effects.h"

// Forward declare registry functions if effects component is included
extern const effect_desc_t* fx_registry_get_by_name(const char* name) __attribute__((weak));
extern const effect_desc_t** fx_registry_get_list(int* count_out) __attribute__((weak));

uint8_t led_fx_u8_lerp(uint8_t a, uint8_t b, uint8_t t) {
  int32_t delta = (int32_t)b - (int32_t)a;
  return (uint8_t)(a + ((delta * (int32_t)t) / 256));
}

void led_fx_fill_rgb(px_rgba_t *fb, int npx, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < npx; ++i) { fb[i].r = r; fb[i].g = g; fb[i].b = b; fb[i].w = 0; }
}

// Forward decls for batch 2 effects (implemented there)
extern const effect_desc_t FX_RAINBOW_FLOW;
extern const effect_desc_t FX_TWINKLE;
extern const effect_desc_t FX_METEOR;
extern const effect_desc_t FX_FIRE2012;

static const effect_desc_t *s_fx[] = {
  &FX_RAINBOW_FLOW, &FX_TWINKLE, &FX_METEOR, &FX_FIRE2012,
};

#define MAX_TOTAL_FX 32
static const effect_desc_t *s_merged_list[MAX_TOTAL_FX];
static size_t s_merged_count = 0;
static bool s_merged_ready = false;

static void build_merged_list(void) {
  if (s_merged_ready) return;
  
  // Add built-in effects
  size_t n = sizeof(s_fx)/sizeof(s_fx[0]);
  for (size_t i = 0; i < n && s_merged_count < MAX_TOTAL_FX; i++) {
    s_merged_list[s_merged_count++] = s_fx[i];
  }
  
  // Add registered effects if registry exists
  if (fx_registry_get_list != NULL) {
    int reg_count = 0;
    const effect_desc_t** reg_list = fx_registry_get_list(&reg_count);
    for (int i = 0; i < reg_count && s_merged_count < MAX_TOTAL_FX; i++) {
      s_merged_list[s_merged_count++] = reg_list[i];
    }
  }
  
  s_merged_ready = true;
}

const effect_desc_t *const *led_fx_list(size_t *count_out) {
  build_merged_list();
  if (count_out) *count_out = s_merged_count;
  return s_merged_list;
}

const effect_desc_t *led_fx_get_by_name(const char *name) {
  // Check built-in effects
  for (size_t i = 0; i < sizeof(s_fx)/sizeof(s_fx[0]); ++i) {
    if (0 == strcasecmp(s_fx[i]->name, name)) return s_fx[i];
  }
  
  // Check registered effects if registry exists
  if (fx_registry_get_by_name != NULL) {
    const effect_desc_t* fx = fx_registry_get_by_name(name);
    if (fx) return fx;
  }
  
  return NULL;
}
