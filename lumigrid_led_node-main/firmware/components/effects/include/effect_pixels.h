#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "effects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Render context passed to new-style effects
typedef struct {
  px_rgba_t    *framebuf;   // output buffer
  int           count;       // pixel count
  int           channel;     // 0..3 channel index
  uint32_t      now_ms;      // current timestamp
  float         brightness;  // global brightness 0..1
} render_ctx_t;

// Pixel manipulation helpers
void ep_set_rgb8(render_ctx_t* r, int idx, uint8_t red, uint8_t green, uint8_t blue);
void ep_set_rgba8(render_ctx_t* r, int idx, uint8_t red, uint8_t green, uint8_t blue, uint8_t white);
void ep_fill_rgb8(render_ctx_t* r, uint8_t red, uint8_t green, uint8_t blue);
uint8_t ep_estimate_avg_brightness(const px_rgba_t* fb, int count);
void ep_scale_rgb8(px_rgba_t* fb, int count, float scale);

#ifdef __cplusplus
}
#endif
