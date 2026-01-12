#include "sync_protocol.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include <errno.h>

static const char *TAG = "SYNC_PROTO";

#define MCAST_GRP "239.10.7.42"
#define MCAST_PORT 45454
#define SYNC_TIMEOUT_MS 3000        // Consider desync if no packet for 3 seconds
#define DESYNC_THRESHOLD 5          // Number of consecutive missed/bad packets before desync
#define MAX_JITTER_MS 100.0         // Maximum acceptable jitter in ms
#define DEFAULT_BPM 120.0f          // Default/fallback BPM

typedef enum {
    SYNC_PACKET_TICK = 1,
    SYNC_PACKET_CUE = 2
} sync_packet_type_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t type;
    uint8_t pad[3];
} sync_packet_t;

// Main sync clock structure
static sync_clock_t s_clock = {
    .bpm = DEFAULT_BPM,
    .last_pulse_ms = 0,
    .running = true,
    .phase_reset_ms = 0,
    .last_sync_recv_ms = 0,
    .desync_count = 0
};

static int s_tx_socket = -1;
static int s_rx_socket = -1;
static bool s_running = false;
static double s_phase = 0.0;
static double s_last_ms = 0.0;
static double s_drift_ewma = 0.0;
static bool s_is_synced = false;
static uint32_t s_missed_packets = 0;

extern volatile float g_beat_phase __attribute__((weak));

static inline void publish_phase(void){
    if (&g_beat_phase){
        g_beat_phase = (float)s_phase;
    }
}

// Core sync functions as per issue requirements

void sync_pulse(uint32_t now_ms) {
    s_clock.phase_reset_ms = now_ms;
    s_clock.last_pulse_ms = now_ms;
    s_phase = 0.0;
    ESP_LOGD(TAG, "sync_pulse: Reset phase at %lu ms", now_ms);
    publish_phase();
}

static inline float fracf(float x) {
    return x - floorf(x);
}

float sync_phase_01(uint32_t now_ms) {
    if (!s_clock.running) {
        ESP_LOGD(TAG, "sync_phase_01: Clock not running, returning 0.0");
        return 0.0f;  // Clock stopped, phase is effectively 0
    }
    
    const float ms_per_qn = 60000.0f / (s_clock.bpm > 0.1f ? s_clock.bpm : DEFAULT_BPM);
    float dt = (float)(now_ms - s_clock.phase_reset_ms);
    float phase = fracf(dt / ms_per_qn);
    
    return phase;
}

float sync_phase_quantized(uint32_t now_ms, int steps) {
    float p = sync_phase_01(now_ms);
    if (steps <= 1) {
        return p;
    }
    float s_val = floorf(p * steps + 0.5f) / (float)steps;
    return fmodf(s_val, 1.0f);
}

// Legacy API maintained for compatibility

void sync_set_tempo(float bpm){
    float old_bpm = s_clock.bpm;
    s_clock.bpm = (bpm > 20.f) ? bpm : 20.f;
    s_last_ms = 0.0;
    
    if (fabsf(old_bpm - s_clock.bpm) > 0.1f) {
        ESP_LOGD(TAG, "Tempo changed: %.1f -> %.1f BPM", old_bpm, s_clock.bpm);
    }
}

float sync_get_tempo(void){
    return s_clock.bpm;
}

float sync_get_phase(void){
    return (float)s_phase;
}

float sync_drift_correction_ms(void){
    return (float)s_drift_ewma;
}

bool sync_is_synced(void) {
    return s_is_synced;
}

uint32_t sync_get_desync_count(void) {
    return s_clock.desync_count;
}

void sync_tick_local(uint32_t now_ms){
    double period = 60000.0 / (double)s_clock.bpm;
    if (period <= 0.0){
        period = 500.0;
    }
    if (s_last_ms == 0.0){
        s_last_ms = now_ms;
        return;
    }
    double dt = (double)now_ms - s_last_ms;
    s_last_ms = now_ms;
    double next_phase = s_phase + dt / period;
    s_phase = fmod(next_phase, 1.0);
    if (s_phase < 0.0){
        s_phase += 1.0;
    }
    publish_phase();
    
    // Check for desync timeout in slave mode
    if (s_rx_socket >= 0 && s_clock.last_sync_recv_ms > 0) {
        uint32_t time_since_sync = now_ms - s_clock.last_sync_recv_ms;
        
        if (time_since_sync > SYNC_TIMEOUT_MS && s_is_synced) {
            s_is_synced = false;
            s_clock.desync_count++;
            s_missed_packets++;
            ESP_LOGW(TAG, "DESYNC DETECTED: No sync packets for %lu ms (desync count: %lu)", 
                     time_since_sync, s_clock.desync_count);
            ESP_LOGI(TAG, "FALLBACK: Switching to local clock (BPM: %.1f)", s_clock.bpm);
        }
    }
}

