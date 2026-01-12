#include "effects_registry.h"
#include "effect_pixels.h"
#include "effects.h"
#include "sync.h"
#include <math.h>
#include <string.h>

// ===== Utility Functions =====

static inline uint8_t qadd8(uint8_t a, uint8_t b) {
    uint16_t s = a + b;
    return (s > 255) ? 255 : (uint8_t)s;
}

static inline uint8_t qsub8(uint8_t a, uint8_t b) {
    return (a > b) ? (a - b) : 0;
}

static inline uint8_t scale8(uint8_t val, uint8_t scale) {
    return ((uint16_t)val * (uint16_t)scale) >> 8;
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

// ===== SCANNER (Larson Scanner / KITT) =====
static void fx_scanner(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0xFF0000;
    float intensity = params ? params->intensity : 0.8f;
    
    int npx = ctx->count;
    if (npx <= 1) return;
    
    // Calculate position
    uint32_t cycle_ms = (uint32_t)(2000.0f / speed); // Full back-and-forth cycle
    uint32_t pos_in_cycle = ctx->now_ms % cycle_ms;
    float t = (float)pos_in_cycle / cycle_ms; // 0.0 to 1.0
    
    // Triangle wave for back-and-forth motion
    float position;
    if (t < 0.5f) {
        position = t * 2.0f; // 0 to 1
    } else {
        position = 2.0f - t * 2.0f; // 1 to 0
    }
    
    int center = (int)(position * (npx - 1));
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Clear buffer
    memset(ctx->framebuf, 0, npx * sizeof(px_rgba_t));
    
    // Draw scanner with trailing fade
    int trail_len = (int)(npx * intensity * 0.3f);
    if (trail_len < 2) trail_len = 2;
    
    for (int i = 0; i < npx; i++) {
        int dist = abs(i - center);
        if (dist < trail_len) {
            uint8_t brightness = 255 - (dist * 255 / trail_len);
            ctx->framebuf[i].r = scale8(r, brightness);
            ctx->framebuf[i].g = scale8(g, brightness);
            ctx->framebuf[i].b = scale8(b, brightness);
        }
    }
}

// ===== COMET =====
static void fx_comet(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0x00FFFF;
    float intensity = params ? params->intensity : 0.7f;
    
    int npx = ctx->count;
    if (npx <= 1) return;
    
    // Calculate head position
    uint32_t cycle_ms = (uint32_t)(3000.0f / speed);
    uint32_t pos_in_cycle = ctx->now_ms % cycle_ms;
    float t = (float)pos_in_cycle / cycle_ms;
    
    int head = (int)(t * npx);
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Clear buffer
    memset(ctx->framebuf, 0, npx * sizeof(px_rgba_t));
    
    // Draw comet with exponential decay trail
    int trail_len = (int)(npx * intensity * 0.5f);
    if (trail_len < 5) trail_len = 5;
    
    for (int i = 0; i < trail_len && head - i >= 0; i++) {
        int idx = head - i;
        if (idx >= 0 && idx < npx) {
            float decay = expf(-3.0f * i / trail_len);
            uint8_t brightness = (uint8_t)(255 * decay);
            ctx->framebuf[idx].r = scale8(r, brightness);
            ctx->framebuf[idx].g = scale8(g, brightness);
            ctx->framebuf[idx].b = scale8(b, brightness);
        }
    }
}

// ===== THEATER CHASE =====
static void fx_theater_chase(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0xFF00FF;
    
    int npx = ctx->count;
    int spacing = 3; // Pixels between lit pixels
    
    uint32_t cycle_ms = (uint32_t)(1000.0f / speed);
    uint32_t offset = (ctx->now_ms / cycle_ms) % spacing;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Clear buffer
    memset(ctx->framebuf, 0, npx * sizeof(px_rgba_t));
    
    // Light every Nth pixel with offset
    for (int i = offset; i < npx; i += spacing) {
        ctx->framebuf[i].r = r;
        ctx->framebuf[i].g = g;
        ctx->framebuf[i].b = b;
    }
}

// ===== RUNNING LIGHTS =====
static void fx_running_lights(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    
    uint32_t cycle_ms = (uint32_t)(2000.0f / speed);
    float phase = ((float)(ctx->now_ms % cycle_ms) / cycle_ms) * 2.0f * M_PI;
    
    for (int i = 0; i < npx; i++) {
        float wave = (sinf(phase + (i * 2.0f * M_PI / npx)) + 1.0f) / 2.0f;
        uint8_t brightness = (uint8_t)(wave * 255);
        
        uint8_t hue = (uint8_t)((i * 256 / npx) & 0xFF);
        hsv2rgb(hue, 255, brightness, &ctx->framebuf[i].r, 
                &ctx->framebuf[i].g, &ctx->framebuf[i].b);
    }
}

// ===== HEARTBEAT =====
static void fx_heartbeat(render_ctx_t* ctx, const effect_params_t* params) {
    uint32_t color = params ? params->color : 0xFF0000;
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Heartbeat pattern: two pulses
    uint32_t cycle_ms = (uint32_t)(1200.0f / speed);
    uint32_t t = ctx->now_ms % cycle_ms;
    
    float brightness = 0.0f;
    if (t < 150) {
        // First pulse
        brightness = sinf((float)t / 150.0f * M_PI);
    } else if (t > 300 && t < 450) {
        // Second pulse
        brightness = sinf((float)(t - 300) / 150.0f * M_PI) * 0.7f;
    }
    
    uint8_t bright_u8 = (uint8_t)(brightness * 255);
    
    for (int i = 0; i < npx; i++) {
        ctx->framebuf[i].r = scale8(r, bright_u8);
        ctx->framebuf[i].g = scale8(g, bright_u8);
        ctx->framebuf[i].b = scale8(b, bright_u8);
    }
}

// ===== BREATHING =====
static void fx_breathing(render_ctx_t* ctx, const effect_params_t* params) {
    uint32_t color = params ? params->color : 0x0080FF;
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Smooth sine wave breathing
    uint32_t cycle_ms = (uint32_t)(3000.0f / speed);
    float t = ((float)(ctx->now_ms % cycle_ms) / cycle_ms) * 2.0f * M_PI;
    float brightness = (sinf(t) + 1.0f) / 2.0f; // 0.0 to 1.0
    
    uint8_t bright_u8 = (uint8_t)(brightness * 255);
    
    for (int i = 0; i < npx; i++) {
        ctx->framebuf[i].r = scale8(r, bright_u8);
        ctx->framebuf[i].g = scale8(g, bright_u8);
        ctx->framebuf[i].b = scale8(b, bright_u8);
    }
}

// ===== POLICE STROBE =====
static void fx_police_strobe(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    
    int npx = ctx->count;
    int half = npx / 2;
    
    uint32_t cycle_ms = (uint32_t)(500.0f / speed);
    uint32_t phase = (ctx->now_ms / cycle_ms) % 4;
    
    // Clear buffer
    memset(ctx->framebuf, 0, npx * sizeof(px_rgba_t));
    
    // Alternate red/blue flashing on each half
    if (phase < 2) {
        // Left red, right blue
        for (int i = 0; i < half; i++) {
            ctx->framebuf[i].r = (phase == 0) ? 255 : 0;
        }
        for (int i = half; i < npx; i++) {
            ctx->framebuf[i].b = (phase == 0) ? 255 : 0;
        }
    } else {
        // Left blue, right red
        for (int i = 0; i < half; i++) {
            ctx->framebuf[i].b = (phase == 2) ? 255 : 0;
        }
        for (int i = half; i < npx; i++) {
            ctx->framebuf[i].r = (phase == 2) ? 255 : 0;
        }
    }
}

// ===== CONFETTI =====
static void fx_confetti(render_ctx_t* ctx, const effect_params_t* params) {
    float intensity = params ? params->intensity : 0.3f;
    
    int npx = ctx->count;
    
    // Fade existing pixels
    for (int i = 0; i < npx; i++) {
        ctx->framebuf[i].r = scale8(ctx->framebuf[i].r, 240);
        ctx->framebuf[i].g = scale8(ctx->framebuf[i].g, 240);
        ctx->framebuf[i].b = scale8(ctx->framebuf[i].b, 240);
    }
    
    // Add new random sparkles
    int sparkle_count = (int)(intensity * 3) + 1;
    for (int s = 0; s < sparkle_count; s++) {
        int pos = (ctx->now_ms * 71 + s * 137) % npx;
        uint8_t hue = (uint8_t)((ctx->now_ms + s * 43) & 0xFF);
        hsv2rgb(hue, 255, 255, &ctx->framebuf[pos].r, 
                &ctx->framebuf[pos].g, &ctx->framebuf[pos].b);
    }
}

// ===== BPM STROBE =====
static void fx_bpm_strobe(render_ctx_t* ctx, const effect_params_t* params) {
    uint32_t color = params ? params->color : 0xFFFFFF;
    
    int npx = ctx->count;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Get beat phase from sync system
    float beat_phase = sync_phase_01(ctx->now_ms);
    
    // Flash on beat
    uint8_t brightness = (beat_phase < 0.1f) ? 255 : 0;
    
    for (int i = 0; i < npx; i++) {
        ctx->framebuf[i].r = scale8(r, brightness);
        ctx->framebuf[i].g = scale8(g, brightness);
        ctx->framebuf[i].b = scale8(b, brightness);
    }
}

// ===== RIPPLE =====
static void fx_ripple(render_ctx_t* ctx, const effect_params_t* params) {
    float speed = params ? params->speed : 1.0f;
    uint32_t color = params ? params->color : 0x00FFFF;
    
    int npx = ctx->count;
    int center = npx / 2;
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Clear buffer
    memset(ctx->framebuf, 0, npx * sizeof(px_rgba_t));
    
    // Calculate ripple
    uint32_t cycle_ms = (uint32_t)(2000.0f / speed);
    float t = ((float)(ctx->now_ms % cycle_ms) / cycle_ms);
    float ripple_radius = t * center;
    
    for (int i = 0; i < npx; i++) {
        float dist = fabsf(i - center);
        float diff = fabsf(dist - ripple_radius);
        
        if (diff < 3.0f) {
            float brightness = 1.0f - (diff / 3.0f);
            ctx->framebuf[i].r = scale8(r, (uint8_t)(brightness * 255));
            ctx->framebuf[i].g = scale8(g, (uint8_t)(brightness * 255));
            ctx->framebuf[i].b = scale8(b, (uint8_t)(brightness * 255));
        }
    }
}

// ===== Registration =====
void fx_pattern_register_all(void) {
    fx_register("scanner", fx_scanner);
    fx_register("comet", fx_comet);
    fx_register("theater_chase", fx_theater_chase);
    fx_register("running_lights", fx_running_lights);
    fx_register("heartbeat", fx_heartbeat);
    fx_register("breathing", fx_breathing);
    fx_register("police_strobe", fx_police_strobe);
    fx_register("confetti", fx_confetti);
    fx_register("bpm_strobe", fx_bpm_strobe);
    fx_register("ripple", fx_ripple);
}
