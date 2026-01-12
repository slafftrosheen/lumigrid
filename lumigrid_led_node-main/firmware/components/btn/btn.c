#include "btn.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "effect_control.h"
#include "pca9685_driver.h"
#include "task_wifi.h"

static const char* TAG="BTN";
static int s_gpio=0, s_safe=0;
static int s_diag_mode=0;

// Diagnostic effect cycle
static void fx_next_diag(void){
  const char* effects[] = {"barber_pole", "strobe_sync", "twinkle_dust", "rainbow_flow"};
  s_diag_mode = (s_diag_mode + 1) % 4;
  ESP_LOGI(TAG, "Diagnostic mode %d: %s", s_diag_mode, effects[s_diag_mode]);
  fx_set_channel_by_name(0, effects[s_diag_mode], NULL);
  fx_set_channel_by_name(1, effects[s_diag_mode], NULL);
}

// Self-test: cycle through LED effects and PWM channels
#define PCA9685_MAX_CHANNELS 8
static void run_self_test(void){
  ESP_LOGI(TAG, "Running self-test...");
  fx_set_channel_by_name(0, "barber_pole", NULL);
  fx_set_channel_by_name(1, "barber_pole", NULL);
  
  // Sweep through PCA9685 PWM channels if available
  // Each channel briefly lights up to verify PWM functionality
  for (int ch = 0; ch < PCA9685_MAX_CHANNELS; ch++) {
    esp_err_t err = pca9685_set_duty(ch, 0.3f);
    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(100));  // Brief pulse
      pca9685_set_duty(ch, 0.0f);
    } else if (ch == 0) {
      // PCA9685 not available, log once and skip the rest
      ESP_LOGI(TAG, "PCA9685 not available, skipping PWM sweep");
      break;
    }
  }
}

// Toggle WiFi AP mode - switches between current mode and AP fallback
static void wifi_toggle_ap(void){
  ESP_LOGI(TAG, "AP toggle requested");
  
  // Get current WiFi mode
  wifi_mode_t mode;
  esp_err_t err = esp_wifi_get_mode(&mode);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not get WiFi mode, starting AP fallback");
    wifi_start_ap_fallback();
    return;
  }
  
  // If already in AP mode, this is idempotent and will just log
  // If in STA mode, switch to AP mode
  if (mode == WIFI_MODE_STA || mode == WIFI_MODE_NULL) {
    ESP_LOGI(TAG, "Switching to AP mode");
    wifi_start_ap_fallback();
  } else if (mode == WIFI_MODE_AP) {
    ESP_LOGI(TAG, "Already in AP mode");
  } else {
    ESP_LOGI(TAG, "WiFi in APSTA mode, switching to AP only");
    wifi_start_ap_fallback();
  }
}

static void task(void* arg){
  (void)arg;
  uint64_t down_start=0; 
  int prev_level=1; 
  int click=0;
  
  while(1){
    int level=gpio_get_level(s_gpio);
    uint64_t now=esp_timer_get_time()/1000ULL;
    
    // Detect button press edge (high to low transition)
    if(level==0 && prev_level==1){ 
      down_start=now; 
    }
    
    // Detect button release edge (low to high transition)
    if(level==1 && prev_level==0 && down_start > 0){
      uint64_t dur = now - down_start; 
      down_start=0;
      
      if (dur>3000){ 
        // long press: self-test
        run_self_test();
        click=0;
      } else {
        click++;
      }
    }
    
    // double-click window
    static uint64_t t_click0=0;
    if (click==1 && t_click0==0) t_click0=now;
    if (click && (now - t_click0)>400){
      if (click==1){
        fx_next_diag();
      } else if (click>=2){
        wifi_toggle_ap();
      }
      click=0; 
      t_click0=0;
    }
    
    prev_level = level;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void btn_init(int gpio_pullup){
  s_gpio=gpio_pullup;
  gpio_config_t io={
    .pin_bit_mask=1ULL<<s_gpio,
    .mode=GPIO_MODE_INPUT,
    .pull_up_en=1,
    .pull_down_en=0,
    .intr_type=GPIO_INTR_DISABLE
  };
  gpio_config(&io);
  
  // safe-boot check: held for 1s during early boot
  uint64_t t0=esp_timer_get_time();
  while ((esp_timer_get_time()-t0)<1000000ULL){
    if (gpio_get_level(s_gpio)==0){ 
      s_safe=1; 
      ESP_LOGW(TAG, "Safe boot detected!");
      break; 
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  
  xTaskCreate(task,"btn",3072,NULL,4,NULL);
}

int btn_is_safe_boot(void){ 
  return s_safe; 
}
