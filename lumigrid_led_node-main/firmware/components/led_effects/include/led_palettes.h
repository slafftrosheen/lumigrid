#pragma once
#include <stdint.h>
#include "effects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
// 16 anchors (RGB); sampled as a 256-step gradient
uint8_t r[16], g[16], b[16];
uint8_t count; // used anchors (<=16)
} palette16_t;

// Built-ins: ID 0 means "disabled / effect decides"
enum {
PALETTE_NONE = 0,
PALETTE_HEAT = 1,
PALETTE_OCEAN = 2,
PALETTE_PARTY = 3,
PALETTE_AURORA = 4,
PALETTE_SUNSET = 5,
};

const palette16_t* palette_get(uint8_t palette_id);

// t: 0..255; returns RGB in out
static inline void palette_sample_u8(const palette16_t* p, uint8_t t, uint8_t* r, uint8_t* g, uint8_t* b) {
if (!p || p->count == 0) { *r = *g = *b = 0; return; }
if (p->count == 1) { *r = p->r[0]; *g = p->g[0]; *b = p->b[0]; return; }
// scale t across (count-1) segments
uint32_t segs = p->count - 1;
uint32_t pos = (uint32_t)t * segs; // 0..(255*(count-1))
uint8_t i = (uint8_t)(pos >> 8); // segment index
uint8_t f = (uint8_t)(pos & 0xFF);// 0..255 within segment
uint8_t ar = p->r[i], ag = p->g[i], ab = p->b[i];
uint8_t br = p->r[i+1], bg = p->g[i+1], bb = p->b[i+1];
// lerp8
*r = (uint8_t)(ar + (((uint16_t)(br - ar) * f) >> 8));
*g = (uint8_t)(ag + (((uint16_t)(bg - ag) * f) >> 8));
*b = (uint8_t)(ab + (((uint16_t)(bb - ab) * f) >> 8));
}

#ifdef __cplusplus
}
#endif
