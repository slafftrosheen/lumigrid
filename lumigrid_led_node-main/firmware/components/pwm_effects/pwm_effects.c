#include "pwm_effects.h"
#include "pca9685_driver.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "PWM_EFFECTS";

// Per-channel state
typedef struct {
    bool active;
    waveform_config_t waveform;
    adsr_envelope_t adsr;
    pwm_candle_t candle;
    pwm_lightning_t lightning;
    pwm_fade_t fade;
    pwm_automation_t automation;
} pwm_channel_state_t;

static pwm_channel_state_t s_channels[PWM_MAX_CHANNELS];
static pwm_group_t s_groups[PWM_MAX_GROUPS];
static bool s_initialized = false;

esp_err_t pwm_effects_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    memset(s_channels, 0, sizeof(s_channels));
    memset(s_groups, 0, sizeof(s_groups));
    
    ESP_LOGI(TAG, "PWM effects system initialized");
    s_initialized = true;
    return ESP_OK;
}

// Waveform generation
static float generate_waveform(const waveform_config_t* config) {
    float value = 0.0f;
    
    switch (config->type) {
        case WAVE_SINE:
            value = sinf(config->phase * 2.0f * M_PI);
            break;
        case WAVE_TRIANGLE:
            value = 2.0f * fabsf(2.0f * (config->phase - floorf(config->phase + 0.5f))) - 1.0f;
            break;
        case WAVE_SAWTOOTH:
            value = 2.0f * (config->phase - floorf(config->phase)) - 1.0f;
            break;
        case WAVE_REVERSE_SAWTOOTH:
            value = 1.0f - 2.0f * (config->phase - floorf(config->phase));
            break;
        case WAVE_SQUARE:
            value = (config->phase - floorf(config->phase)) < 0.5f ? 1.0f : -1.0f;
            break;
        case WAVE_PULSE:
            value = (config->phase - floorf(config->phase)) < config->duty_cycle ? 1.0f : -1.0f;
            break;
        case WAVE_EXPONENTIAL:
            value = expf(config->phase * 2.0f) - 1.0f;
            value = value / (expf(2.0f) - 1.0f) * 2.0f - 1.0f;
            break;
        case WAVE_LOGARITHMIC:
            value = logf(1.0f + config->phase * 9.0f) / logf(10.0f) * 2.0f - 1.0f;
            break;
        case WAVE_CUSTOM:
            if (config->lut && config->lut_size > 0) {
                uint16_t index = (uint16_t)(config->phase * config->lut_size) % config->lut_size;
                value = (config->lut[index] / 4095.0f) * 2.0f - 1.0f;
            }
            break;
    }
    
    // Apply amplitude and offset
    return (value * config->amplitude + config->offset);
}

esp_err_t pwm_waveform_create(uint8_t channel, const waveform_config_t* config) {
    if (channel >= PWM_MAX_CHANNELS || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].waveform = *config;
    s_channels[channel].waveform.phase = 0.0f;
    s_channels[channel].active = true;
    
    ESP_LOGI(TAG, "Created waveform on channel %u (type=%d, freq=%.2f Hz)", 
             channel, config->type, config->frequency_hz);
    return ESP_OK;
}

esp_err_t pwm_waveform_update(uint8_t channel, uint32_t delta_ms) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_channels[channel].active) {
        return ESP_OK;
    }
    
    waveform_config_t* wave = &s_channels[channel].waveform;
    
    // Update phase
    float delta_sec = delta_ms / 1000.0f;
    wave->phase += wave->frequency_hz * delta_sec;
    wave->phase = fmodf(wave->phase, 1.0f); // Keep in 0-1 range
    
    // Generate value and set PWM
    float value = generate_waveform(wave);
    float duty = (value + 1.0f) / 2.0f; // Convert -1..1 to 0..1
    duty = fminf(fmaxf(duty, 0.0f), 1.0f); // Clamp
    
    return pca9685_set_duty(channel, duty);
}

esp_err_t pwm_waveform_stop(uint8_t channel) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].active = false;
    return ESP_OK;
}

// ADSR Envelope
esp_err_t pwm_adsr_create(uint8_t channel, const adsr_envelope_t* envelope) {
    if (channel >= PWM_MAX_CHANNELS || !envelope) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].adsr = *envelope;
    s_channels[channel].adsr.gate_on = false;
    
    ESP_LOGI(TAG, "Created ADSR on channel %u", channel);
    return ESP_OK;
}

esp_err_t pwm_adsr_trigger(uint8_t channel) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].adsr.gate_on = true;
    s_channels[channel].adsr.trigger_time = (uint32_t)(esp_timer_get_time() / 1000);
    
    return ESP_OK;
}

