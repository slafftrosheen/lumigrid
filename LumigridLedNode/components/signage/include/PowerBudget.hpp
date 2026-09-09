// components/signage/include/PowerBudget.hpp
#pragma once
#include <cstdint>
#include "AddressableDriver.hpp"
#include "Pca9685Driver.hpp"

namespace lg {

/// Per-frame power limiter with soft-start support.
///
/// Estimates total LED power draw from current pixel/PWM values and
/// proportionally scales brightness if the budget would be exceeded.
class PowerBudget {
public:
    /// Set the maximum power draw in milliwatts. 0 = unlimited.
    void set_limit_mw(uint16_t max_mw) { limit_mw_ = max_mw; }

    /// Configure soft-start ramp. When enabled, brightness ramps from 0
    /// to full over ramp_ms milliseconds after enable().
    void set_soft_start(bool enabled, uint16_t ramp_ms);

    /// Called once when outputs are enabled, to start the soft-start ramp.
    void on_enable();

    /// Returns a brightness multiplier (0–255) for soft-start.
    /// Should be called every frame with the elapsed time since enable.
    uint8_t soft_start_factor(uint32_t ms_since_enable) const;

    /// Estimate total power and clamp brightness if budget exceeded.
    /// Modifies brightness in-place. Returns estimated mW before clamping.
    uint32_t enforce(const AddressableDriver& pixels,
                     const Pca9685Driver& pwm,
                     uint8_t& brightness);

private:
    uint16_t limit_mw_ = 0;
    bool     soft_start_enabled_ = false;
    uint16_t ramp_ms_ = 500;
    uint32_t enable_ms_ = 0;

    /// Per-pixel power in mW at full brightness (WS2812B: ~60mA @ 5V = 300mW).
    static constexpr uint32_t kPixelMaxMw = 300;
    /// Per-PWM channel max mW (adjustable per installation, default 500mW).
    static constexpr uint32_t kPwmChannelMaxMw = 500;
};

} // namespace lg
