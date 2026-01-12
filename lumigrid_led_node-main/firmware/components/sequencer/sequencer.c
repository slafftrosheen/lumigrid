#include "sequencer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char* TAG = "SEQUENCER";

// Static sequencer state
static bool s_initialized = false;
static sequence_t* s_active_sequence = NULL;
static playback_state_t* s_active_playback = NULL;

esp_err_t sequencer_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing sequencer system");
    s_initialized = true;
    return ESP_OK;
}

sequence_t* sequencer_create_sequence(const char* name, uint16_t bpm) {
    if (!name) {
        ESP_LOGE(TAG, "Invalid sequence name");
        return NULL;
    }
    
    sequence_t* seq = (sequence_t*)calloc(1, sizeof(sequence_t));
    if (!seq) {
        ESP_LOGE(TAG, "Failed to allocate sequence");
        return NULL;
    }
    
    static uint32_t next_id = 1;
    seq->sequence_id = next_id++;
    strncpy(seq->name, name, sizeof(seq->name) - 1);
    seq->name[sizeof(seq->name) - 1] = '\0';
    seq->bpm = bpm > 0 ? bpm : 120;
    seq->time_signature = 0x44; // 4/4 time
    seq->track_count = 0;
    seq->total_duration_ms = 0;
    
    // Allocate initial event array
    seq->event_capacity = 32;
    seq->events = (timeline_event_t*)calloc(seq->event_capacity, sizeof(timeline_event_t));
    if (!seq->events) {
        ESP_LOGE(TAG, "Failed to allocate events");
        free(seq);
        return NULL;
    }
    seq->event_count = 0;
    
    ESP_LOGI(TAG, "Created sequence '%s' (ID: %lu, BPM: %u)", seq->name, seq->sequence_id, seq->bpm);
    return seq;
}

void sequencer_destroy_sequence(sequence_t* seq) {
    if (!seq) return;
    
    // Free event data
    for (size_t i = 0; i < seq->event_count; i++) {
        if (seq->events[i].event_data) {
            free(seq->events[i].event_data);
        }
    }
    
    if (seq->events) {
        free(seq->events);
    }
    
    free(seq);
}

int sequencer_add_track(sequence_t* seq, const char* name, uint8_t channel) {
    if (!seq || !name) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    
    if (seq->track_count >= SEQUENCER_MAX_TRACKS) {
        ESP_LOGE(TAG, "Maximum tracks reached");
        return -1;
    }
    
    if (channel >= 16) {
        ESP_LOGE(TAG, "Invalid channel: %u", channel);
        return -1;
    }
    
    track_t* track = &seq->tracks[seq->track_count];
    track->id = seq->track_count;
    strncpy(track->name, name, sizeof(track->name) - 1);
    track->name[sizeof(track->name) - 1] = '\0';
    track->enabled = true;
    track->channel = channel;
    track->volume = 1.0f;
    track->muted = false;
    track->solo = false;
    
    ESP_LOGI(TAG, "Added track %d: '%s' (channel %u)", seq->track_count, track->name, channel);
    return seq->track_count++;
}

