#include "selftest.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "effects.h"
#include "effect_control.h"
#include "storage_fs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char* TAG="SELFTEST";
static int s_btn=-1;
static uint64_t s_down_us=0;
static int s_mode=0;

static void apply_mode(int m){
  switch(m%5){
    case 0: 
      fx_set_channel_by_name(0,"barber_pole",NULL); 
      fx_set_channel_by_name(1,"barber_pole",NULL); 
      break;
    case 1: 
      fx_set_channel_by_name(0,"strobe_sync",NULL); 
      fx_set_channel_by_name(1,"strobe_sync",NULL); 
      break;
    case 2: 
      fx_set_channel_by_name(0,"twinkle_dust",NULL); 
      fx_set_channel_by_name(1,"twinkle_dust",NULL); 
      break;
    case 3: 
      fx_set_channel_by_name(0,"rainbow_flow",NULL); 
      fx_set_channel_by_name(1,"rainbow_flow",NULL); 
      break;
    case 4: 
      fx_set_channel_by_name(0,"vu_bars",NULL); 
      fx_set_channel_by_name(1,"vu_bars",NULL); 
      break;
  }
}

static void poll(void* arg){
  (void)arg;
  int lvl = gpio_get_level(s_btn);
  uint64_t now = esp_timer_get_time();
  static int prev = 1;
  if (lvl==0 && prev==1){ s_down_us = now; }
  if (lvl==1 && prev==0){
    uint64_t dur = now - s_down_us;
    if (dur > 200000 && dur < 3000000){
      s_mode++; apply_mode(s_mode);
      ESP_LOGI(TAG,"short press: mode=%d", s_mode%5);
    }
  }
  // long hold
  if (lvl==0 && s_down_us && now - s_down_us > 5000000ULL){
    ESP_LOGW(TAG,"long hold detected: factory reset storage");
    // Note: actual format implementation would go here
    // For now, just restart to trigger format_if_mount_failed
    ESP_LOGW(TAG,"Restarting to trigger storage format on next mount...");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  }
  prev = lvl;
}

void selftest_init(int gpio_btn){
  s_btn = gpio_btn;
  gpio_config_t c = {
    .pin_bit_mask = 1ULL<<s_btn,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = 1,
    .pull_down_en = 0,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&c);
  apply_mode(0);

  static esp_timer_handle_t tmr;
  const esp_timer_create_args_t a = { .callback = poll, .name="btnpoll" };
  esp_timer_create(&a,&tmr);
  esp_timer_start_periodic(tmr, 50*1000); // 50ms
}
