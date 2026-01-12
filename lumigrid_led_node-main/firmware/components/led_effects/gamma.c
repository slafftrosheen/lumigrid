#include <math.h>
#include "gamma.h"

#ifndef CLAMP01
#define CLAMP01(x) ((x) < 0 ? 0 : ((x) > 1 ? 1 : (x)))
#endif

void gamma_build(gamma_ctx_t *ctx, float gamma, float global_brightness) {
  float g = (gamma <= 0.01f) ? 2.2f : gamma;
  float s = CLAMP01(global_brightness);
  for (int i = 0; i < 256; ++i) {
    float n = (float)i / 255.0f;
    float out = powf(n, g) * s;
    int v = (int)lrintf(out * 255.0f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    ctx->lut[i] = (uint8_t)v;
  }
  ctx->dither_phase = 0;
}

void gamma_apply(gamma_ctx_t *ctx, px_rgba_t *fb, int npx) {
  // Ordered temporal dither: flip LSB on alternating frames to reduce banding.
  ctx->dither_phase ^= 1;
  for (int i = 0; i < npx; ++i) {
    uint8_t r = ctx->lut[fb[i].r];
    uint8_t g = ctx->lut[fb[i].g];
    uint8_t b = ctx->lut[fb[i].b];
    if (ctx->dither_phase) {
      if (r < 255) { ++r; }
      if (g < 255) { ++g; }
      if (b < 255) { ++b; }
    }
    fb[i].r = r; fb[i].g = g; fb[i].b = b;
  }
}
