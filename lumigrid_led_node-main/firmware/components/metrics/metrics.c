#include "metrics.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nodesync.h"
#include <stdio.h>
#include <string.h>

// Batch 26: DMX support is optional
#ifdef CONFIG_DMXI_ENABLED
#include "dmxi.h"
#endif

static const char* TAG = "METRICS";
static uint32_t s_last_us=0; 
static uint32_t s_led_drops[8]={0};

void metrics_note_frame_us(uint32_t us){ 
  s_last_us=us; 
}

void metrics_pulse_led_drop(int ch){ 
  if(ch>=0 && ch<8) s_led_drops[ch]++; 
}

static esp_err_t h_metrics(httpd_req_t* r){
  httpd_resp_set_type(r,"text/plain; version=0.0.4");
  multi_heap_info_t hi; 
  heap_caps_get_info(&hi, MALLOC_CAP_DEFAULT);
  
  char b[512];  // Increased buffer size for metrics output
  snprintf(b,sizeof(b),
    "# HELP heap_free_bytes Free heap memory in bytes\n"
    "# TYPE heap_free_bytes gauge\n"
    "heap_free_bytes %lu\n"
    "# HELP heap_largest_free_block Largest free block in heap\n"
    "# TYPE heap_largest_free_block gauge\n"
    "heap_largest_free_block %lu\n"
    "# HELP fx_last_frame_us Last frame render time in microseconds\n"
    "# TYPE fx_last_frame_us gauge\n"
    "fx_last_frame_us %lu\n",
    (unsigned long)hi.total_free_bytes,
    (unsigned long)hi.largest_free_block,
    (unsigned long)s_last_us);
  httpd_resp_sendstr_chunk(r,b);
  
#ifdef CONFIG_DMXI_ENABLED
  snprintf(b,sizeof(b),
    "# HELP dmx_active DMX input active (1 if frames received in last 2s)\n"
    "# TYPE dmx_active gauge\n"
    "dmx_active %d\n",
    dmxi_active());
  httpd_resp_sendstr_chunk(r,b);
#endif
  
  snprintf(b,sizeof(b),
    "# HELP nodesync_master NodeSync master status (1=master, 0=follower)\n"
    "# TYPE nodesync_master gauge\n"
    "nodesync_master %d\n",
    nodesync_is_master());
  httpd_resp_sendstr_chunk(r,b);
  
  // LED drops - HELP/TYPE once, then all label values
  httpd_resp_sendstr_chunk(r,
    "# HELP led_drops_total Total LED frame drops\n"
    "# TYPE led_drops_total counter\n");
  for(int i=0;i<8;i++){
    snprintf(b,sizeof(b),
      "led_drops_total{channel=\"%d\"} %u\n",
      i,(unsigned)s_led_drops[i]);
    httpd_resp_sendstr_chunk(r,b);
  }
  
  httpd_resp_sendstr_chunk(r,NULL);
  return ESP_OK;
}

void metrics_register(httpd_handle_t s){
  const httpd_uri_t u={.uri="/metrics",.method=HTTP_GET,.handler=h_metrics};
  httpd_register_uri_handler(s,&u);
  ESP_LOGI(TAG, "Metrics endpoint registered at /metrics");
}
