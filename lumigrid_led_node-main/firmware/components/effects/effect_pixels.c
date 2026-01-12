#include "effect_pixels.h"

void ep_set_rgb8(render_ctx_t* r, int idx, uint8_t red, uint8_t green, uint8_t blue) {
  if (!r || !r->framebuf || idx < 0 || idx >= r->count) return;
  r->framebuf[idx].r = red;
  r->framebuf[idx].g = green;
  r->framebuf[idx].b = blue;
  r->framebuf[idx].w = 0;
}

void ep_set_rgba8(render_ctx_t* r, int idx, uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
  if (!r || !r->framebuf || idx < 0 || idx >= r->count) return;
  r->framebuf[idx].r = red;
  r->framebuf[idx].g = green;
  r->framebuf[idx].b = blue;
  r->framebuf[idx].w = white;
}

void ep_fill_rgb8(render_ctx_t* r, uint8_t red, uint8_t green, uint8_t blue) {
  if (!r || !r->framebuf) return;
  for (int i = 0; i < r->count; i++) {
    r->framebuf[i].r = red;
    r->framebuf[i].g = green;
    r->framebuf[i].b = blue;
    r->framebuf[i].w = 0;
  }
}

uint8_t ep_estimate_avg_brightness(const px_rgba_t* fb, int count) {
  if (!fb || count <= 0) {
    return 0;
  }
  uint32_t sum = 0;
  for (int i = 0; i < count; ++i) {
    uint8_t r = fb[i].r;
    uint8_t g = fb[i].g;
    uint8_t b = fb[i].b;
    uint8_t m = r > g ? r : g;
    if (b > m) {
      m = b;
    }
    sum += m;
  }
  sum /= (uint32_t)count;
  if (sum > 255u) {
    sum = 255u;
  }
  return (uint8_t)sum;
}

void ep_scale_rgb8(px_rgba_t* fb, int count, float scale) {
  if (!fb || count <= 0) {
    return;
  }
  if (scale < 0.f) {
    scale = 0.f;
  }
  for (int i = 0; i < count; ++i) {
    fb[i].r = (uint8_t)(fb[i].r * scale);
    fb[i].g = (uint8_t)(fb[i].g * scale);
    fb[i].b = (uint8_t)(fb[i].b * scale);
  }
}
