#include "effects_registry.h"
#include "effect_pixels.h"
#include "effects.h"
#include <math.h>
#include <string.h>

// ===== Utility Functions =====

static inline uint8_t scale8(uint8_t val, uint8_t scale) {
    return ((uint16_t)val * (uint16_t)scale) >> 8;
}

// Simple Perlin-style noise function
static float noise1d(int x, uint32_t seed) {
    x = (x << 13) ^ x;
    int n = (x * (x * x * 15731 + 789221) + seed) & 0x7fffffff;
    return 1.0f - (n / 1073741824.0f);
}

static float smoothnoise1d(float x, uint32_t seed) {
    int int_x = (int)x;
    float frac_x = x - int_x;
    
    float v1 = noise1d(int_x, seed);
    float v2 = noise1d(int_x + 1, seed);
    
    // Cosine interpolation
    float ft = frac_x * 3.14159f;
    float f = (1.0f - cosf(ft)) * 0.5f;
    
    return v1 * (1.0f - f) + v2 * f;
}

// 2D Perlin noise
static float noise2d(int x, int y, uint32_t seed) {
    int n = x + y * 57;
    n = (n << 13) ^ n;
    int nn = (n * (n * n * 15731 + 789221) + seed) & 0x7fffffff;
    return 1.0f - (nn / 1073741824.0f);
}

static float smoothnoise2d(float x, float y, uint32_t seed) {
    int int_x = (int)x;
    int int_y = (int)y;
    float frac_x = x - int_x;
    float frac_y = y - int_y;
    
    float v1 = noise2d(int_x, int_y, seed);
    float v2 = noise2d(int_x + 1, int_y, seed);
    float v3 = noise2d(int_x, int_y + 1, seed);
    float v4 = noise2d(int_x + 1, int_y + 1, seed);
    
    // Cosine interpolation
    float ft_x = frac_x * 3.14159f;
    float f_x = (1.0f - cosf(ft_x)) * 0.5f;
    
    float ft_y = frac_y * 3.14159f;
    float f_y = (1.0f - cosf(ft_y)) * 0.5f;
    
    float i1 = v1 * (1.0f - f_x) + v2 * f_x;
    float i2 = v3 * (1.0f - f_x) + v4 * f_x;
    
    return i1 * (1.0f - f_y) + i2 * f_y;
}

static void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:
        case 5: *r = v; *g = p; *b = q; break;
    }
}

// ===== PERLIN FLOW =====
static void fx_perlin_flow(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    float intensity = params ? params->intensity : 0.5f;
    
    int npx = ctx->count;
    uint32_t seed = 12345;
    
    float time_scale = speed * 0.01f;
    float spatial_scale = intensity * 0.1f;
    
    for (int i = 0; i < npx; i++) {
        float x = i * spatial_scale;
        float t = ctx->now_ms * time_scale;
        
        float noise_val = smoothnoise1d(x + t, seed);
        noise_val = (noise_val + 1.0f) / 2.0f; // Normalize to 0-1
        
        uint8_t hue = (uint8_t)(noise_val * 255);
        hsv2rgb(hue, 255, 255, &ctx->framebuf[i].r, 
                &ctx->framebuf[i].g, &ctx->framebuf[i].b);
    }
}

// ===== PLASMA =====
static void fx_plasma(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    float t = ctx->now_ms * speed * 0.001f;
    
    for (int i = 0; i < npx; i++) {
        float x = (float)i / npx * 10.0f;
        
        // Multiple interference patterns
        float v = sinf(x + t);
        v += sinf(x * 2.0f - t * 0.5f);
        v += cosf(x * 3.0f + t * 0.3f);
        v += cosf(x * 0.5f - t * 0.7f);
        
        // Normalize to 0-1
        v = (v + 4.0f) / 8.0f;
        
        uint8_t hue = (uint8_t)(v * 255);
        hsv2rgb(hue, 255, 255, &ctx->framebuf[i].r, 
                &ctx->framebuf[i].g, &ctx->framebuf[i].b);
    }
}

// ===== LAVA LAMP =====
static void fx_lava_lamp(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0xFF4500;
    
    int npx = ctx->count;
    
    uint8_t base_r = (color >> 16) & 0xFF;
    uint8_t base_g = (color >> 8) & 0xFF;
    uint8_t base_b = color & 0xFF;
    
    float t = ctx->now_ms * speed * 0.0005f;
    uint32_t seed = 54321;
    
    for (int i = 0; i < npx; i++) {
        float x = (float)i / npx * 5.0f;
        
        // Multiple octaves of noise
        float noise_val = smoothnoise2d(x, t, seed) * 0.5f;
        noise_val += smoothnoise2d(x * 2.0f, t * 1.5f, seed + 1) * 0.25f;
        noise_val += smoothnoise2d(x * 4.0f, t * 2.0f, seed + 2) * 0.125f;
        
        noise_val = (noise_val + 1.0f) / 2.0f; // Normalize
        
        uint8_t brightness = (uint8_t)(noise_val * 255);
        ctx->framebuf[i].r = scale8(base_r, brightness);
        ctx->framebuf[i].g = scale8(base_g, brightness);
        ctx->framebuf[i].b = scale8(base_b, brightness);
    }
}

