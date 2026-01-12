#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "effects.h"

#ifdef __cplusplus
extern "C" {
#endif

// Public control API for REST/MQTT/UI layers
esp_err_t fx_set_channel_by_name(int ch, const char *name, const effect_params_t *p);
esp_err_t fx_set_channel_brightness(int ch, float brightness);
esp_err_t fx_list(const char ***names_out, size_t *count_out);
// Crossfade to new effect over duration_ms. If params==NULL, reuse current params.
esp_err_t fx_transition_to(int ch, const char *name, uint32_t duration_ms, const effect_params_t *params);
esp_err_t fx_set_speed(int ch, float speed);
esp_err_t fx_set_intensity(int ch, float intensity);
float fx_get_speed(int ch);
float fx_get_intensity(int ch);
float fx_get_brightness(int ch);
const char* fx_current_name(int ch);
size_t fx_count(void);
const char* fx_name(size_t idx);
void fx_blackout(int ch, bool enable);

#ifdef __cplusplus
}
#endif