esp_err_t pwm_adsr_release(uint8_t channel) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].adsr.gate_on = false;
    s_channels[channel].adsr.release_time = (uint32_t)(esp_timer_get_time() / 1000);
    
    return ESP_OK;
}

float pwm_adsr_calculate(uint8_t channel, uint32_t current_time) {
    if (channel >= PWM_MAX_CHANNELS) {
        return 0.0f;
    }
    
    adsr_envelope_t* env = &s_channels[channel].adsr;
    
    if (!env->gate_on && env->release_time == 0) {
        return 0.0f; // Not triggered yet
    }
    
    if (env->gate_on) {
        // Attack, Decay, Sustain phases
        uint32_t elapsed = current_time - env->trigger_time;
        
        if (elapsed < env->attack_ms) {
            // Attack phase
            return (float)elapsed / env->attack_ms;
        } else if (elapsed < env->attack_ms + env->decay_ms) {
            // Decay phase
            float decay_progress = (float)(elapsed - env->attack_ms) / env->decay_ms;
            return 1.0f - (1.0f - env->sustain_level) * decay_progress;
        } else {
            // Sustain phase
            return env->sustain_level;
        }
    } else {
        // Release phase
        uint32_t elapsed = current_time - env->release_time;
        if (elapsed >= env->release_ms) {
            return 0.0f;
        }
        float release_progress = (float)elapsed / env->release_ms;
        return env->sustain_level * (1.0f - release_progress);
    }
}

// Channel groups
esp_err_t pwm_group_create(uint8_t group_id, const uint8_t* channels, uint8_t count) {
    if (group_id >= PWM_MAX_GROUPS || !channels || count > PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_group_t* group = &s_groups[group_id];
    group->channel_count = count;
    memcpy(group->channels, channels, count);
    group->synchronized = true;
    memset(group->phase_offset, 0, sizeof(group->phase_offset));
    
    ESP_LOGI(TAG, "Created channel group %u with %u channels", group_id, count);
    return ESP_OK;
}

esp_err_t pwm_group_set_phase_offset(uint8_t group_id, uint8_t channel_index, float offset) {
    if (group_id >= PWM_MAX_GROUPS || channel_index >= s_groups[group_id].channel_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_groups[group_id].phase_offset[channel_index] = offset;
    return ESP_OK;
}

// Candle effect
esp_err_t pwm_candle_start(uint8_t channel, const pwm_candle_t* params) {
    if (channel >= PWM_MAX_CHANNELS || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].candle = *params;
    if (s_channels[channel].candle.seed == 0) {
        s_channels[channel].candle.seed = (uint32_t)esp_timer_get_time();
    }
    
    ESP_LOGI(TAG, "Started candle effect on channel %u", channel);
    return ESP_OK;
}

static uint32_t simple_rand(uint32_t* seed) {
    *seed = *seed * 1103515245 + 12345;
    return (*seed / 65536) % 32768;
}

esp_err_t pwm_candle_update(uint8_t channel, uint32_t current_time) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_candle_t* candle = &s_channels[channel].candle;
    
    // Generate flicker using simple random
    float flicker = (simple_rand(&candle->seed) / 32768.0f) * 2.0f - 1.0f;
    flicker *= candle->flicker_amount;
    
    float brightness = candle->base_brightness + flicker;
    brightness = fminf(fmaxf(brightness, 0.0f), 1.0f);
    
    return pca9685_set_duty(channel, brightness);
}

// Lightning effect
esp_err_t pwm_lightning_start(uint8_t channel, const pwm_lightning_t* params) {
    if (channel >= PWM_MAX_CHANNELS || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].lightning = *params;
    s_channels[channel].lightning.active = false;
    s_channels[channel].lightning.current_flash = 0;
    
    ESP_LOGI(TAG, "Started lightning effect on channel %u", channel);
    return ESP_OK;
}

esp_err_t pwm_lightning_update(uint8_t channel, uint32_t current_time) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_lightning_t* lightning = &s_channels[channel].lightning;
    
    if (!lightning->active) {
        // Check if we should trigger
        float rand_val = (esp_timer_get_time() % 1000) / 1000.0f;
        if (rand_val < lightning->trigger_probability) {
            lightning->active = true;
            lightning->current_flash = 0;
            lightning->next_flash_time = current_time;
        } else {
            return pca9685_set_duty(channel, 0.0f);
        }
    }
    
    if (lightning->active && current_time >= lightning->next_flash_time) {
        // Flash!
        if ((current_time - lightning->next_flash_time) < lightning->flash_duration_ms) {
            pca9685_set_duty(channel, 1.0f);
        } else {
            pca9685_set_duty(channel, 0.0f);
            lightning->current_flash++;
            
            if (lightning->current_flash >= lightning->flash_count) {
                lightning->active = false;
            } else {
                lightning->next_flash_time = current_time + lightning->inter_flash_ms;
            }
        }
    }
    
    return ESP_OK;
}

