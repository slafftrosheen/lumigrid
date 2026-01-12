#include "trigger_engine.h"
#include "esp_timer.h"
#include <math.h>
#include "self_test.h"
#include "effect_control.h"
#include "task_pwm_driver.h"

// Forward declaration to work around build order issue
extern esp_err_t fx_set_channel_brightness(int ch, float brightness);

volatile float g_beat_phase = 0.f; // read by fx_waves()
static volatile uint32_t s_strobe_until_ms = 0;

void trigger_set_beat(float phase01){
  float wrapped = fmodf(phase01, 1.f);
  if (wrapped < 0.f){
    wrapped += 1.f;
  }
  g_beat_phase = wrapped;
}

void trigger_strobe(uint32_t ms){
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  s_strobe_until_ms = now_ms + ms;
}

float trigger_strobe_level(uint32_t now_ms){
  return (now_ms < s_strobe_until_ms) ? 1.f : 0.f;
}

void trigger_blackout(void){
  self_test_cancel();
  for (int i = 0; i < 8; ++i){
    fx_set_channel_brightness(i, 0.0f);
    pwm_set_mode_static(i, 0.f);
  }
}
