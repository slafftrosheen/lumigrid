#include "dmxi.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage_fs.h"
#include "layout.h"
#include <string.h>

static const char* TAG="DMXI";
static int s_uart_num=UART_NUM_1;  // Store the UART number
static int s_univ_start=1;    // DMX start addr (1..512)
static int s_timeout_ms=2000;
static volatile int s_last_ms=0;
static bool s_initialized=false;

// DMX frame buffer - stores last received DMX data
static uint8_t s_dmx_frame[513];
static volatile bool s_frame_ready = false;

typedef struct { 
  int ch;      // channel index
  int mode;    // 3=RGB, 4=RGBW
  int start;   // DMX address start (1-based)
  float scale; 
} map_t;

static map_t s_map[8]; 
static int s_mapn=0;

static void load_cfg(void){
  char path[128];
  if (!storage_fs_path(path, sizeof(path), "dmx.json")){
    ESP_LOGW(TAG,"no dmx.json path available");
    return;
  }
  
  FILE* f=fopen(path,"r");
  if(!f){ 
    ESP_LOGW(TAG,"no /storage/dmx.json"); 
    return; 
  }
  
  char buf[1024]; 
  int n=fread(buf,1,sizeof(buf)-1,f); 
  buf[n]=0; 
  fclose(f);
  
  cJSON* j=cJSON_Parse(buf); 
  if(!j) return;
  
  cJSON* s=cJSON_GetObjectItem(j,"start"); 
  if(s && cJSON_IsNumber(s)) s_univ_start=s->valueint;
  
  cJSON* t=cJSON_GetObjectItem(j,"timeout_ms"); 
  if(t && cJSON_IsNumber(t)) s_timeout_ms=t->valueint;
  
  cJSON* m=cJSON_GetObjectItem(j,"map");
  if (m && cJSON_IsArray(m)){
    s_mapn=0;
    for (int i=0;i<cJSON_GetArraySize(m) && s_mapn<8;i++){
      cJSON* e=cJSON_GetArrayItem(m,i);
      if(!cJSON_IsObject(e)) continue;
      
      cJSON* ch_obj = cJSON_GetObjectItem(e,"channel");
      cJSON* mode_obj = cJSON_GetObjectItem(e,"mode");
      cJSON* addr_obj = cJSON_GetObjectItem(e,"addr");
      cJSON* scale_obj = cJSON_GetObjectItem(e,"scale");
      
      if(!ch_obj || !mode_obj || !addr_obj) continue;
      
      s_map[s_mapn]=(map_t){
        .ch = ch_obj->valueint,
        .mode = mode_obj->valueint,
        .start = addr_obj->valueint,
        .scale = scale_obj ? (float)scale_obj->valuedouble : 1.0f
      };
      s_mapn++;
    }
  }
  cJSON_Delete(j);
  ESP_LOGI(TAG,"DMX config loaded: %d mappings", s_mapn);
}

static void rx_task(void* arg){
  uint8_t frame[513]; // 1 start code + 512 slots
  
  while(1){
    // crude DMX receiver: look for long BREAK using uart_get_buffered_data_len
    size_t len=0;
    uart_get_buffered_data_len(s_uart_num, &len);
    
    if (len>=50){ // heuristic for break + slots
      int n = uart_read_bytes(s_uart_num, frame, sizeof(frame), 2/portTICK_PERIOD_MS);
      if (n>= (s_univ_start+1)){
        // mark time
        s_last_ms = (int)(esp_timer_get_time()/1000ULL);
        
        // Store frame for external access
        memcpy((void*)s_dmx_frame, frame, sizeof(s_dmx_frame));
        s_frame_ready = true;
        
        // Note: Direct framebuffer manipulation removed to decouple from aled_state
        // Applications should poll dmxi_active() and dmxi_get_channel_value()
        // to integrate DMX data into their rendering pipeline
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void dmxi_init(int uart_num, int rx_gpio){
  if(s_initialized) return;
  s_initialized = true;
  s_uart_num = uart_num;
  
  load_cfg();
  
  const uart_config_t cfg={ 
    .baud_rate=250000, 
    .data_bits=UART_DATA_8_BITS, 
    .parity=UART_PARITY_DISABLE, 
    .stop_bits=UART_STOP_BITS_2, 
    .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT
  };
  
  uart_driver_install(s_uart_num, 1024, 0, 0, NULL, 0);
  uart_param_config(s_uart_num, &cfg);
  uart_set_pin(s_uart_num, UART_PIN_NO_CHANGE, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  
  xTaskCreate(rx_task,"dmx_rx",4096,NULL,5,NULL);
  ESP_LOGI(TAG,"DMX input initialized on UART%d GPIO%d", s_uart_num, rx_gpio);
}

int dmxi_active(void){
  int now=(int)(esp_timer_get_time()/1000ULL);
  return ((now - s_last_ms) < s_timeout_ms) ? 1 : 0;
}

// Get DMX value for a specific channel
uint8_t dmxi_get_channel_value(int dmx_addr){
  if(!s_frame_ready || dmx_addr < 1 || dmx_addr > 512) return 0;
  return s_dmx_frame[dmx_addr];
}

// Get RGB values from DMX starting at addr
void dmxi_get_rgb(int dmx_addr, uint8_t* r, uint8_t* g, uint8_t* b){
  if(!s_frame_ready || dmx_addr < 1 || dmx_addr > 510) {
    *r = *g = *b = 0;
    return;
  }
  *r = s_dmx_frame[dmx_addr];
  *g = s_dmx_frame[dmx_addr+1];
  *b = s_dmx_frame[dmx_addr+2];
}

// Get RGBW values from DMX starting at addr
void dmxi_get_rgbw(int dmx_addr, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* w){
  if(!s_frame_ready || dmx_addr < 1 || dmx_addr > 509) {
    *r = *g = *b = *w = 0;
    return;
  }
  *r = s_dmx_frame[dmx_addr];
  *g = s_dmx_frame[dmx_addr+1];
  *b = s_dmx_frame[dmx_addr+2];
  *w = s_dmx_frame[dmx_addr+3];
}
