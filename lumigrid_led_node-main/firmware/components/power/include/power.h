#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int max_ma_total;
  int max_ma_ch[4];
} power_conf_t;

void power_init(void);
void power_on_frame_begin(void);
float power_scale_for_channel(int ch, int est_ma);
const power_conf_t* power_cfg(void);
int power_estimate_ma(int pixels, uint8_t avg_brightness);

#ifdef __cplusplus
}
#endif
