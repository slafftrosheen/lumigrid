#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "effects_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_COMPOSITE_LAYERS 8

// Extended blend modes
typedef enum {
    BLEND_NORMAL = 0,       // Source over destination
    BLEND_ADD,              // Additive
    BLEND_MULTIPLY,         // Multiply
    BLEND_SCREEN,           // Screen
    BLEND_OVERLAY,          // Overlay (combines multiply and screen)
    BLEND_HARD_LIGHT,       // Hard light (intense overlay)
    BLEND_SOFT_LIGHT,       // Soft light (subtle overlay)
    BLEND_COLOR_DODGE,      // Color dodge (brightening)
    BLEND_COLOR_BURN,       // Color burn (darkening)
    BLEND_DIFFERENCE,       // Absolute difference
    BLEND_EXCLUSION,        // Similar to difference but softer
    BLEND_HUE,              // Hue from source, sat/val from dest
    BLEND_SATURATION,       // Saturation from source
    BLEND_COLOR,            // Hue + saturation from source
    BLEND_LUMINOSITY        // Luminosity from source
} blend_mode_t;

// Layer configuration
typedef struct {
    bool enabled;
    const char* effect_name;
    void* effect_state;
    blend_mode_t blend_mode;
    float opacity;          // 0.0 - 1.0
    float speed_multiplier; // Speed modifier for this layer
    uint32_t color_override; // Optional color override (0 = use default)
} composite_layer_t;

// Composite effect structure
typedef struct {
    composite_layer_t layers[MAX_COMPOSITE_LAYERS];
    uint8_t layer_count;
    px_rgba_t* scratch_buffer; // Temporary buffer for layer rendering
    int buffer_size;
} effect_composite_t;

/**
 * @brief Initialize effect compositor
 * @param comp Compositor structure
 * @param buffer_size Number of pixels
 * @return ESP_OK on success
 */
esp_err_t compositor_init(effect_composite_t* comp, int buffer_size);

/**
 * @brief Destroy compositor and free resources
 * @param comp Compositor structure
 */
void compositor_destroy(effect_composite_t* comp);

/**
 * @brief Add a layer to the composite
 * @param comp Compositor structure
 * @param effect_name Name of effect to use
 * @param blend_mode Blend mode for this layer
 * @param opacity Layer opacity (0.0 - 1.0)
 * @return Layer index or -1 on error
 */
int compositor_add_layer(effect_composite_t* comp, const char* effect_name, 
                        blend_mode_t blend_mode, float opacity);

/**
 * @brief Remove a layer
 * @param comp Compositor structure
 * @param layer_index Index of layer to remove
 * @return ESP_OK on success
 */
esp_err_t compositor_remove_layer(effect_composite_t* comp, int layer_index);

/**
 * @brief Set layer opacity
 * @param comp Compositor structure
 * @param layer_index Layer index
 * @param opacity New opacity (0.0 - 1.0)
 * @return ESP_OK on success
 */
esp_err_t compositor_set_layer_opacity(effect_composite_t* comp, int layer_index, float opacity);

/**
 * @brief Set layer blend mode
 * @param comp Compositor structure
 * @param layer_index Layer index
 * @param blend_mode New blend mode
 * @return ESP_OK on success
 */
esp_err_t compositor_set_layer_blend(effect_composite_t* comp, int layer_index, blend_mode_t blend_mode);

/**
 * @brief Enable/disable a layer
 * @param comp Compositor structure
 * @param layer_index Layer index
 * @param enabled Enable state
 * @return ESP_OK on success
 */
esp_err_t compositor_set_layer_enabled(effect_composite_t* comp, int layer_index, bool enabled);

/**
 * @brief Render composite effect
 * @param comp Compositor structure
 * @param output Output pixel buffer
 * @param time_ms Current time in milliseconds
 * @return ESP_OK on success
 */
esp_err_t compositor_render(effect_composite_t* comp, px_rgba_t* output, uint32_t time_ms);

/**
 * @brief Blend two pixels using specified blend mode
 * @param dest Destination pixel (modified in place)
 * @param src Source pixel
 * @param blend Blend mode
 * @param opacity Source opacity (0.0 - 1.0)
 */
void compositor_blend_pixel(px_rgba_t* dest, const px_rgba_t* src, 
                           blend_mode_t blend, float opacity);

#ifdef __cplusplus
}
#endif
