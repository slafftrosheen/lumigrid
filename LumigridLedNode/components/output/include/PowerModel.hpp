#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include "SignageTypes.hpp"

namespace lg {

// Conservative command estimates inherited from the final output guard.
// These are software planning assumptions, not measured fixture calibration.
struct PixelPowerModel {
    uint16_t idle_mw = 5;
    uint16_t rgb_component_mw = 295;
    uint16_t white_component_mw = 380;
};

namespace power_model {
inline constexpr PixelPowerModel kPixel{5, 295, 380};
// LCM(255, 4095): retain exact RGB/RGBW and PWM fractions through all limits.
inline constexpr uint64_t kUnitsPerMw = 69615;
inline constexpr uint16_t kFullScale = 256;

inline constexpr uint32_t full_pixel_mw(bool rgbw) {
    return kPixel.idle_mw + 3u * kPixel.rgb_component_mw + (rgbw ? kPixel.white_component_mw : 0u);
}

struct Load {
    uint64_t idle = 0;
    uint64_t active = 0;
    uint64_t total() const { return idle + active; }
    Load& operator+=(const Load& other) {
        idle += other.idle;
        active += other.active;
        return *this;
    }
};

inline uint64_t units(uint64_t mw) {
    return mw > UINT64_MAX / kUnitsPerMw ? UINT64_MAX : mw * kUnitsPerMw;
}

inline uint64_t milliwatts(uint64_t value) {
    return value / kUnitsPerMw + (value % kUnitsPerMw != 0);
}

inline PowerMilliwatts reported_mw(uint64_t value) {
    return static_cast<PowerMilliwatts>(std::min<uint64_t>(milliwatts(value), UINT32_MAX));
}

inline uint64_t pixel_active(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
    return ((uint64_t(r) + g + b) * kPixel.rgb_component_mw +
            uint64_t(w) * kPixel.white_component_mw) * (kUnitsPerMw / 255);
}

inline uint64_t pwm_active(uint16_t duty, uint32_t rated_mw) {
    return uint64_t(std::min<uint16_t>(duty, 4095)) * rated_mw * (kUnitsPerMw / 4095);
}

inline uint32_t pwm_rating(const DeviceConfig& cfg, uint8_t ch) {
    const auto& channel = cfg.channels[ch];
    return cfg.schema_version < 4 && channel.rated_full_power_mw == 0
        ? channel.max_milliwatts : channel.rated_full_power_mw;
}

inline uint32_t channel_limit(const DeviceConfig& cfg, uint8_t ch) {
    const auto& channel = cfg.channels[ch];
    return cfg.schema_version < 4 && channel.channel_limit_mw == 0
        ? channel.max_milliwatts : channel.channel_limit_mw;
}

inline uint16_t scale_for_units(const Load& load, uint64_t ceiling) {
    if (load.total() <= ceiling) return kFullScale;
    if (ceiling <= load.idle || load.active == 0) return 0;
    // The numerator is bounded by the 16 channels' RGB/PWM numeric ranges.
    return static_cast<uint16_t>(((ceiling - load.idle) * kFullScale) / load.active);
}

inline uint16_t scale_for_limit(const Load& load, uint64_t limit_mw) {
    return limit_mw == 0 ? kFullScale : scale_for_units(load, units(limit_mw));
}

inline PowerEstimateState model_state(uint16_t enabled_mask, uint16_t unmodeled_mask) {
    if (unmodeled_mask == 0) return PowerEstimateState::Complete;
    return (enabled_mask & ~unmodeled_mask) != 0
        ? PowerEstimateState::Partial : PowerEstimateState::Unknown;
}
} // namespace power_model

struct SupplyDomainDiagnostics {
    uint8_t id = 0;
    bool enabled = false;
    uint32_t voltage_mv = 0;
    uint32_t rated_current_ma = 0;
    uint64_t usable_power_mw = 0;
    uint64_t estimated_load_mw = 0;
    uint16_t channel_mask = 0;
    uint32_t throttle_events = 0;
};

// Per-frame renderer context travels with its synchronous output transaction.
// Final-driver limiting merges these masks, counting each limited channel/domain once.
struct OutputPowerRequest {
    PowerMilliwatts requested_mw = 0;
    uint16_t limited_channel_mask = 0;
    uint8_t limited_supply_mask = 0;
    bool throttled = false;
};

// Published only after confirmed output/blackout. A failed transaction retains
// the last confirmed estimates and clears command_confirmed until recovery.
struct OutputPowerDiagnostics {
    bool command_confirmed = false;
    bool estimate_complete = false;
    PowerEstimateState model_state = PowerEstimateState::Unknown;
    uint16_t unmodeled_channel_mask = 0;
    PowerMilliwatts requested_power_mw = 0;
    PowerMilliwatts limited_power_mw = 0;
    PowerMilliwatts idle_power_mw = 0;
    PowerMilliwatts limit_mw = 0;
    PowerMilliwatts previous_limited_power_mw = 0;
    PowerMilliwatts max_observed_step_mw = 0;
    uint32_t throttling_events = 0;
    uint32_t channel_throttle_events = 0;
    SupplyDomainDiagnostics supply_domains[kMaxPowerSupplies]{};
};

} // namespace lg