esp_err_t sequencer_add_event(sequence_t* seq, uint32_t timestamp_ms, 
                               uint32_t duration_ms, uint8_t track_id,
                               event_type_t type, const void* data, size_t data_size) {
    if (!seq) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (track_id >= seq->track_count) {
        ESP_LOGE(TAG, "Invalid track ID: %u", track_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Expand event array if needed
    if (seq->event_count >= seq->event_capacity) {
        size_t new_capacity = seq->event_capacity * 2;
        if (new_capacity > SEQUENCER_MAX_EVENTS) {
            new_capacity = SEQUENCER_MAX_EVENTS;
        }
        
        if (seq->event_count >= new_capacity) {
            ESP_LOGE(TAG, "Maximum events reached");
            return ESP_ERR_NO_MEM;
        }
        
        timeline_event_t* new_events = (timeline_event_t*)realloc(
            seq->events, new_capacity * sizeof(timeline_event_t));
        if (!new_events) {
            ESP_LOGE(TAG, "Failed to expand event array");
            return ESP_ERR_NO_MEM;
        }
        
        seq->events = new_events;
        seq->event_capacity = new_capacity;
    }
    
    // Add event
    timeline_event_t* evt = &seq->events[seq->event_count];
    evt->timestamp_ms = timestamp_ms;
    evt->duration_ms = duration_ms;
    evt->track_id = track_id;
    evt->event_type = type;
    
    // Copy event data if provided
    if (data && data_size > 0) {
        evt->event_data = malloc(data_size);
        if (!evt->event_data) {
            ESP_LOGE(TAG, "Failed to allocate event data");
            return ESP_ERR_NO_MEM;
        }
        memcpy(evt->event_data, data, data_size);
    } else {
        evt->event_data = NULL;
    }
    
    seq->event_count++;
    
    // Update total duration
    uint32_t event_end = timestamp_ms + duration_ms;
    if (event_end > seq->total_duration_ms) {
        seq->total_duration_ms = event_end;
    }
    
    return ESP_OK;
}

esp_err_t sequencer_remove_event(sequence_t* seq, size_t event_index) {
    if (!seq || event_index >= seq->event_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Free event data
    if (seq->events[event_index].event_data) {
        free(seq->events[event_index].event_data);
    }
    
    // Shift remaining events
    if (event_index < seq->event_count - 1) {
        memmove(&seq->events[event_index], &seq->events[event_index + 1],
                (seq->event_count - event_index - 1) * sizeof(timeline_event_t));
    }
    
    seq->event_count--;
    return ESP_OK;
}

uint32_t sequencer_snap_to_grid(uint32_t timestamp_ms, quantization_t quant, uint16_t bpm) {
    if (quant == QUANT_NONE || bpm == 0) {
        return timestamp_ms;
    }
    
    // Calculate quarter note duration in ms
    float quarter_note_ms = (60000.0f / bpm);
    
    // Calculate grid size based on quantization
    float grid_ms = quarter_note_ms;
    switch (quant) {
        case QUANT_1_8:  grid_ms = quarter_note_ms / 2.0f; break;
        case QUANT_1_16: grid_ms = quarter_note_ms / 4.0f; break;
        case QUANT_1_32: grid_ms = quarter_note_ms / 8.0f; break;
        default: break;
    }
    
    // Snap to nearest grid line
    uint32_t grid_count = (uint32_t)roundf(timestamp_ms / grid_ms);
    return (uint32_t)(grid_count * grid_ms);
}

float sequencer_interpolate_automation(const keyframe_t* kf_start, 
                                       const keyframe_t* kf_end,
                                       uint32_t current_time,
                                       uint32_t start_time,
                                       uint32_t end_time) {
    if (!kf_start || !kf_end || end_time <= start_time) {
        return kf_start ? kf_start->value : 0.0f;
    }
    
    // Calculate normalized time (0.0 to 1.0)
    float t = (float)(current_time - start_time) / (float)(end_time - start_time);
    t = fminf(fmaxf(t, 0.0f), 1.0f); // Clamp
    
    // Apply curve
    float curved_t = t;
    switch ((curve_type_t)kf_start->curve) {
        case CURVE_LINEAR:
            curved_t = t;
            break;
        case CURVE_EASE_IN:
            curved_t = t * t;
            break;
        case CURVE_EASE_OUT:
            curved_t = t * (2.0f - t);
            break;
        case CURVE_EASE_IN_OUT:
            curved_t = t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
            break;
        case CURVE_S_CURVE:
            curved_t = (sinf((t - 0.5f) * 3.14159f) + 1.0f) / 2.0f;
            break;
    }
    
    // Linear interpolation with curved time
    return kf_start->value + (kf_end->value - kf_start->value) * curved_t;
}

playback_state_t* sequencer_create_playback(sequence_t* seq) {
    if (!seq) {
        ESP_LOGE(TAG, "Invalid sequence");
        return NULL;
    }
    
    playback_state_t* state = (playback_state_t*)calloc(1, sizeof(playback_state_t));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate playback state");
        return NULL;
    }
    
    state->sequence = seq;
    state->playhead_ms = 0;
    state->playing = false;
    state->looping = false;
    state->loop_start_ms = 0;
    state->loop_end_ms = seq->total_duration_ms;
    state->tempo_multiplier = 1.0f;
    state->last_update_ms = 0;
    
    ESP_LOGI(TAG, "Created playback state for sequence '%s'", seq->name);
    return state;
}

void sequencer_destroy_playback(playback_state_t* state) {
    if (state) {
        free(state);
    }
}

esp_err_t sequencer_play(playback_state_t* state) {
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    state->playing = true;
    state->last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);
    
    ESP_LOGI(TAG, "Starting playback");
    return ESP_OK;
}

esp_err_t sequencer_pause(playback_state_t* state) {
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    state->playing = false;
    ESP_LOGI(TAG, "Pausing playback");
    return ESP_OK;
}

esp_err_t sequencer_stop(playback_state_t* state) {
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    state->playing = false;
    state->playhead_ms = 0;
    ESP_LOGI(TAG, "Stopping playback");
    return ESP_OK;
}

esp_err_t sequencer_update(playback_state_t* state, uint32_t delta_ms) {
    if (!state || !state->sequence) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!state->playing) {
        return ESP_OK;
    }
    
    // Apply tempo multiplier
    uint32_t scaled_delta = (uint32_t)(delta_ms * state->tempo_multiplier);
    state->playhead_ms += scaled_delta;
    
    // Handle looping
    if (state->looping) {
        if (state->playhead_ms >= state->loop_end_ms) {
            state->playhead_ms = state->loop_start_ms + 
                                (state->playhead_ms - state->loop_end_ms);
        }
    } else {
        // Stop at end
        if (state->playhead_ms >= state->sequence->total_duration_ms) {
            state->playhead_ms = state->sequence->total_duration_ms;
            state->playing = false;
        }
    }
    
    // Process events at current playhead position
    // (This is where we would trigger effects, keyframes, etc.)
    // For now, just a placeholder
    
    return ESP_OK;
}

