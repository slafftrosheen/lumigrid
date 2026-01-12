#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dmxi_init(int uart_num, int rx_gpio);
int  dmxi_active(void);   // 1 if DMX frames in last 2s

// Get DMX channel values
uint8_t dmxi_get_channel_value(int dmx_addr);
void dmxi_get_rgb(int dmx_addr, uint8_t* r, uint8_t* g, uint8_t* b);
void dmxi_get_rgbw(int dmx_addr, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* w);

#ifdef __cplusplus
}
#endif
