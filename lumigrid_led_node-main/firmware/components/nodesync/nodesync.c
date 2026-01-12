#include "nodesync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/igmp.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "sync.h"
#include "effect_control.h"
#include "mdns.h"

static const char* TAG = "NODESYNC";
static int s_sock = -1;
static struct sockaddr_in s_mcast_addr;
static bool s_master = false;
static esp_timer_handle_t s_tx_timer = NULL;

// Batch 31: NodeSync v2 state tracking
static float s_bpm = 120.0f;
static char s_effect[32] = "barber_pole";
static uint32_t s_beat_epoch_ms = 0;  // epoch of beat zero (master p.o.v)

void nodesync_set_master(bool on) {
  s_master = on;
}

int nodesync_is_master(void) {
  return s_master ? 1 : 0;
}

static void tx_cb(void* arg) {
  (void)arg;
  if (!s_master || s_sock < 0) {
    return;
  }
  uint8_t pkt[2 + 1 + 1 + 4 + 4];
  pkt[0] = 'L';
  pkt[1] = 'G';
  pkt[2] = 1;
  pkt[3] = 1;
  uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  memcpy(&pkt[4], &ms, sizeof(ms));
  float bpm = sync_get_bpm();
  memcpy(&pkt[8], &bpm, sizeof(bpm));
  sendto(s_sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&s_mcast_addr, sizeof(s_mcast_addr));
}

static void rx_task(void* arg) {
  (void)arg;
  uint8_t buf[128];
  while (1) {
    int len = recv(s_sock, buf, sizeof(buf), 0);
    if (len <= 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (len >= 12 && buf[0] == 'L' && buf[1] == 'G' && buf[2] == 1) {
      if (buf[3] == 1 && len >= 12) {
        uint32_t ms = 0;
        float bpm = 0.f;
        memcpy(&ms, &buf[4], sizeof(ms));
        memcpy(&bpm, &buf[8], sizeof(bpm));
        // Update local state for phase tracking
        s_bpm = bpm;
        s_beat_epoch_ms = ms;
        sync_set_bpm(bpm);
        sync_pulse(ms);
      } else if (buf[3] == 2 && len > 8) {
        float bpm = 0.f;
        memcpy(&bpm, &buf[4], sizeof(bpm));
        int name_len = len - 8;
        if (name_len > 31) {
          name_len = 31;
        }
        char name[32];
        memcpy(name, &buf[8], name_len);
        name[name_len] = '\0';
        // Update local state
        s_bpm = bpm;
        strncpy(s_effect, name, sizeof(s_effect) - 1);
        s_effect[sizeof(s_effect) - 1] = '\0';
        for (int ch = 0; ch < 4; ++ch) {
          fx_set_channel_by_name(ch, name, NULL);
        }
        sync_set_bpm(bpm);
      }
    }
  }
}

void nodesync_broadcast_scene(const char* effect, float bpm) {
  if (s_sock < 0 || !effect) {
    return;
  }
  uint8_t pkt[2 + 1 + 1 + 4 + 32];
  pkt[0] = 'L';
  pkt[1] = 'G';
  pkt[2] = 1;
  pkt[3] = 2;
  memcpy(&pkt[4], &bpm, sizeof(bpm));
  size_t len = strnlen(effect, 31);
  memcpy(&pkt[8], effect, len);
  sendto(s_sock, pkt, (int)(8 + len), 0, (struct sockaddr*)&s_mcast_addr, sizeof(s_mcast_addr));
}

void nodesync_init(void) {
  if (s_sock >= 0) {
    return;
  }
  s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s_sock < 0) {
    ESP_LOGW(TAG, "socket create failed");
    return;
  }
  int yes = 1;
  setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  struct sockaddr_in local = {
    .sin_family = AF_INET,
    .sin_port = htons(5533),
    .sin_addr.s_addr = htonl(INADDR_ANY)
  };
  if (bind(s_sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
    ESP_LOGW(TAG, "bind failed");
  }
  ip_mreq mreq = {
    .imr_multiaddr.s_addr = inet_addr("239.12.23.34"),
    .imr_interface.s_addr = htonl(INADDR_ANY)
  };
  setsockopt(s_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  s_mcast_addr.sin_family = AF_INET;
  s_mcast_addr.sin_port = htons(5533);
  s_mcast_addr.sin_addr.s_addr = inet_addr("239.12.23.34");

  xTaskCreate(rx_task, "ns_rx", 4096, NULL, 5, NULL);

  esp_timer_create_args_t args = {
    .callback = tx_cb,
    .arg = NULL,
    .name = "ns_tx"
  };
  esp_timer_create(&args, &s_tx_timer);
  esp_timer_start_periodic(s_tx_timer, 50 * 1000);

  mdns_service_add(NULL, "_lumigrid", "_udp", 5533, NULL, 0);
}

// Batch 31: NodeSync v2 additions
static uint32_t uptime_ms(void) { 
  return (uint32_t)(esp_timer_get_time() / 1000ULL); 
}

void nodesync_tick_beat(void) {
  if (!s_master || s_sock < 0) {
    return;
  }
  // Already handled by tx_cb periodic timer
  // This function can be called explicitly if needed
}

float nodesync_now_phase(float* out_bpm) {
  uint32_t now = uptime_ms();
  float bpm_val = s_bpm;
  if (bpm_val < 10.0f) bpm_val = 120.0f;
  
  float beat_ms = 60000.0f / bpm_val;
  float elapsed = (float)(now - s_beat_epoch_ms);
  float phase = fmodf(elapsed / beat_ms, 1.0f);
  if (phase < 0.0f) phase += 1.0f;
  
  if (out_bpm) *out_bpm = bpm_val;
  return phase;
}
