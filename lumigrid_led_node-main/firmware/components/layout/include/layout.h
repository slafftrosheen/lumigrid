#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Layout-specific LED types (independent from led_effects component)
typedef enum { LAYOUT_STRIP_WS2812B=0, LAYOUT_STRIP_SK6812_RGBW=1 } strip_type_t;
typedef enum { LAYOUT_ORDER_RGB=0, LAYOUT_ORDER_GRB, LAYOUT_ORDER_BGR, LAYOUT_ORDER_GBR, LAYOUT_ORDER_RGBW, LAYOUT_ORDER_GRBW } layout_color_order_t;

typedef struct {
  uint16_t start;
  uint16_t len;
  bool     reverse;
  uint8_t  mirror;   // 0 = none, >0 mirror repeats (A|A'|A|A'...)
} segment_t;

typedef struct {
  strip_type_t       type;
  layout_color_order_t order;
  uint16_t      length;       // physical LEDs
  int8_t        pwr_pwm;      // PCA9685 channel or -1
  uint8_t       segments_count;
  segment_t     segments[12];
} channel_layout_t;

typedef struct {
  channel_layout_t ch[4];
} layout_t;

void        layout_init(void);
int         layout_load(const char* path);  // typically storage_fs_path("layout.json")
int         layout_save(const char* path);
const layout_t* layout_get(void);

// mapping: returns virtual->physical map for channel (len = layout_virtual_len)
int         layout_virtual_len(int ch);
int         layout_build_map(int ch, uint16_t* out_map, int cap);

// ===== Batch 24: Extended layout API for 8 channels =====
typedef enum { LTYPE_NONE=0, LTYPE_WS2812B=1, LTYPE_SK6812_RGBW=2, LTYPE_PWM=3 } lg_type_t;
typedef enum { LORDER_RGB=0, LORDER_GRB=1, LORDER_BGR=2, LORDER_RGBW=3, LORDER_GRBW=4, LORDER_WRGB=5 } lg_order_t;

typedef struct {
  lg_type_t  type;
  lg_order_t order;
  uint8_t    gpio;       // for addressable
  uint16_t   pixels;     // addressable pixel count
  uint8_t    pwm_r;      // for PWM fixture mapping (PCA9685 channel idx or 255)
  uint8_t    pwm_g;
  uint8_t    pwm_b;
  uint8_t    pwm_w;      // optional W
  bool       enabled;
} lg_channel_t;

const lg_channel_t* layout_get_channel(int ch);     // 0..7
int  layout_channel_count(void);                    // logical channels supported (8)
int  layout_addr_channels(void);                    // count of addressable-capable channels
bool layout_is_rgbw(int ch);                        // true if SK6812_RGBW or PWM with W

#ifdef __cplusplus
}
#endif
