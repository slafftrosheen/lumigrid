#include "effects_compositor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char* TAG = "COMPOSITOR";

// Helper to clamp values
static inline uint8_t clamp_u8(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static inline float clamp_f(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// RGB to HSV conversion
static void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float* h, float* s, float* v) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;
    
    float max_val = fmaxf(fmaxf(rf, gf), bf);
    float min_val = fminf(fminf(rf, gf), bf);
    float delta = max_val - min_val;
    
    *v = max_val;
    
    if (max_val == 0.0f) {
        *s = 0.0f;
        *h = 0.0f;
        return;
    }
    
    *s = delta / max_val;
    
    if (delta == 0.0f) {
        *h = 0.0f;
    } else if (max_val == rf) {
        *h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
    } else if (max_val == gf) {
        *h = 60.0f * ((bf - rf) / delta + 2.0f);
    } else {
        *h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
    
    if (*h < 0.0f) *h += 360.0f;
}

// HSV to RGB conversion
static void hsv_to_rgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (s == 0.0f) {
        *r = *g = *b = (uint8_t)(v * 255);
        return;
    }
    
    h = fmodf(h, 360.0f) / 60.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    
    switch (i) {
        case 0: *r = (uint8_t)(v * 255); *g = (uint8_t)(t * 255); *b = (uint8_t)(p * 255); break;
        case 1: *r = (uint8_t)(q * 255); *g = (uint8_t)(v * 255); *b = (uint8_t)(p * 255); break;
        case 2: *r = (uint8_t)(p * 255); *g = (uint8_t)(v * 255); *b = (uint8_t)(t * 255); break;
        case 3: *r = (uint8_t)(p * 255); *g = (uint8_t)(q * 255); *b = (uint8_t)(v * 255); break;
        case 4: *r = (uint8_t)(t * 255); *g = (uint8_t)(p * 255); *b = (uint8_t)(v * 255); break;
        default: *r = (uint8_t)(v * 255); *g = (uint8_t)(p * 255); *b = (uint8_t)(q * 255); break;
    }
}

