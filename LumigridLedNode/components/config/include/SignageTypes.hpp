// components/config/include/SignageTypes.hpp
#pragma once
#include <cstdint>
#include <cstring>

namespace lg {

// ──────────────────────────── Brand Colors ────────────────────────────

struct BrandColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// ──────────────────────────── Signage Effects ─────────────────────────

enum class SignageEffect : uint8_t {
    Solid    = 0,   // Static brand color
    Breathe  = 1,   // Smooth sine-wave brightness pulse
    Chase    = 2,   // Moving dot along pixel strip
    Sweep    = 3,   // Color wipe across all lanes sequentially
    Reveal   = 4,   // Sequential lane-by-lane enable (brand reveal)
    Sparkle  = 5,   // Random pixel twinkle over base color
    Gradient = 6,   // Smooth gradient across strip length
    Strobe   = 7,
    Fire     = 8
};

inline const char* signage_effect_name(SignageEffect e) {
    switch (e) {
        case SignageEffect::Solid:    return "solid";
        case SignageEffect::Breathe:  return "breathe";
        case SignageEffect::Chase:    return "chase";
        case SignageEffect::Sweep:    return "sweep";
        case SignageEffect::Reveal:   return "reveal";
        case SignageEffect::Sparkle:  return "sparkle";
        case SignageEffect::Gradient: return "gradient";
        case SignageEffect::Strobe:   return "strobe";
        case SignageEffect::Fire:     return "fire";
    }
    return "unknown";
}

inline bool parse_signage_effect(const char* name, SignageEffect& out) {
    if (!name) return false;
    if (strcmp(name, "solid")    == 0) { out = SignageEffect::Solid;    return true; }
    if (strcmp(name, "breathe")  == 0) { out = SignageEffect::Breathe;  return true; }
    if (strcmp(name, "chase")    == 0) { out = SignageEffect::Chase;    return true; }
    if (strcmp(name, "sweep")    == 0) { out = SignageEffect::Sweep;    return true; }
    if (strcmp(name, "reveal")   == 0) { out = SignageEffect::Reveal;   return true; }
    if (strcmp(name, "sparkle")  == 0) { out = SignageEffect::Sparkle;  return true; }
    if (strcmp(name, "gradient") == 0) { out = SignageEffect::Gradient; return true; }
    if (strcmp(name, "strobe")   == 0) { out = SignageEffect::Strobe;   return true; }
    if (strcmp(name, "fire")     == 0) { out = SignageEffect::Fire;     return true; }
    return false;
}

// ──────────────────────────── Channel Config ──────────────────────────

struct ChannelConfig {
    BrandColor    pixel_color;          // Addressable LED color for this lane
    uint16_t      pwm_duty = 0;         // PCA9685 duty (0–4095)
    SignageEffect effect = SignageEffect::Solid;
    uint16_t      effect_speed_ms = 2000;
    uint8_t       palette_id = 0;
    uint8_t       phase_offset = 0;
    uint8_t       fx_param = 0;
    bool          pixel_enabled = true;
    bool          pwm_enabled = true;
};

// ──────────────────────────── Brand Preset ────────────────────────────

struct BrandPreset {
    char          name[24]       = {};  // "Store Open", "Night Mode", etc.
    ChannelConfig channels[8]    = {};  // Per-channel configuration
    uint8_t       brightness     = 128; // Global brightness (0–255)
    uint16_t      transition_ms  = 1000;   // Cross-fade duration (ms)
    uint8_t       _reserved[4]   = {};     // Future use, keeps alignment

    void set_name(const char* n) {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
};

// ──────────────────────────── Sequencer ────────────────────────────────

struct SequencerStep {
    uint8_t preset_index = 0;
    uint8_t length_beats = 1;
};

struct SequencerTrack {
    uint8_t       channel_id = 0;
    SequencerStep steps[16]  = {};
    uint8_t       num_steps  = 16;
    bool          enabled    = false;
};

struct Sequence {
    SequencerTrack tracks[8] = {};
    uint16_t       bpm       = 120;
    bool           enabled   = false;
};

// ──────────────────────────── Device Config ───────────────────────────

struct DeviceConfig {
    char     device_name[32]        = "lumigrid";
    uint8_t  active_preset_index    = 0;
    uint8_t  fallback_preset_index  = 0;
    uint16_t watchdog_timeout_s     = 60;   // Seconds before fallback
    uint16_t max_power_mw           = 0;    // 0 = no limit
    uint8_t  gamma_x10              = 22;   // 2.2 gamma
    bool     soft_start_enabled     = true;
    uint16_t soft_start_ms          = 500;
    bool     mqtt_enabled           = false;
    char     mqtt_broker[64]        = {};
    uint16_t mqtt_port              = 1883;
    char     timezone[48]           = "UTC0";
    bool     sync_enabled           = false;
    uint8_t  sync_role              = 0;    // 0=Auto, 1=Master, 2=Worker
    uint8_t  _reserved[8]           = {};
};

// ──────────────────────────── Sync Protocol ──────────────────────────

enum class NodeRole : uint8_t {
    Auto   = 0,
    Master = 1,
    Worker = 2,
};

constexpr uint8_t kMaxPresets   = 32;

} // namespace lg
