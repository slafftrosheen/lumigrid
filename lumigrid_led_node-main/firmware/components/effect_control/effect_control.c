#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "effects.h"
#include "effect_control.h"

// Provided by the effect engine task (main component)
extern void effect_engine_set_fx_params(int ch, const effect_desc_t *fx, const effect_params_t *p);
extern void effect_engine_set_brightness(int ch, float brightness);
extern void effect_engine_transition_to(int ch, const effect_desc_t *fx, uint32_t dur_ms, const effect_params_t *p);
extern const effect_desc_t* effect_engine_get_fx(int ch);
extern effect_params_t effect_engine_get_params(int ch);
extern float effect_engine_get_brightness(int ch);
extern void effect_engine_set_speed(int ch, float speed);
extern void effect_engine_set_intensity(int ch, float intensity);
extern void effect_engine_blackout(int ch, bool enable);

static effect_params_t fx_default_params(void)
{
  effect_params_t params = {
    .speed = 1.0f,
    .intensity = 0.5f,
    .color = 0xFFFFFFu,
    .palette_id = 0,
    .brightness = 1.0f,
  };
  return params;
}

esp_err_t fx_set_channel_by_name(int ch, const char *name, const effect_params_t *p)
{
  const effect_desc_t *fx = led_fx_get_by_name(name ? name : "");
  if (!fx) return ESP_ERR_NOT_FOUND;
  effect_params_t params = p ? *p : fx_default_params();
  effect_engine_set_fx_params(ch, fx, &params);
  return ESP_OK;
}

esp_err_t fx_set_channel_brightness(int ch, float b)
{
  if (b < 0) b = 0;
  if (b > 1) b = 1;
  effect_engine_set_brightness(ch, b);
  return ESP_OK;
}

esp_err_t fx_transition_to(int ch, const char *name, uint32_t duration_ms, const effect_params_t *params)
{
 const effect_desc_t *fx = led_fx_get_by_name(name ? name : "");
 if (!fx) return ESP_ERR_NOT_FOUND;
 effect_engine_transition_to(ch, fx, duration_ms, params);
 return ESP_OK;
}

esp_err_t fx_list(const char ***names_out, size_t *count_out)
{
  if (!names_out) {
    return ESP_ERR_INVALID_ARG;
  }

  *names_out = NULL;

  size_t n = 0;
  const effect_desc_t *const *list = led_fx_list(&n);
  if (!list || !n) {
    return ESP_ERR_NOT_FOUND;
  }

  const char **names = (const char **)malloc(n * sizeof(char *));
  if (!names) {
    return ESP_ERR_NO_MEM;
  }

  for (size_t i = 0; i < n; ++i) {
    names[i] = list[i]->name;
  }

  *names_out = names;
  if (count_out) {
    *count_out = n;
  }

  return ESP_OK;
}

esp_err_t fx_set_speed(int ch, float speed)
{
  effect_engine_set_speed(ch, speed);
  return ESP_OK;
}

esp_err_t fx_set_intensity(int ch, float intensity)
{
  effect_engine_set_intensity(ch, intensity);
  return ESP_OK;
}

float fx_get_speed(int ch)
{
  effect_params_t p = effect_engine_get_params(ch);
  return p.speed;
}

float fx_get_intensity(int ch)
{
  effect_params_t p = effect_engine_get_params(ch);
  return p.intensity;
}

float fx_get_brightness(int ch)
{
  return effect_engine_get_brightness(ch);
}

const char* fx_current_name(int ch)
{
  const effect_desc_t* fx = effect_engine_get_fx(ch);
  return fx ? fx->name : NULL;
}

size_t fx_count(void)
{
  size_t n = 0;
  (void)led_fx_list(&n);
  return n;
}

const char* fx_name(size_t idx)
{
  size_t n = 0;
  const effect_desc_t* const* list = led_fx_list(&n);
  if (!list || idx >= n) {
    return NULL;
  }
  return list[idx]->name;
}

void fx_blackout(int ch, bool enable)
{
  effect_engine_blackout(ch, enable);
}
