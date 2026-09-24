// components/config/include/ChannelConfiguration.hpp
#pragma once
#include <cstdint>
#include "SignageTypes.hpp"

namespace lg {

/**
 * @brief Canonical evaluation of whether a physical channel has been configured.
 *
 * Rules:
 * - Addressable (indices 0..7): configured if pixel_count > 0 || enabled || dmx_start_channel > 0 || max_milliwatts > 0
 * - PWM (indices 8..15): configured if enabled || dmx_start_channel > 0 || max_milliwatts > 0
 *
 * Channel name is metadata only and does NOT make a channel configured.
 */
inline bool channel_is_configured(uint8_t channel_index, const ChannelConfig& cfg) {
    if (channel_index < 8) {
        return cfg.pixel_count > 0 || cfg.enabled || cfg.dmx_start_channel > 0 || cfg.max_milliwatts > 0;
    }
    return cfg.enabled || cfg.dmx_start_channel > 0 || cfg.max_milliwatts > 0;
}

} // namespace lg
