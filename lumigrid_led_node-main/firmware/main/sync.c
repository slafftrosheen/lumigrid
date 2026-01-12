#include "sync.h"
#include <math.h>

static sync_clock_t g_clk = { .bpm = 120.0f, .last_pulse_ms = 0, .phase_reset_ms = 0, .running = true };

void sync_init(void) { }
void sync_set_bpm(float bpm) { if (bpm > 1.0f && bpm < 400.0f) g_clk.bpm = bpm; }
float sync_get_bpm(void) { return g_clk.bpm; }
void sync_start(void) { g_clk.running = true; }
void sync_stop(void)  { g_clk.running = false; }
void sync_pulse(uint32_t now_ms) { g_clk.phase_reset_ms = now_ms; g_clk.last_pulse_ms = now_ms; }

void sync_tick(uint32_t now_ms) {
  (void)now_ms; // reserved for future drift correction
}

static inline float fracf(float x){ return x - floorf(x); }

float sync_phase_01(uint32_t now_ms){
  if (!g_clk.running) return 0.0f;
  const float ms_per_qn = 60000.0f / (g_clk.bpm > 0.1f ? g_clk.bpm : 120.0f); // quarter note
  float dt = (float)((now_ms - g_clk.phase_reset_ms));
  return fracf(dt / ms_per_qn);
}

float sync_phase_quantized(uint32_t now_ms, int steps){
  float p = sync_phase_01(now_ms);
  if (steps <= 1) return p;
  float s = floorf(p * steps + 0.5f) / (float)steps;
  return fmodf(s, 1.0f);
}