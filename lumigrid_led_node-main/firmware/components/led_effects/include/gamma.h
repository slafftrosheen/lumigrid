#pragma once

#include <stdint.h>
#include "effects_types.h"

typedef struct {
  // 256-entry gamma LUT (8-bit in -> 8-bit out)
  uint8_t lut[256];
  // Frame dither phase (for ordered temporal dithering)
  uint8_t dither_phase;
} gamma_ctx_t;

// Build LUT for gamma (e.g., 2.2). brightness 0..1 scales output.
void gamma_build(gamma_ctx_t *ctx, float gamma, float global_brightness);

// Apply gamma + simple temporal dithering in-place to RGB, ignore W.
void gamma_apply(gamma_ctx_t *ctx, px_rgba_t *fb, int npx);
