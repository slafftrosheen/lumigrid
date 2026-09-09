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

// ──────────────────────────── New Product Schema ─────────────────────────

enum class OutputType : uint8_t { RMT = 0, PWM = 1, SPI = 2 };

struct PhysicalOutput {
    OutputType type = OutputType::RMT;
    uint8_t hardware_id = 0; // RMT channel 0-7, PWM channel 0-15
    uint8_t gpio_pin = 0;
    bool enabled = false;
};

enum class FixtureProfile : uint8_t { WS2812 = 0, SK6812 = 1, PWM_Single = 2, PWM_RGB = 3, PWM_RGBW = 4 };

enum class PixelOrder : uint8_t { RGB = 0, GRB = 1, RGBW = 2, GRBW = 3 };

struct FixtureConfig {
    FixtureProfile profile = FixtureProfile::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint8_t physical_output_index[4] = {}; // Indices into PhysicalOutputs array
    uint8_t zone_id = 0; // Which zone this fixture belongs to (0-7)
    uint16_t num_pixels = 0;
    uint8_t max_brightness = 255;
    uint8_t white_balance[3] = {255, 255, 255};
    bool gamma_correction = true;
    uint16_t max_milliwatts = 0; // 0 = no local limit
    uint16_t dmx_universe = 0;      // DMX Universe (0-63999)
    uint16_t dmx_start_channel = 0; // 1-512 (0 = unpatched / follows zone wash)
    uint8_t  dmx_mode = 0;          // 0 = Wash, 1 = Direct Pixel Map
};

enum class BlendMode : uint8_t {
    Normal = 0,
    Add = 1,
    Multiply = 2,
    Screen = 3
};

inline const char* blend_mode_name(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:   return "normal";
        case BlendMode::Add:      return "add";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Screen:   return "screen";
    }
    return "normal";
}

inline bool parse_blend_mode(const char* name, BlendMode& out) {
    if (!name) return false;
    if (strcmp(name, "normal") == 0)   { out = BlendMode::Normal; return true; }
    if (strcmp(name, "add") == 0)      { out = BlendMode::Add; return true; }
    if (strcmp(name, "multiply") == 0) { out = BlendMode::Multiply; return true; }
    if (strcmp(name, "screen") == 0)   { out = BlendMode::Screen; return true; }
    return false;
}

struct LayerConfig {
    uint8_t effect = 0; // Maps to SignageEffect
    BrandColor primary_color = {255, 255, 255};
    BrandColor secondary_color = {0, 0, 0};
    uint16_t speed_ms = 2000;
    uint8_t fx_param = 0;
    uint8_t palette_id = 0;
    uint8_t opacity = 255;
    BlendMode blend_mode = BlendMode::Normal;
    bool enabled = false;
};

struct ZoneContent {
    LayerConfig layers[3] = {};
};

struct SceneConfig {
    char name[24] = "";
    ZoneContent zones[8] = {}; // Content per zone index
    uint8_t global_brightness = 128;
    uint16_t transition_ms = 1000;
    uint32_t checksum = 0;
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
    Fire     = 8,
    Aurora   = 9,
    ColorWave = 10,
    StaticPulse = 11
};

struct EffectMetadata {
    const char* id;
    const char* label;
    const char* description;
    const char* parameter_label;
    const char* parameter_unit;
    uint8_t parameter_min;
    uint8_t parameter_max;
    uint8_t parameter_default;
    bool has_parameter;
};

inline const EffectMetadata& signage_effect_metadata(SignageEffect effect) {
    static constexpr EffectMetadata kMetadata[] = {
        {"solid",    "Steady color",  "A constant, even wash of color.",                         "",              "",         0,  0,   0, false},
        {"breathe",  "Smooth pulse",  "Gently fades between a soft glow and full brightness.",    "Minimum glow",  "%",        1, 100,   4, true},
        {"chase",    "Moving comet",  "A bright comet travels along the strip.",                  "Tail length",   "pixels",   1,   8,   3, true},
        {"sweep",    "Color sweep",   "Builds color across the strip from end to end.",           "",              "",         0,   0,   0, false},
        {"reveal",   "Soft reveal",   "Fades the lane in for a polished sign-on.",                "Fade length",   "%",       10,  90,  25, true},
        {"sparkle",  "Random twinkle","Adds crisp white twinkles over the base color.",           "Twinkles",      "per lane", 1,  12,   2, true},
        {"gradient", "Flowing glow",  "Moves a soft wave of light along the strip.",              "Wave contrast", "%",        0, 100, 100, true},
        {"strobe",   "Beat strobe",   "Flashes the lane with an adjustable on-time.",             "Flash time",    "%",        5,  95,  50, true},
        {"fire",     "Living flame",  "Creates a flickering flame texture from the selected color.","Flame height", "%",       20, 100,  70, true},
        {"aurora",   "Aurora glow",   "Gentle shifting colors across the strip.",                 "Hue shift",     "deg",      1, 255, 120, true},
        {"colorwave","Color wave",    "Smooth sine wave color interpolation.",                    "Wave length",   "px",       10, 255,  50, true},
        {"staticpulse","Static pulse", "Slow dimmer pulse for main/ambiance lights.",             "Min dim",       "%",        0, 100,  20, true},
    };

    const uint8_t index = static_cast<uint8_t>(effect);
    return index < sizeof(kMetadata) / sizeof(kMetadata[0])
        ? kMetadata[index]
        : kMetadata[0];
}

