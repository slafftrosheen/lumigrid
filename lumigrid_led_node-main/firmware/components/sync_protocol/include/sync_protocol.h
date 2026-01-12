#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

// Core sync clock structure as per issue requirements
typedef struct {
    float bpm;
    uint32_t last_pulse_ms;
    bool running;
    // Internal state for resilience
    uint32_t phase_reset_ms;
    uint32_t last_sync_recv_ms;
    uint32_t desync_count;
} sync_clock_t;

// Protocol functions
esp_err_t sync_protocol_start_master(void);
esp_err_t sync_protocol_start_slave(void);
esp_err_t sync_protocol_stop(void);
void sync_protocol_init(void);

// Core sync functions as per issue requirements
void sync_pulse(uint32_t now_ms);         // trigger from UDP or master
float sync_phase_01(uint32_t now_ms);     // phase in [0,1]
float sync_phase_quantized(uint32_t now_ms, int steps); // quantized

// Legacy API (maintained for compatibility)
void sync_set_tempo(float bpm);
float sync_get_tempo(void);
float sync_get_phase(void);
float sync_drift_correction_ms(void);
void sync_tick_local(uint32_t now_ms);
void sync_rx_peer(uint32_t peer_ms, float peer_phase);

// Status and diagnostics
bool sync_is_synced(void);                // returns true if receiving sync
uint32_t sync_get_desync_count(void);     // count of desync events