void sync_rx_peer(uint32_t peer_ms, float peer_phase){
    double local_ms = esp_timer_get_time() / 1000.0;
    double drift = (double)peer_ms - local_ms;
    
    // Check for excessive jitter
    double jitter = fabs(drift - s_drift_ewma);
    if (jitter > MAX_JITTER_MS && s_is_synced) {
        s_missed_packets++;
        ESP_LOGW(TAG, "High jitter detected: %.1f ms (drift: %.1f, EWMA: %.1f)", 
                 jitter, drift, s_drift_ewma);
        
        if (s_missed_packets >= DESYNC_THRESHOLD) {
            s_is_synced = false;
            s_clock.desync_count++;
            ESP_LOGW(TAG, "DESYNC: Excessive jitter threshold exceeded (%lu consecutive)", 
                     s_missed_packets);
            ESP_LOGI(TAG, "FALLBACK: Switching to local clock");
        }
    } else {
        // Good packet received
        if (s_missed_packets > 0) {
            ESP_LOGD(TAG, "Sync recovered after %lu missed packets", s_missed_packets);
        }
        s_missed_packets = 0;
        
        if (!s_is_synced) {
            s_is_synced = true;
            ESP_LOGI(TAG, "SYNC ESTABLISHED: Now following master clock");
        }
    }
    
    // Update drift with EWMA
    double old_drift = s_drift_ewma;
    s_drift_ewma = 0.9 * s_drift_ewma + 0.1 * drift;
    
    if (fabs(old_drift - s_drift_ewma) > 5.0) {
        ESP_LOGD(TAG, "Drift correction updated: %.2f -> %.2f ms", old_drift, s_drift_ewma);
    }

    if (peer_phase >= 0.0f){
        double wrapped = fmod((double)peer_phase, 1.0);
        if (wrapped < 0.0){
            wrapped += 1.0;
        }
        double phase_diff = fabs(s_phase - wrapped);
        if (phase_diff > 0.05) {
            ESP_LOGD(TAG, "Phase correction: %.3f -> %.3f (diff: %.3f)", 
                     s_phase, wrapped, phase_diff);
        }
        s_phase = wrapped;
        publish_phase();
    }
    
    // Update last sync receive time
    s_clock.last_sync_recv_ms = (uint32_t)local_ms;
}

