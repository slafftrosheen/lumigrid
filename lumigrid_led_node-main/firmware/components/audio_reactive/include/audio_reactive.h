#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // 0..255 overall level; bands[0..4] low..high 0..255
  uint8_t level;
  uint8_t bands[5];
  bool    enabled;
} ar_state_t;

void   ar_init(void);              // does nothing until enabled
void   ar_enable_adc(int adc_unit, int adc_channel, int sample_hz);
void   ar_enable_i2s(int i2s_port, int sample_hz);
void   ar_disable(void);
void   ar_tick(uint32_t now_ms);   // cheap RMS envelope; call each frame
ar_state_t ar_get(void);

#ifdef __cplusplus
}
#endif