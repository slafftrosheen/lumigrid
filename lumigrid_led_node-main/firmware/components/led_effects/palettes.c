#include "led_palettes.h"

static const palette16_t PAL_HEAT = {
.r={ 0, 32,128,255}, .g={ 0, 0, 64,255}, .b={ 0, 0, 0, 64}, .count=4
};
static const palette16_t PAL_OCEAN = {
.r={ 0, 0, 0, 16, 0}, .g={ 0, 32,128,192,255}, .b={ 32,128,192,255,255}, .count=5
};
static const palette16_t PAL_PARTY = {
.r={255,255, 0,255, 0,255}, .g={ 0,255,255, 0, 0, 32}, .b={255, 0,255, 0,255,255}, .count=6
};
static const palette16_t PAL_AURORA = {
.r={ 0, 0, 16, 0, 0}, .g={ 8, 64,192,255,128}, .b={ 32, 96, 64, 32, 0}, .count=5
};
static const palette16_t PAL_SUNSET = {
.r={ 32,128,255,255}, .g={ 0, 32,128, 64}, .b={ 0, 0, 16, 0}, .count=4
};

const palette16_t* palette_get(uint8_t palette_id){
switch (palette_id){
case PALETTE_HEAT: return &PAL_HEAT;
case PALETTE_OCEAN: return &PAL_OCEAN;
case PALETTE_PARTY: return &PAL_PARTY;
case PALETTE_AURORA: return &PAL_AURORA;
case PALETTE_SUNSET: return &PAL_SUNSET;
default: return NULL;
}
}
