#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t r, g, b;
  uint8_t pos; // 0..255 along gradient
} pal_stop_t;

typedef struct {
  char      name[24];
  uint8_t   count;
  pal_stop_t stops[16];
  uint32_t  lut[256]; // ARGB8888 cache
  bool      lut_ready;
} palette_t;

// lifecycle
void palettes_init(void);
int  palettes_reload_dir(const char* dir);
int  palettes_count(void);
const palette_t* palettes_get(int idx);
const palette_t* palettes_find(const char* name);

// sampling
uint32_t palette_sample_argb(const palette_t* p, float t01);

// selection for effects engine (global current)
void palettes_set_active(const char* name);
const palette_t* palettes_active(void);

#ifdef __cplusplus
}
#endif