esp_err_t compositor_init(effect_composite_t* comp, int buffer_size) {
    if (!comp || buffer_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(comp, 0, sizeof(effect_composite_t));
    
    comp->scratch_buffer = (px_rgba_t*)calloc(buffer_size, sizeof(px_rgba_t));
    if (!comp->scratch_buffer) {
        ESP_LOGE(TAG, "Failed to allocate scratch buffer");
        return ESP_ERR_NO_MEM;
    }
    
    comp->buffer_size = buffer_size;
    comp->layer_count = 0;
    
    ESP_LOGI(TAG, "Compositor initialized with buffer size %d", buffer_size);
    return ESP_OK;
}

void compositor_destroy(effect_composite_t* comp) {
    if (!comp) return;
    
    if (comp->scratch_buffer) {
        free(comp->scratch_buffer);
        comp->scratch_buffer = NULL;
    }
    
    // Free effect states
    for (int i = 0; i < comp->layer_count; i++) {
        if (comp->layers[i].effect_state) {
            free(comp->layers[i].effect_state);
        }
    }
}

int compositor_add_layer(effect_composite_t* comp, const char* effect_name, 
                        blend_mode_t blend_mode, float opacity) {
    if (!comp || !effect_name || comp->layer_count >= MAX_COMPOSITE_LAYERS) {
        return -1;
    }
    
    int index = comp->layer_count;
    composite_layer_t* layer = &comp->layers[index];
    
    layer->enabled = true;
    layer->effect_name = effect_name;
    layer->effect_state = NULL; // Will be created on first render
    layer->blend_mode = blend_mode;
    layer->opacity = clamp_f(opacity, 0.0f, 1.0f);
    layer->speed_multiplier = 1.0f;
    layer->color_override = 0;
    
    comp->layer_count++;
    
    ESP_LOGI(TAG, "Added layer %d: %s (blend=%d, opacity=%.2f)", 
             index, effect_name, blend_mode, opacity);
    return index;
}

esp_err_t compositor_remove_layer(effect_composite_t* comp, int layer_index) {
    if (!comp || layer_index < 0 || layer_index >= comp->layer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Free effect state
    if (comp->layers[layer_index].effect_state) {
        free(comp->layers[layer_index].effect_state);
    }
    
    // Shift remaining layers
    for (int i = layer_index; i < comp->layer_count - 1; i++) {
        comp->layers[i] = comp->layers[i + 1];
    }
    
    comp->layer_count--;
    return ESP_OK;
}

esp_err_t compositor_set_layer_opacity(effect_composite_t* comp, int layer_index, float opacity) {
    if (!comp || layer_index < 0 || layer_index >= comp->layer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    comp->layers[layer_index].opacity = clamp_f(opacity, 0.0f, 1.0f);
    return ESP_OK;
}

esp_err_t compositor_set_layer_blend(effect_composite_t* comp, int layer_index, blend_mode_t blend_mode) {
    if (!comp || layer_index < 0 || layer_index >= comp->layer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    comp->layers[layer_index].blend_mode = blend_mode;
    return ESP_OK;
}

esp_err_t compositor_set_layer_enabled(effect_composite_t* comp, int layer_index, bool enabled) {
    if (!comp || layer_index < 0 || layer_index >= comp->layer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    comp->layers[layer_index].enabled = enabled;
    return ESP_OK;
}

void compositor_blend_pixel(px_rgba_t* dest, const px_rgba_t* src, 
                           blend_mode_t blend, float opacity) {
    if (opacity <= 0.0f) return;
    if (opacity > 1.0f) opacity = 1.0f;
    
    float src_r = src->r / 255.0f;
    float src_g = src->g / 255.0f;
    float src_b = src->b / 255.0f;
    
    float dest_r = dest->r / 255.0f;
    float dest_g = dest->g / 255.0f;
    float dest_b = dest->b / 255.0f;
    
    float out_r, out_g, out_b;
    
    switch (blend) {
        case BLEND_NORMAL:
            out_r = src_r * opacity + dest_r * (1.0f - opacity);
            out_g = src_g * opacity + dest_g * (1.0f - opacity);
            out_b = src_b * opacity + dest_b * (1.0f - opacity);
            break;
            
        case BLEND_ADD:
            out_r = fminf(dest_r + src_r * opacity, 1.0f);
            out_g = fminf(dest_g + src_g * opacity, 1.0f);
            out_b = fminf(dest_b + src_b * opacity, 1.0f);
            break;
            
        case BLEND_MULTIPLY:
            out_r = dest_r * (1.0f + (src_r - 1.0f) * opacity);
            out_g = dest_g * (1.0f + (src_g - 1.0f) * opacity);
            out_b = dest_b * (1.0f + (src_b - 1.0f) * opacity);
            break;
            
        case BLEND_SCREEN:
            out_r = 1.0f - (1.0f - dest_r) * (1.0f - src_r * opacity);
            out_g = 1.0f - (1.0f - dest_g) * (1.0f - src_g * opacity);
            out_b = 1.0f - (1.0f - dest_b) * (1.0f - src_b * opacity);
            break;
            
        case BLEND_OVERLAY: {
            float blend_channel = dest_r < 0.5f ? 
                2.0f * dest_r * src_r : 
                1.0f - 2.0f * (1.0f - dest_r) * (1.0f - src_r);
            out_r = dest_r * (1.0f - opacity) + blend_channel * opacity;
            
            blend_channel = dest_g < 0.5f ? 
                2.0f * dest_g * src_g : 
                1.0f - 2.0f * (1.0f - dest_g) * (1.0f - src_g);
            out_g = dest_g * (1.0f - opacity) + blend_channel * opacity;
            
            blend_channel = dest_b < 0.5f ? 
                2.0f * dest_b * src_b : 
                1.0f - 2.0f * (1.0f - dest_b) * (1.0f - src_b);
            out_b = dest_b * (1.0f - opacity) + blend_channel * opacity;
            break;
        }
        
        case BLEND_DIFFERENCE:
            out_r = dest_r + (fabsf(dest_r - src_r) - dest_r) * opacity;
            out_g = dest_g + (fabsf(dest_g - src_g) - dest_g) * opacity;
            out_b = dest_b + (fabsf(dest_b - src_b) - dest_b) * opacity;
            break;
            
        case BLEND_HUE: {
            float src_h, src_s, src_v;
            float dest_h, dest_s, dest_v;
            rgb_to_hsv(src->r, src->g, src->b, &src_h, &src_s, &src_v);
            rgb_to_hsv(dest->r, dest->g, dest->b, &dest_h, &dest_s, &dest_v);
            
            float blend_h = dest_h * (1.0f - opacity) + src_h * opacity;
            uint8_t r, g, b;
            hsv_to_rgb(blend_h, dest_s, dest_v, &r, &g, &b);
            out_r = r / 255.0f;
            out_g = g / 255.0f;
            out_b = b / 255.0f;
            break;
        }
        
        case BLEND_SATURATION: {
            float src_h, src_s, src_v;
            float dest_h, dest_s, dest_v;
            rgb_to_hsv(src->r, src->g, src->b, &src_h, &src_s, &src_v);
            rgb_to_hsv(dest->r, dest->g, dest->b, &dest_h, &dest_s, &dest_v);
            
            float blend_s = dest_s * (1.0f - opacity) + src_s * opacity;
            uint8_t r, g, b;
            hsv_to_rgb(dest_h, blend_s, dest_v, &r, &g, &b);
            out_r = r / 255.0f;
            out_g = g / 255.0f;
            out_b = b / 255.0f;
            break;
        }
        
        default:
            // Fallback to normal blend
            out_r = src_r * opacity + dest_r * (1.0f - opacity);
            out_g = src_g * opacity + dest_g * (1.0f - opacity);
            out_b = src_b * opacity + dest_b * (1.0f - opacity);
            break;
    }
    
    dest->r = clamp_u8((int)(out_r * 255));
    dest->g = clamp_u8((int)(out_g * 255));
    dest->b = clamp_u8((int)(out_b * 255));
}

esp_err_t compositor_render(effect_composite_t* comp, px_rgba_t* output, uint32_t time_ms) {
    if (!comp || !output) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clear output
    memset(output, 0, comp->buffer_size * sizeof(px_rgba_t));
    
    // Render and blend each layer
    for (int i = 0; i < comp->layer_count; i++) {
        composite_layer_t* layer = &comp->layers[i];
        
        if (!layer->enabled) continue;
        
        // Clear scratch buffer
        memset(comp->scratch_buffer, 0, comp->buffer_size * sizeof(px_rgba_t));
        
        // Render layer effect to scratch buffer
        // Note: This is a placeholder - actual effect rendering would be done here
        // In a full implementation, we'd look up the effect by name and call its render function
        
        // Blend scratch buffer into output
        for (int p = 0; p < comp->buffer_size; p++) {
            compositor_blend_pixel(&output[p], &comp->scratch_buffer[p], 
                                  layer->blend_mode, layer->opacity);
        }
    }
    
    return ESP_OK;
}
