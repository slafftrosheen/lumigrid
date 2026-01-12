#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "effects.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
char name[32]; // effect name
uint32_t duration_ms; // how long to run
effect_params_t params; // params for this slot
float brightness; // 0..1
} fx_slot_t;

typedef struct {
int channel; // target channel
int count;
fx_slot_t *slots;
int index;
uint32_t t_start;
bool running;
uint32_t transition_ms; // crossfade between slots
} fx_playlist_t;

// Load playlist from JSON path (LittleFS). Ownership of internal buffers kept globally.
esp_err_t fx_playlist_load(const char *path, int channel, uint32_t default_transition_ms);
void fx_playlist_start(int channel);
void fx_playlist_stop(int channel);
void fx_playlist_tick(uint32_t now_ms); // call once per frame

#ifdef __cplusplus
}
#endif
