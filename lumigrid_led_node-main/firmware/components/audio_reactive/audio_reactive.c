#include "audio_reactive.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Audio reactive implementation with ADC and I2S support.
 * Provides real-time audio level and frequency band analysis for LED effects.
 */

// Configuration constants
#define SAMPLE_BUFFER_SIZE 64
#define ADC_TO_AUDIO_SCALE 16  // Scale ADC 12-bit to 16-bit signed audio
#define RMS_VISIBILITY_SCALE 2.0f  // Boost RMS level for better visibility

// I2S GPIO pin configuration (should be made configurable via config system)
#define I2S_BCLK_PIN GPIO_NUM_26
#define I2S_WS_PIN GPIO_NUM_25
#define I2S_DIN_PIN GPIO_NUM_34

static const char* TAG="AUDIO_RX";
static ar_state_t g = { .enabled=false };

typedef struct {
  int mode; // 0=off,1=adc,2=i2s
  int sample_hz;
  uint32_t last_ms;
  float env;
  
  // ADC state
  adc_oneshot_unit_handle_t adc_handle;
  adc_channel_t adc_channel;
  adc_cali_handle_t adc_cali_handle;
  
  // I2S state
  i2s_chan_handle_t i2s_rx_handle;
  
  // Simple sample buffer for analysis
  int16_t samples[SAMPLE_BUFFER_SIZE];
  int sample_count;
} ar_impl_t;

static ar_impl_t s = {0};

void ar_init(void){ 
  memset(&g,0,sizeof(g)); 
  memset(&s,0,sizeof(s)); 
}

void ar_enable_adc(int adc_unit, int adc_channel, int sample_hz){
  // Clean up previous state if any
  ar_disable();
  
  s.mode = 1;
  s.sample_hz = sample_hz;
  s.adc_channel = (adc_channel_t)adc_channel;
  s.env = 0.0f;
  
  // Initialize ADC oneshot mode
  adc_oneshot_unit_init_cfg_t init_config = {
    .unit_id = (adc_unit_t)adc_unit,
  };
  esp_err_t ret = adc_oneshot_new_unit(&init_config, &s.adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
    s.mode = 0;
    return;
  }
  
  // Configure ADC channel
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten = ADC_ATTEN_DB_11,  // Full range 0-3.3V
  };
  ret = adc_oneshot_config_channel(s.adc_handle, s.adc_channel, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
    adc_oneshot_del_unit(s.adc_handle);
    s.adc_handle = NULL;
    s.mode = 0;
    return;
  }
  
  // Initialize calibration (optional but recommended)
  adc_cali_line_fitting_config_t cali_config = {
    .unit_id = (adc_unit_t)adc_unit,
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ret = adc_cali_create_scheme_line_fitting(&cali_config, &s.adc_cali_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ADC calibration not available: %s", esp_err_to_name(ret));
    s.adc_cali_handle = NULL;
  }
  
  g.enabled = true;
  ESP_LOGI(TAG, "ADC reactive enabled (unit=%d ch=%d @ %d Hz)", adc_unit, adc_channel, sample_hz);
}

void ar_enable_i2s(int i2s_port, int sample_hz){
  // Clean up previous state if any
  ar_disable();
  
  s.mode = 2;
  s.sample_hz = sample_hz;
  s.env = 0.0f;
  
  // Initialize I2S in standard RX mode for audio input
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_port, I2S_ROLE_MASTER);
  esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s.i2s_rx_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
    s.mode = 0;
    return;
  }
  
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_hz),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = I2S_BCLK_PIN,
      .ws = I2S_WS_PIN,
      .dout = I2S_GPIO_UNUSED,
      .din = I2S_DIN_PIN,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
      },
    },
  };
  
  ret = i2s_channel_init_std_mode(s.i2s_rx_handle, &std_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
    i2s_del_channel(s.i2s_rx_handle);
    s.i2s_rx_handle = NULL;
    s.mode = 0;
    return;
  }
  
  ret = i2s_channel_enable(s.i2s_rx_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
    i2s_del_channel(s.i2s_rx_handle);
    s.i2s_rx_handle = NULL;
    s.mode = 0;
    return;
  }
  
  g.enabled = true;
  ESP_LOGI(TAG, "I2S reactive enabled (port=%d @ %d Hz)", i2s_port, sample_hz);
}

