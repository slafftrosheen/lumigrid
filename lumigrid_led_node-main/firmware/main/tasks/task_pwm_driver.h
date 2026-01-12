#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { PWMG_RGB = 0, PWMG_RGBW = 1 } pwm_group_kind_t;

typedef struct {
  char name[32];           // group name (owned storage)
  pwm_group_kind_t kind;   // RGB or RGBW
  // Optional: channel maps if you expose them; not required by rest_api.c
  int map_r, map_g, map_b, map_w;
} pwm_group_t;

void task_pwm_driver_start(void);
void pwm_set_mode_static(uint8_t ch, float duty);
void pwm_set_mode_breath(uint8_t ch, float min_val, float max_val, float period_ms);
void pwm_set_mode_candle(uint8_t ch, float base, float flicker, uint32_t seed);
void pwm_set_mode_warmdim(uint8_t ch, float duty);

// Query groups for /api/status
int                pwm_groups_count(void);
const pwm_group_t* pwm_groups_get(int idx);

// Runtime apply after /api/config POST
void pwm_groups_init_from_config(void);

// Live control from /api/trigger action
void pwm_group_set_rgb (const char *name, float r, float g, float b);
void pwm_group_set_rgbw(const char *name, float r, float g, float b, float w);

float pwm_max_configured_duty(void);

#ifdef __cplusplus
}
#endif
