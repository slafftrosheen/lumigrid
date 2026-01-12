#include "effects_extra.h"
#include "effects.h"
#include "effect_pixels.h"
#include "effects_registry.h"
#include "palettes.h"
#include "sync.h"
#include "audio_reactive.h"
#include <math.h>
#include <string.h>

static void fx_strobe_sync(render_ctx_t* r, const effect_params_t* p){
  const uint32_t now = r->now_ms;
  const float bpm = sync_get_bpm();
  const float period_ms = (60.0f * 1000.0f) / (bpm > 1 ? bpm : 120.0f);
  const float duty = fminf(fmaxf(p->intensity, 0.02f), 0.98f);
  const float phase = sync_phase_01(now);
  const bool on = fmodf(phase, 1.0f) < duty;
  const uint8_t v = on ? (uint8_t)(255.0f * p->brightness) : 0;
  for (int i=0;i<r->count;i++) ep_set_rgb8(r,i,v,v,v);
  (void)period_ms; // informative; keep logic pace from sync
}

static void fx_barber_pole(render_ctx_t* r, const effect_params_t* p){
  const palette_t* pal = palettes_active();
  const float stripes = fmaxf(2.0f, 6.0f * p->intensity + 2.0f);
  const float speed = p->speed * 2.0f - 1.0f;
  const float t = r->now_ms * 0.001f * speed; // -1..+1
  for (int i=0;i<r->count;i++){
    float u = ((float)i / fmaxf((float)r->count, 1.0f)) * stripes + t;
    float phase = u - floorf(u);
    uint32_t argb = pal ? palette_sample_argb(pal, phase) : 0xFF0000FFu;
    uint8_t r8 = (uint8_t)(((argb >> 16) & 0xFFu) * p->brightness);
    uint8_t g8 = (uint8_t)(((argb >> 8)  & 0xFFu) * p->brightness);
    uint8_t b8 = (uint8_t)(( argb        & 0xFFu) * p->brightness);
    ep_set_rgb8(r, i, r8, g8, b8);
  }
}

static uint32_t prng(uint32_t* s){ *s ^= *s<<13; *s ^= *s>>17; *s ^= *s<<5; return *s; }

static void fx_twinkle_dust(render_ctx_t* r, const effect_params_t* p){
  static uint8_t  level[4][2048]; // per-channel decay buffer (cap 2k px)
  static uint32_t seed[4] = {1,2,3,4};
  const int ch = r->channel;
  const int N  = r->count > 2048 ? 2048 : r->count;
  const palette_t* pal = palettes_active();
  // decay
  for (int i=0;i<N;i++){
    int v = level[ch][i];
    v -= (int)(3 + 40*(1.0f - p->intensity));
    if (v<0) v=0;
    level[ch][i] = (uint8_t)v;
  }
  // new sparks
  int sparks = (int)(2 + 40*p->intensity);
  for (int k=0;k<sparks;k++){
    int idx = prng(&seed[ch]) % N;
    int v = level[ch][idx] + (50 + (prng(&seed[ch])%206));
    if (v>255) v=255;
    level[ch][idx] = (uint8_t)v;
  }
  // render (warm white)
  for (int i=0;i<N;i++){
    float pos = (float)i / fmaxf((float)(N - 1), 1.0f);
    uint32_t argb = pal ? palette_sample_argb(pal, pos) : 0xFFFFB040u;
    uint8_t base_r = (argb >> 16) & 0xFFu;
    uint8_t base_g = (argb >> 8)  & 0xFFu;
    uint8_t base_b = argb & 0xFFu;
    float scale = (level[ch][i] / 255.0f) * p->brightness;
    uint8_t r8 = (uint8_t)(base_r * scale);
    uint8_t g8 = (uint8_t)(base_g * scale);
    uint8_t b8 = (uint8_t)(base_b * scale);
    ep_set_rgb8(r, i, r8, g8, b8);
  }
}

static void fx_vu_bars(render_ctx_t* r, const effect_params_t* p){
  ar_state_t ar = ar_get();
  uint32_t band_sum = 0; for(int i=0;i<5;i++) band_sum += ar.bands[i];
  if (band_sum==0){ ep_fill_rgb8(r,0,0,0); return; }
  const int cols = 5;
  int colw = r->count / cols; if (colw<1) colw=1;
  const palette_t* pal = palettes_active();
  for(int c=0;c<cols;c++){
    float norm = fminf(1.0f, (float)ar.bands[c] / 1024.0f);
    int lit = (int)(norm * colw);
    for (int i=0;i<colw;i++){
      int idx = c*colw + i; if (idx>=r->count) break;
      if (i<lit){
        float pos = (float)i / fmaxf((float)(colw - 1), 1.0f);
        uint32_t argb = pal ? palette_sample_argb(pal, pos) : 0xFFFF8000u;
        uint8_t r8 = (uint8_t)(((argb >> 16) & 0xFFu) * p->brightness);
        uint8_t g8 = (uint8_t)(((argb >> 8)  & 0xFFu) * p->brightness);
        uint8_t b8 = (uint8_t)(( argb        & 0xFFu) * p->brightness);
        ep_set_rgb8(r, idx, r8, g8, b8);
      }else{
        ep_set_rgb8(r,idx,0,0,0);
      }
    }
  }
}

void fx_register_extra(void){
  fx_register("strobe_sync",  fx_strobe_sync);
  fx_register("barber_pole",  fx_barber_pole);
  fx_register("twinkle_dust", fx_twinkle_dust);
  fx_register("vu_bars",      fx_vu_bars);
  
  // Register new pattern and noise effects
  fx_pattern_register_all();
  fx_noise_register_all();
}
