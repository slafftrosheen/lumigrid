#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t r, g, b, w; // w optional; leave 0 for RGB strips
} px_rgba_t;

typedef enum {
  LED_WS2812B = 0,
  LED_SK6812_RGBW = 1
} led_type_t;

typedef enum {
  ORDER_GRB = 0,
  ORDER_RGB,
  ORDER_RGBW,
  ORDER_GRBW
} color_order_t;

typedef struct {
  uint8_t r, g, b;
} rgb8_t;

typedef struct {
  const char *name;
  const rgb8_t *keys;
  size_t count;
} fx_palette_ref_t;  // Renamed to avoid conflict with palettes component

typedef struct {
  int            ch;
  led_type_t     type;
  color_order_t  order;
  uint16_t       n_pixels;
  float          gamma;
  uint8_t        max_brightness;
  px_rgba_t     *framebuf;
} aled_channel_t;
