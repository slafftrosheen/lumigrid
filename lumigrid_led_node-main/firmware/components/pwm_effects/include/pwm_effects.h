#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum PWM channels (typically 16 for PCA9685)
#define PWM_MAX_CHANNELS 16
#define PWM_MAX_GROUPS 4

// Waveform types
typedef enum {
    WAVE_SINE,
    WAVE_TRIANGLE,
    WAVE_SAWTOOTH,
    WAVE_REVERSE_SAWTOOTH,
    WAVE_SQUARE,
    WAVE_PULSE,             // Configurable duty cycle
    WAVE_EXPONENTIAL,
    WAVE_LOGARITHMIC,
    WAVE_CUSTOM             // User-defined lookup table
} waveform_type_t;

// Waveform configuration
typedef struct {
    waveform_type_t type;
    float frequency_hz;     // 0.01 - 100 Hz
    float amplitude;        // 0.0 - 1.0
    float offset;           // DC offset
    float duty_cycle;       // For pulse waveform (0.0 - 1.0)
    uint16_t* lut;          // Custom waveform samples (12-bit values)
    uint16_t lut_size;      // Number of samples in LUT
    float phase;            // Current phase (0.0 - 1.0)
} waveform_config_t;

// ADSR Envelope
typedef struct {
    float attack_ms;        // Time to reach peak
    float decay_ms;         // Time to sustain level
    float sustain_level;    // 0.0 - 1.0
    float release_ms;       // Time to fade out
    bool gate_on;           // Trigger state
    uint32_t trigger_time;  // When gate was triggered
    uint32_t release_time;  // When gate was released
} adsr_envelope_t;

// PWM Channel Group (for synchronized control)
typedef struct {
    uint8_t channels[PWM_MAX_CHANNELS];
    uint8_t channel_count;
    bool synchronized;      // Lock to same phase
    float phase_offset[PWM_MAX_CHANNELS];  // Individual phase shifts (0.0-1.0)
} pwm_group_t;

// Candle flicker effect
typedef struct {
    float base_brightness;  // 0.0 - 1.0
    float flicker_amount;   // 0.0 - 1.0
    float flicker_speed;    // Hz
    uint32_t seed;          // Random seed
} pwm_candle_t;

// Lightning flash effect
typedef struct {
    float trigger_probability;  // Per update (0.0 - 1.0)
    uint8_t flash_count;        // Flashes per strike (1-10)
    uint16_t flash_duration_ms; // Duration of each flash
    uint16_t inter_flash_ms;    // Time between flashes
    bool active;                // Currently flashing
    uint8_t current_flash;      // Current flash in sequence
    uint32_t next_flash_time;   // When next flash should occur
} pwm_lightning_t;

// Fade effect (sunrise/sunset)
typedef struct {
    uint32_t duration_ms;
    float start_brightness;
    float end_brightness;
    uint8_t curve;          // curve_type_t from sequencer
    uint32_t start_time;
    bool active;
} pwm_fade_t;

// Color temperature shift (for RGBW)
typedef struct {
    uint16_t kelvin_start;  // 2000K - 6500K
    uint16_t kelvin_end;
    uint32_t transition_ms;
    uint32_t start_time;
    bool active;
} pwm_color_temp_t;

// DMX mapping
typedef struct {
    uint8_t pwm_channel;
    uint16_t dmx_address;   // 1-512
    uint8_t dmx_universe;   // 0-15
    bool inverted;
    float scale;            // Multiplier
} pwm_dmx_mapping_t;

// Control change (MIDI-style)
typedef struct {
    uint8_t channel;
    uint8_t cc_number;      // MIDI CC number
    float value;            // 0.0 - 1.0
    uint32_t timestamp_ms;
} pwm_control_change_t;

// Automation recording
typedef struct {
    pwm_control_change_t* events;
    size_t event_count;
    size_t event_capacity;
    bool recording;
    uint32_t start_time_ms;
} pwm_automation_t;

// API Functions

/**
 * @brief Initialize PWM effects system
 */
esp_err_t pwm_effects_init(void);

/**
 * @brief Create a waveform generator for a channel
 * @param channel PWM channel (0-15)
 * @param config Waveform configuration
 * @return ESP_OK on success
 */
esp_err_t pwm_waveform_create(uint8_t channel, const waveform_config_t* config);

/**
 * @brief Update waveform generator (call periodically)
 * @param channel PWM channel
 * @param delta_ms Time since last update
 * @return ESP_OK on success
 */
esp_err_t pwm_waveform_update(uint8_t channel, uint32_t delta_ms);

