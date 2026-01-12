#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum tracks and events
#define SEQUENCER_MAX_TRACKS 16
#define SEQUENCER_MAX_EVENTS 256

// Track structure - represents a single channel (ALED 0-7 or PWM 8-15)
typedef struct {
    uint32_t id;
    char name[32];
    bool enabled;
    uint8_t channel;        // 0-15 (0-7: ALED, 8-15: PWM)
    float volume;           // 0.0 - 1.0 (brightness/intensity multiplier)
    bool muted;
    bool solo;
} track_t;

// Event types
typedef enum {
    EVT_EFFECT_START,       // Start effect with parameters
    EVT_EFFECT_STOP,        // Stop current effect
    EVT_KEYFRAME,           // Parameter automation point
    EVT_CUE,                // Scene recall
    EVT_TEMPO_CHANGE,       // BPM adjustment
    EVT_SYNC_MARKER         // Master/slave sync point
} event_type_t;

// Timeline event - represents an action at a specific time
typedef struct {
    uint32_t timestamp_ms;  // Absolute time from sequence start
    uint32_t duration_ms;   // Event duration
    uint8_t track_id;
    event_type_t event_type;
    void* event_data;       // Polymorphic event payload
} timeline_event_t;

// Keyframe for parameter automation
typedef struct {
    uint8_t param_id;       // Which parameter to automate
    float value;            // Target value
    uint16_t curve;         // LINEAR, EASE_IN, EASE_OUT, etc.
} keyframe_t;

// Interpolation curve types
typedef enum {
    CURVE_LINEAR = 0,
    CURVE_EASE_IN,
    CURVE_EASE_OUT,
    CURVE_EASE_IN_OUT,
    CURVE_S_CURVE
} curve_type_t;

// Quantization settings (MIDI-style)
typedef enum {
    QUANT_NONE = 0,
    QUANT_1_4,              // Quarter note
    QUANT_1_8,              // Eighth note
    QUANT_1_16,             // Sixteenth note
    QUANT_1_32              // Thirty-second note
} quantization_t;

// Sequence structure - contains all tracks and events
typedef struct {
    uint32_t sequence_id;
    char name[64];
    uint32_t total_duration_ms;
    uint16_t bpm;           // Master tempo
    uint8_t time_signature; // 4/4, 3/4, etc. (upper nibble = beats, lower = division)
    track_t tracks[SEQUENCER_MAX_TRACKS];
    uint8_t track_count;
    timeline_event_t* events;
    size_t event_count;
    size_t event_capacity;
} sequence_t;

// Playback state
typedef struct {
    sequence_t* sequence;
    uint32_t playhead_ms;
    bool playing;
    bool looping;
    uint32_t loop_start_ms;
    uint32_t loop_end_ms;
    float tempo_multiplier;  // 0.5x - 2.0x tempo scaling
    uint32_t last_update_ms;
} playback_state_t;

// API Functions

/**
 * @brief Initialize sequencer system
 */
esp_err_t sequencer_init(void);

/**
 * @brief Create a new empty sequence
 * @param name Sequence name
 * @param bpm Beats per minute
 * @return Pointer to new sequence or NULL on error
 */
sequence_t* sequencer_create_sequence(const char* name, uint16_t bpm);

/**
 * @brief Destroy a sequence and free resources
 * @param seq Sequence to destroy
 */
void sequencer_destroy_sequence(sequence_t* seq);

/**
 * @brief Add a track to a sequence
 * @param seq Sequence to modify
 * @param name Track name
 * @param channel Channel number (0-15)
 * @return Track ID or -1 on error
 */
int sequencer_add_track(sequence_t* seq, const char* name, uint8_t channel);

/**
 * @brief Add an event to a sequence
 * @param seq Sequence to modify
 * @param timestamp_ms When the event should trigger
 * @param duration_ms Event duration
 * @param track_id Which track
 * @param type Event type
 * @param data Event-specific data (copied internally)
 * @param data_size Size of event data
 * @return ESP_OK on success
 */
esp_err_t sequencer_add_event(sequence_t* seq, uint32_t timestamp_ms, 
                               uint32_t duration_ms, uint8_t track_id,
                               event_type_t type, const void* data, size_t data_size);

/**
 * @brief Remove an event from a sequence
 * @param seq Sequence to modify
 * @param event_index Index of event to remove
 * @return ESP_OK on success
 */
esp_err_t sequencer_remove_event(sequence_t* seq, size_t event_index);

/**
 * @brief Snap timestamp to nearest grid position
 * @param timestamp_ms Original timestamp
 * @param quant Quantization setting
 * @param bpm Current BPM
 * @return Quantized timestamp
 */
uint32_t sequencer_snap_to_grid(uint32_t timestamp_ms, quantization_t quant, uint16_t bpm);

/**
 * @brief Interpolate between two keyframes
 * @param kf_start Start keyframe
 * @param kf_end End keyframe
 * @param current_time Current time in ms
 * @param start_time Start keyframe time
 * @param end_time End keyframe time
 * @return Interpolated value
 */
float sequencer_interpolate_automation(const keyframe_t* kf_start, 
                                       const keyframe_t* kf_end,
                                       uint32_t current_time,
                                       uint32_t start_time,
                                       uint32_t end_time);

/**
 * @brief Create playback state for a sequence
 * @param seq Sequence to play
 * @return Playback state or NULL on error
 */
playback_state_t* sequencer_create_playback(sequence_t* seq);

/**
 * @brief Destroy playback state
 * @param state Playback state to destroy
 */
void sequencer_destroy_playback(playback_state_t* state);

/**
 * @brief Start playback
 * @param state Playback state
 * @return ESP_OK on success
 */
esp_err_t sequencer_play(playback_state_t* state);

/**
 * @brief Pause playback
 * @param state Playback state
 * @return ESP_OK on success
 */
esp_err_t sequencer_pause(playback_state_t* state);

/**
 * @brief Stop playback and reset playhead
 * @param state Playback state
 * @return ESP_OK on success
 */
esp_err_t sequencer_stop(playback_state_t* state);

/**
 * @brief Update playback state (call from update loop)
 * @param state Playback state
 * @param delta_ms Time since last update
 * @return ESP_OK on success
 */
esp_err_t sequencer_update(playback_state_t* state, uint32_t delta_ms);

/**
 * @brief Seek to specific time
 * @param state Playback state
 * @param time_ms Target time
 * @return ESP_OK on success
 */
esp_err_t sequencer_seek(playback_state_t* state, uint32_t time_ms);

/**
 * @brief Export sequence to JSON
 * @param seq Sequence to export
 * @param json_out Output buffer for JSON string (caller must free)
 * @return ESP_OK on success
 */
esp_err_t sequencer_export_json(const sequence_t* seq, char** json_out);

/**
 * @brief Import sequence from JSON
 * @param json_str JSON string
 * @return Sequence or NULL on error
 */
sequence_t* sequencer_import_json(const char* json_str);

#ifdef __cplusplus
}
#endif