void ar_disable(void){ 
  // Clean up ADC resources
  if (s.mode == 1 && s.adc_handle) {
    if (s.adc_cali_handle) {
      adc_cali_delete_scheme_line_fitting(s.adc_cali_handle);
      s.adc_cali_handle = NULL;
    }
    adc_oneshot_del_unit(s.adc_handle);
    s.adc_handle = NULL;
  }
  
  // Clean up I2S resources
  if (s.mode == 2 && s.i2s_rx_handle) {
    i2s_channel_disable(s.i2s_rx_handle);
    i2s_del_channel(s.i2s_rx_handle);
    s.i2s_rx_handle = NULL;
  }
  
  s.mode = 0; 
  g.enabled = false; 
  g.level = 0; 
  memset(g.bands, 0, sizeof(g.bands)); 
}

static float clamp01(float x){ if(x<0) return 0; if(x>1) return 1; return x; }

// Simple frequency band analysis without FFT
// Divides audio into 5 rough bands using peak detection
static void analyze_bands(const int16_t* samples, int count, uint8_t* bands) {
  if (count < 5) {
    memset(bands, 0, 5);
    return;
  }
  
  // Simple peak detection in time domain, grouped into bands
  // This is a rough approximation - true frequency analysis needs FFT
  int samples_per_band = count / 5;
  for (int band = 0; band < 5; band++) {
    int start = band * samples_per_band;
    int end = (band == 4) ? count : start + samples_per_band;
    
    float max_val = 0;
    for (int i = start; i < end; i++) {
      float val = fabsf((float)samples[i] / 32768.0f);
      if (val > max_val) max_val = val;
    }
    bands[band] = (uint8_t)(clamp01(max_val) * 255.0f);
  }
}

void ar_tick(uint32_t now_ms){
  if (!g.enabled || s.mode==0){ g.level=0; memset(g.bands,0,5); return; }
  
  s.sample_count = 0;
  memset(s.samples, 0, sizeof(s.samples));
  
  // Read samples based on mode
  if (s.mode == 1 && s.adc_handle) {
    // ADC mode: read multiple samples quickly
    for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
      int raw_value = 0;
      esp_err_t ret = adc_oneshot_read(s.adc_handle, s.adc_channel, &raw_value);
      if (ret == ESP_OK) {
        // Convert ADC reading (0-4095 for 12-bit) to signed 16-bit centered at 0
        s.samples[i] = (int16_t)((raw_value - 2048) * ADC_TO_AUDIO_SCALE);
        s.sample_count++;
      }
      // Small delay between samples to match sample rate
      if (s.sample_hz > 0 && i < SAMPLE_BUFFER_SIZE - 1) {
        int delay_us = 1000000 / s.sample_hz;
        if (delay_us > 10) {
          vTaskDelay(pdMS_TO_TICKS(1));  // Minimum FreeRTOS delay
        }
      }
    }
  } else if (s.mode == 2 && s.i2s_rx_handle) {
    // I2S mode: read a chunk of samples
    size_t bytes_read = 0;
    size_t bytes_to_read = SAMPLE_BUFFER_SIZE * sizeof(int16_t);
    esp_err_t ret = i2s_channel_read(s.i2s_rx_handle, s.samples, bytes_to_read, &bytes_read, 10);
    if (ret == ESP_OK) {
      s.sample_count = bytes_read / sizeof(int16_t);
    }
  }
  
  // If no samples collected, fall back to synthetic data
  if (s.sample_count == 0) {
    float t = (now_ms % 4096) / 4096.0f;
    float synth = 0.5f + 0.5f * sinf(6.28318f * (3.0f*t));
    float atk=0.15f, rel=0.04f;
    s.env = (synth > s.env) ? (s.env + atk*(synth - s.env)) : (s.env + rel*(synth - s.env));
    g.level = (uint8_t)(clamp01(s.env) * 255.0f);
    
    for (int i=0;i<5;++i){
      float w = fabsf(sinf(6.28318f * ((i+1)*t)));
      uint8_t v = (uint8_t)(clamp01(0.6f*s.env + 0.4f*w) * 255.0f);
      g.bands[i] = v;
    }
  } else {
    // Process real samples
    // Calculate RMS level
    float sum_sq = 0;
    for (int i = 0; i < s.sample_count; i++) {
      float val = (float)s.samples[i] / 32768.0f;
      sum_sq += val * val;
    }
    float rms = sqrtf(sum_sq / s.sample_count);
    
    // Apply envelope follower for smoothing
    float atk = 0.3f, rel = 0.1f;
    float target = rms;
    s.env = (target > s.env) ? (s.env + atk*(target - s.env)) : (s.env + rel*(target - s.env));
    g.level = (uint8_t)(clamp01(s.env * RMS_VISIBILITY_SCALE) * 255.0f);
    
    // Analyze frequency bands
    analyze_bands(s.samples, s.sample_count, g.bands);
  }
  
  s.last_ms = now_ms;
}

ar_state_t ar_get(void){ return g; }