// Fade effect
esp_err_t pwm_fade_start(uint8_t channel, const pwm_fade_t* params) {
    if (channel >= PWM_MAX_CHANNELS || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].fade = *params;
    s_channels[channel].fade.start_time = (uint32_t)(esp_timer_get_time() / 1000);
    s_channels[channel].fade.active = true;
    
    ESP_LOGI(TAG, "Started fade effect on channel %u", channel);
    return ESP_OK;
}

esp_err_t pwm_fade_update(uint8_t channel, uint32_t current_time) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_fade_t* fade = &s_channels[channel].fade;
    if (!fade->active) {
        return ESP_OK;
    }
    
    uint32_t elapsed = current_time - fade->start_time;
    if (elapsed >= fade->duration_ms) {
        pca9685_set_duty(channel, fade->end_brightness);
        fade->active = false;
        return ESP_OK;
    }
    
    float t = (float)elapsed / (float)fade->duration_ms;
    
    // Apply curve (simple linear for now)
    float brightness = fade->start_brightness + 
                      (fade->end_brightness - fade->start_brightness) * t;
    
    return pca9685_set_duty(channel, brightness);
}

// Color temperature conversion (simplified)
static void kelvin_to_rgb(uint16_t kelvin, float* r, float* g, float* b) {
    float temp = kelvin / 100.0f;
    
    // Red
    if (temp <= 66) {
        *r = 1.0f;
    } else {
        *r = powf((temp - 60.0f) / 329.0f, -0.1332f);
    }
    
    // Green
    if (temp <= 66) {
        *g = 0.39f * logf(temp) - 0.634f;
    } else {
        *g = powf((temp - 60.0f) / 288.0f, -0.0755f);
    }
    
    // Blue
    if (temp >= 66) {
        *b = 1.0f;
    } else if (temp <= 19) {
        *b = 0.0f;
    } else {
        *b = 0.543f * logf(temp - 10.0f) - 1.186f;
    }
    
    // Clamp
    *r = fminf(fmaxf(*r, 0.0f), 1.0f);
    *g = fminf(fmaxf(*g, 0.0f), 1.0f);
    *b = fminf(fmaxf(*b, 0.0f), 1.0f);
}

esp_err_t pwm_color_temp_start(const uint8_t* channels, const pwm_color_temp_t* params) {
    if (!channels || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Started color temp transition %uK -> %uK", 
             params->kelvin_start, params->kelvin_end);
    return ESP_OK;
}

esp_err_t pwm_color_temp_update(const uint8_t* channels, uint32_t current_time) {
    // Placeholder implementation
    return ESP_OK;
}

// Automation
esp_err_t pwm_automation_start_recording(uint8_t channel) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_automation_t* auto_data = &s_channels[channel].automation;
    
    // Allocate if needed
    if (!auto_data->events) {
        auto_data->event_capacity = 128;
        auto_data->events = (pwm_control_change_t*)calloc(
            auto_data->event_capacity, sizeof(pwm_control_change_t));
        if (!auto_data->events) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    auto_data->recording = true;
    auto_data->event_count = 0;
    auto_data->start_time_ms = (uint32_t)(esp_timer_get_time() / 1000);
    
    ESP_LOGI(TAG, "Started automation recording on channel %u", channel);
    return ESP_OK;
}

esp_err_t pwm_automation_stop_recording(uint8_t channel) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_channels[channel].automation.recording = false;
    ESP_LOGI(TAG, "Stopped automation recording on channel %u", channel);
    return ESP_OK;
}

esp_err_t pwm_automation_add_cc(uint8_t channel, uint8_t cc_number, float value) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pwm_automation_t* auto_data = &s_channels[channel].automation;
    if (!auto_data->recording || !auto_data->events) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (auto_data->event_count >= auto_data->event_capacity) {
        return ESP_ERR_NO_MEM;
    }
    
    pwm_control_change_t* cc = &auto_data->events[auto_data->event_count++];
    cc->channel = channel;
    cc->cc_number = cc_number;
    cc->value = value;
    cc->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000) - auto_data->start_time_ms;
    
    return ESP_OK;
}

esp_err_t pwm_automation_playback(uint8_t channel, uint32_t current_time) {
    if (channel >= PWM_MAX_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Placeholder - would iterate through events and apply at appropriate times
    return ESP_OK;
}