// ===== CLOUDS =====
static void fx_clouds(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    float t = ctx->now_ms * speed * 0.0003f;
    uint32_t seed = 98765;
    
    for (int i = 0; i < npx; i++) {
        float x = (float)i / npx * 3.0f;
        
        // Fractal noise (multiple octaves)
        float noise_val = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        
        for (int octave = 0; octave < 4; octave++) {
            noise_val += smoothnoise2d(x * frequency, t * frequency, seed + octave) * amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        
        noise_val = (noise_val + 2.0f) / 4.0f; // Normalize
        
        // Map to white/gray colors
        uint8_t brightness = (uint8_t)(noise_val * 255);
        ctx->framebuf[i].r = brightness;
        ctx->framebuf[i].g = brightness;
        ctx->framebuf[i].b = brightness;
    }
}

// ===== MARBLE =====
static void fx_marble(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    float t = ctx->now_ms * speed * 0.0002f;
    uint32_t seed = 11111;
    
    for (int i = 0; i < npx; i++) {
        float x = (float)i / npx * 8.0f;
        
        // Turbulence function
        float turbulence = 0.0f;
        float size = 1.0f;
        for (int j = 0; j < 3; j++) {
            turbulence += fabsf(smoothnoise2d(x * size, t * size, seed + j)) / size;
            size *= 2.0f;
        }
        
        // Sine pattern with turbulence
        float pattern = sinf((x + turbulence) * 2.0f) * 0.5f + 0.5f;
        
        uint8_t brightness = (uint8_t)(pattern * 255);
        
        // Marble colors (white with dark veins)
        ctx->framebuf[i].r = brightness;
        ctx->framebuf[i].g = scale8(brightness, 240);
        ctx->framebuf[i].b = scale8(brightness, 230);
    }
}

// ===== ELECTRIC (Lightning paths) =====
static void fx_electric(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0x00FFFF;
    
    int npx = ctx->count;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Clear with fade
    for (int i = 0; i < npx; i++) {
        ctx->framebuf[i].r = scale8(ctx->framebuf[i].r, 200);
        ctx->framebuf[i].g = scale8(ctx->framebuf[i].g, 200);
        ctx->framebuf[i].b = scale8(ctx->framebuf[i].b, 200);
    }
    
    // Generate random lightning path
    uint32_t seed = (ctx->now_ms * (uint32_t)speed) / 100;
    int start_pos = (seed * 71) % npx;
    int branch_len = 5 + (seed % 10);
    
    for (int i = 0; i < branch_len && start_pos + i < npx; i++) {
        int pos = start_pos + i;
        // Random walk
        int offset = ((seed + i) * 137) % 3 - 1; // -1, 0, or 1
        pos += offset;
        if (pos >= 0 && pos < npx) {
            ctx->framebuf[pos].r = r;
            ctx->framebuf[pos].g = g;
            ctx->framebuf[pos].b = b;
        }
    }
}

// ===== CELLULAR AUTOMATA (Game of Life-inspired) =====
static uint8_t s_life_state[256]; // Static buffer for cellular automata
static uint32_t s_life_last_update = 0;

static void fx_cellular(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    if (npx > 256) return;
    
    uint32_t update_interval = (uint32_t)(200.0f / speed);
    
    // Initialize on first run or reset
    if (s_life_last_update == 0 || ctx->now_ms - s_life_last_update > 10000) {
        for (int i = 0; i < npx; i++) {
            s_life_state[i] = ((ctx->now_ms + i) % 3 == 0) ? 1 : 0;
        }
        s_life_last_update = ctx->now_ms;
    }
    
    // Update automata
    if (ctx->now_ms - s_life_last_update >= update_interval) {
        uint8_t new_state[256];
        
        for (int i = 0; i < npx; i++) {
            // Count neighbors (wrap around)
            int left = (i - 1 + npx) % npx;
            int right = (i + 1) % npx;
            int neighbors = s_life_state[left] + s_life_state[right];
            
            // Simple rules
            if (s_life_state[i]) {
                new_state[i] = (neighbors == 1) ? 1 : 0;
            } else {
                new_state[i] = (neighbors == 1) ? 1 : 0;
            }
        }
        
        memcpy(s_life_state, new_state, npx);
        s_life_last_update = ctx->now_ms;
    }
    
    // Render
    for (int i = 0; i < npx; i++) {
        if (s_life_state[i]) {
            uint8_t hue = (uint8_t)((i * 256 / npx) & 0xFF);
            hsv2rgb(hue, 255, 255, &ctx->framebuf[i].r, 
                    &ctx->framebuf[i].g, &ctx->framebuf[i].b);
        } else {
            ctx->framebuf[i].r = 0;
            ctx->framebuf[i].g = 0;
            ctx->framebuf[i].b = 0;
        }
    }
}

// ===== Registration =====
void fx_noise_register_all(void) {
    fx_register("perlin_flow", fx_perlin_flow);
    fx_register("plasma", fx_plasma);
    fx_register("lava_lamp", fx_lava_lamp);
    fx_register("clouds", fx_clouds);
    fx_register("marble", fx_marble);
    fx_register("electric", fx_electric);
    fx_register("cellular", fx_cellular);
}
