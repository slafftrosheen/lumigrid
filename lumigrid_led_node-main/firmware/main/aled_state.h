#pragma once

#include <stdint.h>
#include "effects.h"

#ifdef __cplusplus
extern "C" {
#endif

extern aled_channel_t aled_ch[];

int  aled_count(void);
void aled_state_init_defaults(int channels, uint16_t pixels_per_channel);

#ifdef __cplusplus
}
#endif
