// components/config/include/SignageTypes.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "OutputCapabilities.hpp"
#include "ChannelHardware.hpp"

namespace lg {

// ──────────────────────────── Brand Colors ────────────────────────────

struct BrandColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// ──────────────────────────── Constants ────────────────────────────
constexpr uint16_t MAX_FIXTURE_PIXELS = 256;
constexpr uint16_t MAX_PIXELS = MAX_FIXTURE_PIXELS;
constexpr uint8_t MAX_RMT_LANES = OutputCapabilities::kRmtLanes;
constexpr uint8_t MAX_PWM_CHANNELS = OutputCapabilities::kPca9685Channels;

// ──────────────────────────── Channel Topology (Hardware Truth) ───────

enum class AddressableType : uint8_t {
    WS2812 = 0,
    SK6812 = 1
};

enum class PixelOrder : uint8_t {
    RGB = 0,
    GRB = 1,
    RGBW = 2,
    GRBW = 3
};

constexpr bool pixel_order_valid(AddressableType type, PixelOrder order) {
    if (type == AddressableType::WS2812) {
        return order == PixelOrder::RGB || order == PixelOrder::GRB;
    }
    if (type == AddressableType::SK6812) {
        return order == PixelOrder::RGBW || order == PixelOrder::GRBW;
    }
    return false;
}

inline const char* pixel_order_name(PixelOrder order) {
    switch (order) {
        case PixelOrder::RGB: return "RGB";
        case PixelOrder::GRB: return "GRB";
        case PixelOrder::RGBW: return "RGBW";
        case PixelOrder::GRBW: return "GRBW";
    }
    return "GRB";
}

inline bool parse_pixel_order(const char* str, PixelOrder& out) {
    if (!str) return false;
    if (strcmp(str, "RGB") == 0) { out = PixelOrder::RGB; return true; }
    if (strcmp(str, "GRB") == 0) { out = PixelOrder::GRB; return true; }
    if (strcmp(str, "RGBW") == 0) { out = PixelOrder::RGBW; return true; }
    if (strcmp(str, "GRBW") == 0) { out = PixelOrder::GRBW; return true; }
    return false;
}

enum class ZoneRole : uint8_t {
    Unassigned = 0,
    Sign = 1,
    SignElement = 2,
    AmbianceLight = 3,
    MainLight = 4
};

inline const char* zone_role_name(ZoneRole role) {
    switch (role) {
        case ZoneRole::Sign: return "Sign";
        case ZoneRole::SignElement: return "Sign Element";
        case ZoneRole::AmbianceLight: return "Ambiance Light";
        case ZoneRole::MainLight: return "Main Light";
        default: return "Unassigned";
    }
}

inline bool parse_zone_role(const char* name, ZoneRole& out) {
    if (!name) return false;
    if (strcmp(name, "Sign") == 0) { out = ZoneRole::Sign; return true; }
    if (strcmp(name, "Sign Element") == 0) { out = ZoneRole::SignElement; return true; }
    if (strcmp(name, "Ambiance Light") == 0) { out = ZoneRole::AmbianceLight; return true; }
    if (strcmp(name, "Main Light") == 0) { out = ZoneRole::MainLight; return true; }
    if (strcmp(name, "Unassigned") == 0) { out = ZoneRole::Unassigned; return true; }
    return false;
}

// Explicit numeric electrical types and limits (LG-HW-005)
using PowerMilliwatts = uint32_t;
using VoltageMillivolts = uint32_t;
using CurrentMilliamps = uint32_t;

enum class PowerEstimateState : uint8_t {
    Complete = 0,
    Partial  = 1,
    Unknown  = 2
};

inline const char* power_estimate_state_name(PowerEstimateState state) {
    switch (state) {
        case PowerEstimateState::Complete: return "complete";
        case PowerEstimateState::Partial:  return "partial";
        case PowerEstimateState::Unknown:  return "unknown";
    }
    return "unknown";
}

constexpr uint8_t kMaxPowerSupplies = 4;

struct PowerSupplyConfig {
    bool enabled = false;
    char name[24] = {};

    uint32_t voltage_mv = 0;
    uint32_t rated_current_ma = 0;

    uint16_t usable_capacity_permille = 800; // Default 80% planning reserve
    uint16_t channel_mask = 0;               // Bitmask of assigned channels (A1..A8 = bits 0..7, P1..P8 = bits 8..15)

