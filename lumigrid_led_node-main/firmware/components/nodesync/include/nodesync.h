#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nodesync_init(void);
void nodesync_set_master(bool on);
int nodesync_is_master(void);
void nodesync_broadcast_scene(const char* effect, float bpm);

// Batch 31: NodeSync v2 additions
void nodesync_tick_beat(void);            // call from your effect engine timer if you have one
float nodesync_now_phase(float* out_bpm); // 0..1 phase aligned to network

#ifdef __cplusplus
}
#endif
