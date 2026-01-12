#include "aled_state.h"

#include <string.h>

#ifndef LUMI_ALED_MAX
#define LUMI_ALED_MAX 8
#endif

#ifndef LUMI_ALED_FB_PIXELS
#define LUMI_ALED_FB_PIXELS 256
#endif

static px_rgba_t s_fb[LUMI_ALED_MAX][LUMI_ALED_FB_PIXELS];
aled_channel_t aled_ch[LUMI_ALED_MAX];
static int s_aled_count = 0;

int aled_count(void){
    return s_aled_count;
}

void aled_state_init_defaults(int channels, uint16_t pixels_per_channel){
    if (channels <= 0) channels = LUMI_ALED_MAX;
    if (channels > LUMI_ALED_MAX) channels = LUMI_ALED_MAX;
    if (pixels_per_channel == 0 || pixels_per_channel > LUMI_ALED_FB_PIXELS){
        pixels_per_channel = LUMI_ALED_FB_PIXELS;
    }

    s_aled_count = channels;
    for (int i = 0; i < LUMI_ALED_MAX; ++i){
        memset(s_fb[i], 0, sizeof(s_fb[i]));
        if (i < s_aled_count){
            aled_ch[i].ch = i;
            aled_ch[i].type = LED_WS2812B;
            aled_ch[i].order = ORDER_GRB;
            aled_ch[i].n_pixels = pixels_per_channel;
            aled_ch[i].gamma = 2.2f;
            aled_ch[i].max_brightness = 255;
            aled_ch[i].framebuf = s_fb[i];
        } else {
            aled_ch[i] = (aled_channel_t){0};
            aled_ch[i].framebuf = s_fb[i];
        }
    }
}