    uint64_t rated_power_mw() const {
        return (uint64_t(voltage_mv) * rated_current_ma) / 1000ULL;
    }

    uint64_t usable_power_mw() const {
        return (rated_power_mw() * usable_capacity_permille) / 1000ULL;
    }
};

// ── Legacy Channel Config V3 (for lossless binary migration) ──
struct ChannelConfigV3 {
    char name[24] = "";
    bool enabled = false;

    // Addressable-only configuration
    AddressableType addressable_type = AddressableType::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint16_t pixel_count = 0;

    // Common
    uint8_t max_brightness = 255;
    bool gamma_correction = true;
    uint16_t max_milliwatts = 0;

    // DMX patch
    uint16_t dmx_universe = 0;
    uint16_t dmx_start_channel = 0;
    uint8_t dmx_mode = 0;
};

// Active 16-channel configuration (V4)
struct ChannelConfig {
    char name[24] = "";
    bool enabled = false;

    // Addressable-only configuration
    AddressableType addressable_type = AddressableType::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint16_t pixel_count = 0;

    // Common
    uint8_t max_brightness = 255;
    bool gamma_correction = true;

    // HW-005.8: Separate rated full power from channel power limit
    uint32_t rated_full_power_mw = 0; // For PWM: full-duty active power. For ALED: 0 (modeled per-pixel)
    uint32_t channel_limit_mw = 0;    // Local channel power ceiling (0 = unlimited)

    uint32_t max_milliwatts = 0;      // Legacy compatibility field (channel budget in mW)

    // DMX patch
    uint16_t dmx_universe = 0;
    uint16_t dmx_start_channel = 0;
    // Addressable: 0 = wash, 1 = direct pixel.
    // PWM: 0 = normal independent dimmer. Values 2..5 are reserved only for
    // migration compatibility with legacy compound PWM fixtures:
    // 2=R, 3=G, 4=B, 5=W. New UI/configuration must never create these modes.
    uint8_t dmx_mode = 0;
};

struct ChannelGroup {
    char name[24] = "";
    uint16_t channel_mask = 0;
    ZoneRole role = ZoneRole::Unassigned;
    bool enabled = false;
};

// ──────────────────────────── Legacy Output Schema (Compatibility) ───────

enum class OutputType : uint8_t { RMT = 0, PWM = 1, SPI = 2 };

struct PhysicalOutput {
    OutputType type = OutputType::RMT;
    uint8_t hardware_id = 0; // RMT channel 0-7, PWM channel 0-7
    uint8_t gpio_pin = 0;
    bool enabled = false;
};

enum class FixtureProfile : uint8_t { WS2812 = 0, SK6812 = 1, PWM_Single = 2, PWM_RGB = 3, PWM_RGBW = 4 };

/// Physical Output Topology Model (Batch 7 Item 1 - Option A):
/// - Addressable fixtures (WS2812, SK6812): Exactly 1 physical RMT output mapped via
///   physical_output_index[0]. Indices 1..3 are ignored.
/// - PWM fixtures: 1..4 physical PWM outputs mapped according to profile:
///     PWM_Single -> physical_output_index[0] (1 channel)
///     PWM_RGB    -> physical_output_index[0..2] (3 channels)
///     PWM_RGBW   -> physical_output_index[0..3] (4 channels)
struct FixtureConfig {
    FixtureProfile profile = FixtureProfile::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint8_t physical_output_index[4] = {}; // Indices into physical_outputs array
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

struct ChannelContent {
    LayerConfig layers[3] = {};
};

struct SceneConfig {
    uint8_t schema_version = 2;
    char name[24] = "";
    ChannelContent channels[16] = {};
    uint8_t global_brightness = 128;
    uint16_t transition_ms = 1000;
    uint32_t checksum = 0;

