#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float     bpm;          // beats per minute
  uint32_t  last_pulse_ms;
  uint32_t  phase_reset_ms; // for phase snaps
  bool      running;
} sync_clock_t;

void     sync_init(void);
void     sync_set_bpm(float bpm);
float    sync_get_bpm(void);
void     sync_start(void);
void     sync_stop(void);
// Trigger a beat pulse (e.g., from UI or audio detector)
void     sync_pulse(uint32_t now_ms);
// Advance internal state; call every frame
void     sync_tick(uint32_t now_ms);

// [0..1) beat phase, wraps every quarter note
float    sync_phase_01(uint32_t now_ms);
// handy quantization: returns phase snapped to N steps (e.g. 16)
float    sync_phase_quantized(uint32_t now_ms, int steps);

#ifdef __cplusplus
}
#endif