inline const char* signage_effect_name(SignageEffect e) {
    return signage_effect_metadata(e).id;
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
    if (strcmp(name, "aurora")   == 0) { out = SignageEffect::Aurora;   return true; }
    if (strcmp(name, "colorwave")== 0) { out = SignageEffect::ColorWave;return true; }
    if (strcmp(name, "staticpulse")== 0) { out = SignageEffect::StaticPulse; return true; }
    return false;
}

enum class SignageModifier : uint8_t {
    None   = 0,
    Strobe = 1,
};

inline const char* signage_modifier_name(SignageModifier modifier) {
    switch (modifier) {
        case SignageModifier::None:   return "none";
        case SignageModifier::Strobe: return "strobe";
    }
    return "none";
}

inline bool parse_signage_modifier(const char* name, SignageModifier& out) {
    if (!name) return false;
    if (strcmp(name, "none") == 0) {
        out = SignageModifier::None;
        return true;
    }
    if (strcmp(name, "strobe") == 0) {
        out = SignageModifier::Strobe;
        return true;
    }
    return false;
}


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
    uint32_t       checksum  = 0;
};

// ──────────────────────────── Calendar Scheduler ───────────────────────

enum class CalendarAction : uint8_t {
    Preset   = 0,
    Sequence = 1,
};

struct CalendarScheduleEntry {
    uint8_t        hour = 0;
    uint8_t        minute = 0;
    uint8_t        days_of_week = 0x7F; // Sunday bit 0 through Saturday bit 6
    uint8_t        preset_index = 0;
    CalendarAction action = CalendarAction::Preset;
    bool           enabled = false;
    uint8_t        _reserved[2] = {};
};

struct CalendarSchedule {
    CalendarScheduleEntry entries[8] = {};
    uint32_t checksum = 0;
};

// ──────────────────────────── Device Config ───────────────────────────

enum class ZoneRole : uint8_t {
    Unassigned = 0,
    Sign = 1,
    SignElement = 2,
    AmbianceLight = 3,
    MainLight = 4
};

inline const char* zone_role_name(ZoneRole role) {
    switch(role) {
        case ZoneRole::Sign: return "Sign";
        case ZoneRole::SignElement: return "Sign Element";
        case ZoneRole::AmbianceLight: return "Ambiance Light";
        case ZoneRole::MainLight: return "Main Light";
        default: return "Unassigned";
    }
}

inline bool parse_zone_role(const char* name, ZoneRole& out) {
    if(!name) return false;
    if(strcmp(name, "Sign") == 0) { out = ZoneRole::Sign; return true; }
    if(strcmp(name, "Sign Element") == 0) { out = ZoneRole::SignElement; return true; }
    if(strcmp(name, "Ambiance Light") == 0) { out = ZoneRole::AmbianceLight; return true; }
    if(strcmp(name, "Main Light") == 0) { out = ZoneRole::MainLight; return true; }
    out = ZoneRole::Unassigned;
    return true;
}

struct ZoneConfig {
    char name[24] = "";
    ZoneRole role = ZoneRole::Unassigned;
};

enum class MergeMode : uint8_t {
    HTP = 0,
    LTP = 1
};

struct ProtocolConfig {
    bool     artnet_enabled       = true;
    bool     sacn_enabled         = true;
    uint16_t universe_start       = 0;
    MergeMode merge_mode          = MergeMode::HTP;
    uint16_t stream_timeout_ms    = 2000;
    uint8_t  live_stream_layer    = 2;
    uint16_t zone_start_channel   = 1; // 1-512 (default 1: Zone 0 = Ch 1..3, Zone 1 = Ch 4..6, etc.)
};

struct DeviceConfig {
    uint8_t  schema_version         = 2;
    char     device_name[32]        = "lumigrid";
    uint8_t  active_preset_index    = 0;
    uint8_t  fallback_preset_index  = 0;
    uint16_t watchdog_timeout_s     = 60;   // System/Network timeout
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
    uint32_t sync_token             = 0;
    
    // RBAC tokens
    char     admin_token[32]        = "lumigrid_admin";
    char     installer_token[32]    = "lumigrid_install";
    char     operator_token[32]     = "lumigrid_op";

    // Device Identity
    char     serial_number[32]      = "";
    char     hardware_revision[16]  = "v1.0";
    char     site_label[64]         = "";
    char     group_label[64]        = "";
    
    // Product-grade configuration mapping
    PhysicalOutput physical_outputs[16] = {};
    FixtureConfig fixtures[16]          = {};
    ZoneConfig zones[8]                 = {};
    ProtocolConfig protocol             = {};

    uint32_t checksum                   = 0;
};

// ──────────────────────────── Sync Protocol ──────────────────────────

enum class NodeRole : uint8_t {
    Auto   = 0,
    Master = 1,
    Worker = 2,
};

constexpr uint8_t kMaxPresets   = 32;

// ──────────────────────────── Health & Telemetry ─────────────────────

struct DeviceHealth {
    uint32_t uptime_seconds = 0;
    uint32_t free_heap = 0;
    uint32_t min_free_heap = 0;
    int8_t   wifi_rssi = 0;
    uint8_t  reset_reason = 0;
    float    fps = 0.0f;
    uint16_t active_preset = 0;
    
    // Telemetry
    uint32_t packet_loss_count = 0;
    uint32_t clipping_events = 0;
    uint32_t wifi_disconnects = 0;
};

} // namespace lg