    void reset() {
        schema_version = 2;
        name[0] = '\0';
        global_brightness = 128;
        transition_ms = 1000;
        checksum = 0;
        auto* p = reinterpret_cast<uint8_t*>(channels);
        for (size_t i = 0; i < sizeof(channels); ++i) p[i] = 0;
    }
};

struct SceneConfigV1 {
    char name[24] = "";
    ZoneContent zones[8] = {};
    uint8_t global_brightness = 128;
    uint16_t transition_ms = 1000;
    uint32_t checksum = 0;
};

// ──────────────────────────── Scene Motion / Automation ───────────────

enum class TransitionCurve : uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
    Step
};

inline const char* transition_curve_name(TransitionCurve curve) {
    switch (curve) {
        case TransitionCurve::Linear:    return "linear";
        case TransitionCurve::EaseIn:    return "easeIn";
        case TransitionCurve::EaseOut:   return "easeOut";
        case TransitionCurve::EaseInOut: return "easeInOut";
        case TransitionCurve::Step:      return "step";
    }
    return "linear";
}

inline bool parse_transition_curve(const char* name, TransitionCurve& out) {
    if (!name) return false;
    if (strcmp(name, "linear") == 0)    { out = TransitionCurve::Linear; return true; }
    if (strcmp(name, "easeIn") == 0)    { out = TransitionCurve::EaseIn; return true; }
    if (strcmp(name, "easeOut") == 0)   { out = TransitionCurve::EaseOut; return true; }
    if (strcmp(name, "easeInOut") == 0) { out = TransitionCurve::EaseInOut; return true; }
    if (strcmp(name, "step") == 0)      { out = TransitionCurve::Step; return true; }
    return false;
}

constexpr uint8_t kAutomationTracksPerScene = 4;
constexpr uint8_t kAutomationPointsPerTrack = 6;

enum class AutomationTarget : uint8_t {
    GlobalBrightness = 0,
    LayerOpacity,
    LayerSpeed,
    LayerParameter
};

inline const char* automation_target_name(AutomationTarget target) {
    switch (target) {
        case AutomationTarget::GlobalBrightness: return "globalBrightness";
        case AutomationTarget::LayerOpacity:     return "layerOpacity";
        case AutomationTarget::LayerSpeed:       return "layerSpeed";
        case AutomationTarget::LayerParameter:   return "layerParameter";
    }
    return "globalBrightness";
}

inline bool parse_automation_target(const char* name, AutomationTarget& out) {
    if (!name) return false;
    if (strcmp(name, "globalBrightness") == 0) return (out = AutomationTarget::GlobalBrightness, true);
    if (strcmp(name, "layerOpacity") == 0)     return (out = AutomationTarget::LayerOpacity, true);
    if (strcmp(name, "layerSpeed") == 0)       return (out = AutomationTarget::LayerSpeed, true);
    if (strcmp(name, "layerParameter") == 0)   return (out = AutomationTarget::LayerParameter, true);
    return false;
}

enum class AutomationCurve : uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
    Step
};

inline const char* automation_curve_name(AutomationCurve curve) {
    switch (curve) {
        case AutomationCurve::Linear:    return "linear";
        case AutomationCurve::EaseIn:    return "easeIn";
        case AutomationCurve::EaseOut:   return "easeOut";
        case AutomationCurve::EaseInOut: return "easeInOut";
        case AutomationCurve::Step:      return "step";
    }
    return "linear";
}

inline bool parse_automation_curve(const char* name, AutomationCurve& out) {
    if (!name) return false;
    if (strcmp(name, "linear") == 0)    return (out = AutomationCurve::Linear, true);
    if (strcmp(name, "easeIn") == 0)    return (out = AutomationCurve::EaseIn, true);
    if (strcmp(name, "easeOut") == 0)   return (out = AutomationCurve::EaseOut, true);
    if (strcmp(name, "easeInOut") == 0) return (out = AutomationCurve::EaseInOut, true);
    if (strcmp(name, "step") == 0)      return (out = AutomationCurve::Step, true);
    return false;
}

struct AutomationPoint {
    uint16_t position = 0; // 0..65535 normalized
    uint16_t value = 0;
};

struct AutomationTrack {
    AutomationTarget target = AutomationTarget::GlobalBrightness;
    union {
        uint8_t channel = 0;
        uint8_t zone;
    };
    uint8_t layer = 0;
    AutomationCurve curve = AutomationCurve::Linear;
    uint8_t point_count = 0;
    bool loop = true;
    uint32_t period_ms = 5000;
    AutomationPoint points[kAutomationPointsPerTrack]{};
};

struct SceneMotionConfig {
    uint8_t schema_version = 1;
    TransitionCurve transition_curve = TransitionCurve::EaseInOut;
    uint8_t track_count = 0;
    AutomationTrack tracks[kAutomationTracksPerScene]{};
    uint32_t checksum = 0;