esp_err_t sequencer_seek(playback_state_t* state, uint32_t time_ms) {
    if (!state || !state->sequence) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (time_ms > state->sequence->total_duration_ms) {
        time_ms = state->sequence->total_duration_ms;
    }
    
    state->playhead_ms = time_ms;
    ESP_LOGI(TAG, "Seeking to %lu ms", time_ms);
    return ESP_OK;
}

esp_err_t sequencer_export_json(const sequence_t* seq, char** json_out) {
    if (!seq || !json_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddNumberToObject(root, "id", seq->sequence_id);
    cJSON_AddStringToObject(root, "name", seq->name);
    cJSON_AddNumberToObject(root, "duration_ms", seq->total_duration_ms);
    cJSON_AddNumberToObject(root, "bpm", seq->bpm);
    cJSON_AddNumberToObject(root, "time_signature", seq->time_signature);
    
    // Add tracks
    cJSON* tracks = cJSON_CreateArray();
    for (uint8_t i = 0; i < seq->track_count; i++) {
        cJSON* track = cJSON_CreateObject();
        cJSON_AddNumberToObject(track, "id", seq->tracks[i].id);
        cJSON_AddStringToObject(track, "name", seq->tracks[i].name);
        cJSON_AddBoolToObject(track, "enabled", seq->tracks[i].enabled);
        cJSON_AddNumberToObject(track, "channel", seq->tracks[i].channel);
        cJSON_AddNumberToObject(track, "volume", seq->tracks[i].volume);
        cJSON_AddBoolToObject(track, "muted", seq->tracks[i].muted);
        cJSON_AddBoolToObject(track, "solo", seq->tracks[i].solo);
        cJSON_AddItemToArray(tracks, track);
    }
    cJSON_AddItemToObject(root, "tracks", tracks);
    
    // Add events
    cJSON* events = cJSON_CreateArray();
    for (size_t i = 0; i < seq->event_count; i++) {
        cJSON* event = cJSON_CreateObject();
        cJSON_AddNumberToObject(event, "timestamp_ms", seq->events[i].timestamp_ms);
        cJSON_AddNumberToObject(event, "duration_ms", seq->events[i].duration_ms);
        cJSON_AddNumberToObject(event, "track_id", seq->events[i].track_id);
        cJSON_AddNumberToObject(event, "type", seq->events[i].event_type);
        // Note: event_data is not serialized in this basic implementation
        cJSON_AddItemToArray(events, event);
    }
    cJSON_AddItemToObject(root, "events", events);
    
    *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!*json_out) {
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

sequence_t* sequencer_import_json(const char* json_str) {
    if (!json_str) {
        return NULL;
    }
    
    cJSON* root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return NULL;
    }
    
    cJSON* name_obj = cJSON_GetObjectItem(root, "name");
    cJSON* bpm_obj = cJSON_GetObjectItem(root, "bpm");
    
    if (!name_obj || !cJSON_IsString(name_obj) || !bpm_obj || !cJSON_IsNumber(bpm_obj)) {
        ESP_LOGE(TAG, "Invalid JSON format");
        cJSON_Delete(root);
        return NULL;
    }
    
    sequence_t* seq = sequencer_create_sequence(name_obj->valuestring, (uint16_t)bpm_obj->valueint);
    if (!seq) {
        cJSON_Delete(root);
        return NULL;
    }
    
    // Import tracks
    cJSON* tracks = cJSON_GetObjectItem(root, "tracks");
    if (tracks && cJSON_IsArray(tracks)) {
        cJSON* track = NULL;
        cJSON_ArrayForEach(track, tracks) {
            cJSON* track_name = cJSON_GetObjectItem(track, "name");
            cJSON* channel = cJSON_GetObjectItem(track, "channel");
            if (track_name && cJSON_IsString(track_name) && 
                channel && cJSON_IsNumber(channel)) {
                sequencer_add_track(seq, track_name->valuestring, (uint8_t)channel->valueint);
            }
        }
    }
    
    // Import events (basic implementation)
    cJSON* events = cJSON_GetObjectItem(root, "events");
    if (events && cJSON_IsArray(events)) {
        cJSON* event = NULL;
        cJSON_ArrayForEach(event, events) {
            cJSON* timestamp = cJSON_GetObjectItem(event, "timestamp_ms");
            cJSON* duration = cJSON_GetObjectItem(event, "duration_ms");
            cJSON* track_id = cJSON_GetObjectItem(event, "track_id");
            cJSON* type = cJSON_GetObjectItem(event, "type");
            
            if (timestamp && cJSON_IsNumber(timestamp) &&
                duration && cJSON_IsNumber(duration) &&
                track_id && cJSON_IsNumber(track_id) &&
                type && cJSON_IsNumber(type)) {
                sequencer_add_event(seq, 
                                   (uint32_t)timestamp->valueint,
                                   (uint32_t)duration->valueint,
                                   (uint8_t)track_id->valueint,
                                   (event_type_t)type->valueint,
                                   NULL, 0);
            }
        }
    }
    
    cJSON_Delete(root);
    return seq;
}