/**
 * @brief Stop waveform generator
 * @param channel PWM channel
 * @return ESP_OK on success
 */
esp_err_t pwm_waveform_stop(uint8_t channel);

/**
 * @brief Create ADSR envelope
 * @param channel PWM channel
 * @param envelope ADSR parameters
 * @return ESP_OK on success
 */
esp_err_t pwm_adsr_create(uint8_t channel, const adsr_envelope_t* envelope);

/**
 * @brief Trigger ADSR envelope (gate on)
 * @param channel PWM channel
 * @return ESP_OK on success
 */
esp_err_t pwm_adsr_trigger(uint8_t channel);

/**
 * @brief Release ADSR envelope (gate off)
 * @param channel PWM channel
 * @return ESP_OK on success
 */
esp_err_t pwm_adsr_release(uint8_t channel);

/**
 * @brief Update ADSR envelope
 * @param channel PWM channel
 * @param current_time Current time in ms
 * @return Current envelope value (0.0 - 1.0)
 */
float pwm_adsr_calculate(uint8_t channel, uint32_t current_time);

/**
 * @brief Create channel group for synchronized control
 * @param group_id Group ID (0-3)
 * @param channels Array of channel numbers
 * @param count Number of channels
 * @return ESP_OK on success
 */
esp_err_t pwm_group_create(uint8_t group_id, const uint8_t* channels, uint8_t count);

/**
 * @brief Set phase offset for channel in group
 * @param group_id Group ID
 * @param channel_index Index in group
 * @param offset Phase offset (0.0 - 1.0)
 * @return ESP_OK on success
 */
esp_err_t pwm_group_set_phase_offset(uint8_t group_id, uint8_t channel_index, float offset);

/**
 * @brief Start candle flicker effect
 * @param channel PWM channel
 * @param params Candle parameters
 * @return ESP_OK on success
 */
esp_err_t pwm_candle_start(uint8_t channel, const pwm_candle_t* params);

/**
 * @brief Update candle effect
 * @param channel PWM channel
 * @param current_time Current time in ms
 * @return ESP_OK on success
 */
esp_err_t pwm_candle_update(uint8_t channel, uint32_t current_time);

/**
 * @brief Start lightning effect
 * @param channel PWM channel
 * @param params Lightning parameters
 * @return ESP_OK on success
 */
esp_err_t pwm_lightning_start(uint8_t channel, const pwm_lightning_t* params);

/**
 * @brief Update lightning effect
 * @param channel PWM channel
 * @param current_time Current time in ms
 * @return ESP_OK on success
 */
esp_err_t pwm_lightning_update(uint8_t channel, uint32_t current_time);

/**
 * @brief Start fade effect
 * @param channel PWM channel
 * @param params Fade parameters
 * @return ESP_OK on success
 */
esp_err_t pwm_fade_start(uint8_t channel, const pwm_fade_t* params);

/**
 * @brief Update fade effect
 * @param channel PWM channel
 * @param current_time Current time in ms
 * @return ESP_OK on success
 */
esp_err_t pwm_fade_update(uint8_t channel, uint32_t current_time);

/**
 * @brief Start color temperature transition
 * @param channels Array of RGBW channels (R, G, B, W)
 * @param params Color temp parameters
 * @return ESP_OK on success
 */
esp_err_t pwm_color_temp_start(const uint8_t* channels, const pwm_color_temp_t* params);

/**
 * @brief Update color temperature effect
 * @param channels Array of RGBW channels
 * @param current_time Current time in ms
 * @return ESP_OK on success
 */
esp_err_t pwm_color_temp_update(const uint8_t* channels, uint32_t current_time);

/**
 * @brief Start automation recording
 * @param channel PWM channel
 * @return ESP_OK on success
 */
esp_err_t pwm_automation_start_recording(uint8_t channel);

/**
 * @brief Stop automation recording
 * @param channel PWM channel
 * @return ESP_OK on success
 */
esp_err_t pwm_automation_stop_recording(uint8_t channel);

/**
 * @brief Add control change to automation
 * @param channel PWM channel
 * @param cc_number MIDI CC number
 * @param value Control value (0.0 - 1.0)
 * @return ESP_OK on success
 */
esp_err_t pwm_automation_add_cc(uint8_t channel, uint8_t cc_number, float value);

/**
 * @brief Playback automation
 * @param channel PWM channel
 * @param current_time Current time in ms
 * @return ESP_OK on success
 */
esp_err_t pwm_automation_playback(uint8_t channel, uint32_t current_time);

#ifdef __cplusplus
}
#endif