    void reset() {
        schema_version = 1;
        transition_curve = TransitionCurve::EaseInOut;
        track_count = 0;
        checksum = 0;
        auto* p = reinterpret_cast<uint8_t*>(tracks);
        for (size_t i = 0; i < sizeof(tracks); ++i) p[i] = 0;
    }
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

constexpr size_t kSignageEffectCount = 12;

struct EffectMetadata {
    const char* id;
    const char* label;
    const char* description;

    bool supports_speed;
    bool supports_secondary_color;
    bool supports_palette;

    bool spatial;
    bool pwm_recommended;

    const char* parameter_label;
    const char* parameter_unit;

    uint8_t parameter_min;
    uint8_t parameter_max;
    uint8_t parameter_default;

    bool has_parameter;
};

inline const EffectMetadata& signage_effect_metadata(SignageEffect effect) {
    static constexpr EffectMetadata kMetadata[] = {
        {"solid",      "Steady color",  "A constant, even wash of color.",                             false, false, false, false, true,  "",              "",         0,   0,   0, false},
        {"breathe",    "Smooth pulse",  "Gently fades between a soft glow and full brightness.",        true,  false, false, false, true,  "Minimum glow",  "%",        1, 100,   4, true},
        {"chase",      "Moving comet",  "A bright comet travels along the strip.",                      true,  true,  true,  true,  false, "Tail length",   "pixels",   1,   8,   3, true},
        {"sweep",      "Color sweep",   "Builds color across the strip from end to end.",               true,  true,  true,  true,  false, "",              "",         0,   0,   0, false},
        {"reveal",     "Soft reveal",   "Fades the lane in for a polished sign-on.",                    true,  false, false, true,  true,  "Fade length",   "%",       10,  90,  25, true},
        {"sparkle",    "Random twinkle","Adds crisp white twinkles over the base color.",               true,  true,  false, true,  false, "Twinkles",      "per lane", 1,  12,   2, true},
        {"gradient",   "Flowing glow",  "Moves a soft wave of light along the strip.",                  true,  true,  true,  true,  false, "Wave contrast", "%",        0, 100, 100, true},
        {"strobe",     "Beat strobe",   "Flashes the lane with an adjustable on-time.",                 true,  false, false, false, true,  "Flash time",    "%",        5,  95,  50, true},
        {"fire",       "Living flame",  "Creates a flickering flame texture from the selected color.",  true,  true,  true,  true,  false, "Flame height",  "%",       20, 100,  70, true},
        {"aurora",     "Aurora glow",   "Gentle shifting colors across the strip.",                     true,  true,  true,  true,  false, "Hue shift",     "deg",      1, 255, 120, true},
        {"colorwave",  "Color wave",    "Smooth sine wave color interpolation.",                        true,  true,  true,  true,  false, "Wave length",   "px",       10, 255,  50, true},
        {"staticpulse","Static pulse",  "Slow dimmer pulse for main/ambiance lights.",                 true,  false, false, false, true,  "Min dim",       "%",        0, 100,  20, true},
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
    SequencerTrack tracks[16] = {};
    uint16_t       bpm       = 120;
    bool           enabled   = false;
    uint32_t       checksum  = 0;
};

// ──────────────────────────── Calendar Scheduler ───────────────────────

enum class CalendarAction : uint8_t {
    Preset   = 0,
    Sequence = 1,
    CueList  = 2,
};

struct CalendarScheduleEntry {
    uint8_t        hour = 0;
    uint8_t        minute = 0;
    uint8_t        days_of_week = 0x7F; // Sunday bit 0 through Saturday bit 6
    uint8_t        preset_index = 0;
    CalendarAction action = CalendarAction::Preset;
    bool           enabled = false;
    uint8_t        cue_list_index = 0;
    uint8_t        _reserved[1] = {};
};

struct CalendarSchedule {
    CalendarScheduleEntry entries[8] = {};
    uint32_t checksum = 0;
};

// ──────────────────────────── Cue Lists ───────────────────────────────

constexpr uint8_t kMaxCueLists = 8;
constexpr uint8_t kMaxCuesPerList = 16;

// LG-034: explicit end-action semantics (replaces magic 0/1/2 values).
// Only applied on natural Single Shot completion.
enum class CueEndAction : uint8_t {
    HoldLast        = 0,
    Blackout        = 1,
    RestoreFallback = 2,
};

inline const char* cue_end_action_name(CueEndAction action) {
    switch (action) {
        case CueEndAction::Blackout:        return "blackout";
        case CueEndAction::RestoreFallback: return "restore_fallback";
        case CueEndAction::HoldLast:
        default:                            return "hold_last";
    }
}

inline bool parse_cue_end_action(const char* name, CueEndAction& out) {
    if (!name) return false;
    if (strcmp(name, "hold_last") == 0)        return (out = CueEndAction::HoldLast, true);
    if (strcmp(name, "blackout") == 0)         return (out = CueEndAction::Blackout, true);
    if (strcmp(name, "restore_fallback") == 0) return (out = CueEndAction::RestoreFallback, true);
    return false;
}

// LG-034: why a Cue Runner stopped. Governs whether the configured end action runs.
enum class CueStopReason : uint8_t {
    Operator          = 0, // operator STOP: hold current output, no end action
    Preempted         = 1, // Scene/Timeline took ownership: no end action
    NaturalCompletion = 2, // Single Shot reached its actual end: end action applies
    SyncLost          = 3, // remote Cue transport released: no end action
};

inline const char* cue_stop_reason_name(CueStopReason reason) {
    switch (reason) {
        case CueStopReason::Preempted:         return "preempted";
        case CueStopReason::NaturalCompletion: return "natural_completion";
        case CueStopReason::SyncLost:          return "sync_lost";
        case CueStopReason::Operator:
        default:                               return "operator";
    }
}

enum class CueRunMode : uint8_t {
    SingleShot = 0,
    Loop       = 1,
    PingPong   = 2,
    Random     = 3,
    Manual     = 4
};

inline const char* cue_run_mode_name(CueRunMode mode) {
    switch (mode) {
        case CueRunMode::SingleShot: return "single_shot";
        case CueRunMode::Loop:       return "loop";
        case CueRunMode::PingPong:   return "ping_pong";
        case CueRunMode::Random:     return "random";
        case CueRunMode::Manual:     return "manual";
        default:                     return "unknown";
    }
}

inline bool parse_cue_run_mode(const char* name, CueRunMode& out) {
    if (!name) return false;
    if (strcmp(name, "single_shot") == 0) return (out = CueRunMode::SingleShot, true);
    if (strcmp(name, "loop") == 0)        return (out = CueRunMode::Loop, true);
    if (strcmp(name, "ping_pong") == 0)   return (out = CueRunMode::PingPong, true);
    if (strcmp(name, "random") == 0)      return (out = CueRunMode::Random, true);
    if (strcmp(name, "manual") == 0)      return (out = CueRunMode::Manual, true);
    return false;
}

struct CueEntry {
    static constexpr uint8_t kMaxLabelBytes = 23; // label is char[24] incl. NUL

    uint8_t scene_index = 0;
    uint32_t hold_ms = 3000;
    uint16_t fade_ms = 1000;
    TransitionCurve curve = TransitionCurve::EaseInOut;
    char label[24] = "";
};

struct CueList {
    static constexpr uint8_t kMaxNameBytes = 31;   // name is char[32] incl. NUL
    static constexpr uint8_t kMaxLabelBytes = 23;  // label is char[24] incl. NUL

    char name[32] = "";
    CueRunMode mode = CueRunMode::Loop;
    uint8_t count = 0;
    CueEndAction end_action = CueEndAction::HoldLast;
    CueEntry cues[kMaxCuesPerList]{};
    uint32_t checksum = 0;

    void reset() {
        name[0] = '\0';
        mode = CueRunMode::Loop;
        count = 0;
        end_action = CueEndAction::HoldLast;
        checksum = 0;
        auto* p = reinterpret_cast<uint8_t*>(cues);
        for (size_t i = 0; i < sizeof(cues); ++i) p[i] = 0;
    }
};

// ──────────────────────────── Device Config ───────────────────────────

struct ZoneConfig {
    char name[24] = "";
    ZoneRole role = ZoneRole::Unassigned;
};

enum class MergeMode : uint8_t {
    HTP = 0,
    LTP = 1
};

struct ProtocolConfig {
    // LG-038.3 §13/§36/§37: fresh nodes boot with both DMX protocols OFF.
    // The setup wizard / user enables exactly what the install needs; a new
    // controller must not spawn protocol workers before DMX is configured.
    // Persisted nodes keep whatever they stored (no migration overwrite).
    bool     artnet_enabled       = false;
    bool     sacn_enabled         = false;
    uint16_t universe_start       = 0;
    MergeMode merge_mode          = MergeMode::HTP;
    uint16_t stream_timeout_ms    = 2000;
    uint8_t  live_stream_layer    = 2;
    uint16_t zone_start_channel   = 1; // 1-512 (default 1: Zone 0 = Ch 1..3, Zone 1 = Ch 4..6, etc.)
};

struct DeviceConfigV2 {
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
    
    // RBAC tokens (deprecated; managed by AuthStore in LG-029)
    char     admin_token[32]        = "";
    char     installer_token[32]    = "";
    char     operator_token[32]     = "";

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
    
    uint32_t reserved_padding_v3        = 0;

    uint32_t checksum                   = 0;
};

struct DeviceConfigV3 {
    uint8_t  schema_version         = 3;
    char     device_name[32]        = "lumigrid";
    uint8_t  active_preset_index    = 0;
    uint8_t  fallback_preset_index  = 0;
    uint16_t watchdog_timeout_s     = 60;   // System/Network timeout
    uint16_t max_power_mw           = 0;    // 0 = no limit (16-bit legacy)
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
    
    // RBAC tokens (deprecated: managed by AuthStore in LG-029)
    char     admin_token[32]        = "";
    char     installer_token[32]    = "";
    char     operator_token[32]     = "";

    // Device Identity
    char     serial_number[32]      = "";
    char     hardware_revision[16]  = "v1.0";
    char     site_label[64]         = "";
    char     group_label[64]        = "";
    
    // Frozen V3 16-channel topology with 16-bit power fields
    ChannelConfigV3 channels[16]        = {};
    ChannelGroup    groups[8]           = {};
    ProtocolConfig  protocol            = {};

    uint32_t checksum                   = 0;
};

struct DeviceConfig {
    uint8_t  schema_version         = 4;
    char     device_name[32]        = "lumigrid";
    uint8_t  active_preset_index    = 0;
    uint8_t  fallback_preset_index  = 0;
    uint16_t watchdog_timeout_s     = 60;   // System/Network timeout
    PowerMilliwatts max_power_mw    = 0;    // 0 = no limit (32-bit, HW-005)
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
    
    // RBAC tokens (deprecated: managed by AuthStore in LG-029)
    char     admin_token[32]        = "";
    char     installer_token[32]    = "";
    char     operator_token[32]     = "";

    // Device Identity
    char     serial_number[32]      = "";
    char     hardware_revision[16]  = "v1.0";
    char     site_label[64]         = "";
    char     group_label[64]        = "";
    
    // Hardware-truth 16-channel topology (32-bit power, HW-005)
    ChannelConfig channels[16]          = {};
    ChannelGroup  groups[8]             = {};
    ProtocolConfig protocol             = {};

    // HW-005.5: Logical external power-supply domains (up to 4)
    PowerSupplyConfig power_supplies[kMaxPowerSupplies] = {};

    uint32_t checksum                   = 0;

    void reset() {
        schema_version = 4;
        strncpy(device_name, "lumigrid", sizeof(device_name) - 1);
        active_preset_index = 0;
        fallback_preset_index = 0;
        watchdog_timeout_s = 60;
        max_power_mw = 0;
        gamma_x10 = 22;
        soft_start_enabled = true;
        soft_start_ms = 500;
        mqtt_enabled = false;
        mqtt_broker[0] = '\0';
        mqtt_port = 1883;
        strncpy(timezone, "UTC0", sizeof(timezone) - 1);
        sync_enabled = false;
        sync_role = 0;
        sync_token = 0;
        admin_token[0] = '\0';
        installer_token[0] = '\0';
        operator_token[0] = '\0';
        serial_number[0] = '\0';
        strncpy(hardware_revision, "v1.0", sizeof(hardware_revision) - 1);
        site_label[0] = '\0';
        group_label[0] = '\0';
        checksum = 0;
        auto* pc = reinterpret_cast<uint8_t*>(channels);
        for (size_t i = 0; i < sizeof(channels); ++i) pc[i] = 0;
        auto* pg = reinterpret_cast<uint8_t*>(groups);
        for (size_t i = 0; i < sizeof(groups); ++i) pg[i] = 0;
        auto* ps = reinterpret_cast<uint8_t*>(power_supplies);
        for (size_t i = 0; i < sizeof(power_supplies); ++i) ps[i] = 0;
        protocol = ProtocolConfig{};
    }
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