static void sync_tx_task(void *arg) {
    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(MCAST_PORT);
    inet_aton(MCAST_GRP, &dest_addr.sin_addr);
    
    ESP_LOGI(TAG, "TX task started, sending to %s:%d @ 20Hz", MCAST_GRP, MCAST_PORT);
    ESP_LOGI(TAG, "MASTER MODE: Broadcasting sync ticks (BPM: %.1f)", s_clock.bpm);
    
    uint32_t tick_count = 0;
    while (s_running) {
        sync_packet_t packet = {
            .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .type = SYNC_PACKET_TICK,
            .pad = {0, 0, 0}
        };
        
        int err = sendto(s_tx_socket, &packet, sizeof(packet), 0,
                        (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        
        if (err < 0) {
            ESP_LOGW(TAG, "TX error: %d (errno: %d)", err, errno);
        } else {
            tick_count++;
            if (tick_count % 100 == 0) {
                ESP_LOGD(TAG, "Sent %lu tick packets (ts: %lu)", tick_count, packet.timestamp_ms);
            }
        }
        
        // 20 Hz = 50ms interval
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "TX task stopped (sent %lu packets total)", tick_count);
    vTaskDelete(NULL);
}

static void sync_rx_task(void *arg) {
    ESP_LOGI(TAG, "RX task started, listening on %s:%d", MCAST_GRP, MCAST_PORT);
    ESP_LOGI(TAG, "SLAVE MODE: Waiting for master sync (timeout: %d ms)", SYNC_TIMEOUT_MS);
    
    uint32_t packet_count = 0;
    uint32_t error_count = 0;
    
    while (s_running) {
        sync_packet_t packet;
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        
        int len = recvfrom(s_rx_socket, &packet, sizeof(packet), 0,
                          (struct sockaddr *)&source_addr, &addr_len);
        
        if (len == sizeof(packet)) {
            uint32_t local_ms = (uint32_t)(esp_timer_get_time() / 1000);
            int32_t drift_ms = local_ms - packet.timestamp_ms;

            if (packet.type == SYNC_PACKET_TICK) {
                packet_count++;
                sync_rx_peer(packet.timestamp_ms, -1.f);
                
                if (packet_count % 100 == 0) {
                    ESP_LOGD(TAG, "Received %lu tick packets (drift: %ld ms, synced: %d)", 
                             packet_count, drift_ms, s_is_synced);
                }
                
                if (packet_count == 1) {
                    ESP_LOGI(TAG, "First sync packet received (drift: %ld ms)", drift_ms);
                }
            } else if (packet.type == SYNC_PACKET_CUE) {
                ESP_LOGI(TAG, "CUE received at %lu (local: %lu)", packet.timestamp_ms, local_ms);
            } else {
                error_count++;
                ESP_LOGW(TAG, "Invalid packet type: %d", packet.type);
            }
        } else if (len > 0) {
            error_count++;
            ESP_LOGW(TAG, "Malformed packet (size: %d, expected: %d)", len, sizeof(packet));
        }
    }
    
    ESP_LOGI(TAG, "RX task stopped (received %lu packets, %lu errors)", 
             packet_count, error_count);
    vTaskDelete(NULL);
}

esp_err_t sync_protocol_start_master(void) {
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }
    
    s_tx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_tx_socket < 0) {
        ESP_LOGE(TAG, "Failed to create TX socket");
        return ESP_FAIL;
    }
    
    uint8_t ttl = 1;
    setsockopt(s_tx_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    
    s_running = true;
    s_clock.running = true;
    
    // Reset state for master mode
    s_is_synced = false;
    s_missed_packets = 0;
    s_clock.desync_count = 0;
    
    xTaskCreate(sync_tx_task, "sync_tx", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Master mode started");
    ESP_LOGI(TAG, "BPM: %.1f", s_clock.bpm);
    ESP_LOGI(TAG, "Multicast: %s:%d", MCAST_GRP, MCAST_PORT);
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

esp_err_t sync_protocol_start_slave(void) {
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }
    
    s_rx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_rx_socket < 0) {
        ESP_LOGE(TAG, "Failed to create RX socket");
        return ESP_FAIL;
    }
    
    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MCAST_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(s_rx_socket, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind RX socket");
        close(s_rx_socket);
        return ESP_FAIL;
    }
    
    struct ip_mreq imreq = {0};
    inet_aton(MCAST_GRP, &imreq.imr_multiaddr);
    imreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(s_rx_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &imreq, sizeof(imreq)) < 0) {
        ESP_LOGE(TAG, "Failed to join multicast group");
        close(s_rx_socket);
        return ESP_FAIL;
    }
    
    s_running = true;
    s_clock.running = true;
    
    // Reset state for slave mode
    s_is_synced = false;
    s_missed_packets = 0;
    s_clock.last_sync_recv_ms = 0;
    s_clock.desync_count = 0;
    
    xTaskCreate(sync_rx_task, "sync_rx", 4096, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Slave mode started");
    ESP_LOGI(TAG, "Local BPM (fallback): %.1f", s_clock.bpm);
    ESP_LOGI(TAG, "Multicast: %s:%d", MCAST_GRP, MCAST_PORT);
    ESP_LOGI(TAG, "Desync timeout: %d ms", SYNC_TIMEOUT_MS);
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

esp_err_t sync_protocol_stop(void) {
    if (!s_running) {
        ESP_LOGW(TAG, "Not running");
        return ESP_OK;
    }
    
    s_running = false;
    s_clock.running = false;
    
    if (s_tx_socket >= 0) {
        close(s_tx_socket);
        s_tx_socket = -1;
        ESP_LOGI(TAG, "Master TX socket closed");
    }
    
    if (s_rx_socket >= 0) {
        close(s_rx_socket);
        s_rx_socket = -1;
        ESP_LOGI(TAG, "Slave RX socket closed");
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Sync protocol stopped");
    ESP_LOGI(TAG, "Total desync events: %lu", s_clock.desync_count);
    ESP_LOGI(TAG, "Final drift EWMA: %.2f ms", s_drift_ewma);
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}

void sync_protocol_init(void) {
    ESP_LOGI(TAG, "Sync protocol initialized");
    ESP_LOGI(TAG, "Call sync_protocol_start_master() or sync_protocol_start_slave()");
    ESP_LOGD(TAG, "Default BPM: %.1f", s_clock.bpm);
    ESP_LOGD(TAG, "Multicast group: %s:%d", MCAST_GRP, MCAST_PORT);
}
