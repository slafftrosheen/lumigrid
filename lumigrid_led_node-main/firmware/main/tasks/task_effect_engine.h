#pragma once

#include "effects.h"

#ifdef __cplusplus
extern "C" {
#endif

void task_effect_engine_start(void);
void effect_engine_set_fx_params(int ch, const effect_desc_t *fx, const effect_params_t *p);
void effect_engine_set_fx(int ch, const effect_desc_t *fx);
void effect_engine_set_params(int ch, const effect_params_t *p);
void effect_engine_transition_to(int ch, const effect_desc_t *fx, uint32_t dur_ms, const effect_params_t *p);
void effect_engine_set_brightness(int ch, float brightness);
const effect_desc_t* effect_engine_get_fx(int ch);
effect_params_t effect_engine_get_params(int ch);
float effect_engine_get_brightness(int ch);
void effect_engine_set_speed(int ch, float speed);
void effect_engine_set_intensity(int ch, float intensity);
void effect_engine_blackout(int ch, bool enable);

#ifdef __cplusplus
}
#endif
