// components/signage/include/PowerBudget.hpp
#pragma once
#include <cstdint>
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

/// Multi-tier per-frame power limiter with quiescent modeling and soft-start support.
///
/// Estimates total LED power draw directly from the newly rendered RenderFrame.
/// Enforces both local per-fixture power limits (FixtureConfig.max_milliwatts)
/// and the global power supply budget (DeviceConfig.max_power_mw) in-place before output.
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

    /// Estimate total power from RenderFrame, apply local fixture clamping,
    /// soft-start, and global budget clamping in-place.
    /// Returns estimated mW before global clamping.
    uint32_t enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms);

    /// Total count of frames where output was scaled due to local or global limits.
    uint32_t throttling_events() const { return throttling_events_; }

    /// Latest estimated power consumption in mW.
    uint32_t current_power_mw() const { return last_power_mw_; }

private:
    uint16_t limit_mw_ = 0;
    bool     soft_start_enabled_ = false;
    uint16_t ramp_ms_ = 500;
    uint32_t enable_ms_ = 0;
    uint32_t throttling_events_ = 0;
    uint32_t last_power_mw_ = 0;

    /// Quiescent idle power per addressable pixel in mW (~1mA @ 5V = 5mW).
    static constexpr uint32_t kPixelIdleMw = 5;
    /// Maximum active power per addressable pixel in mW (~55mA @ 5V = 275mW).
    static constexpr uint32_t kPixelActiveMw = 295;
    /// Per-PWM channel max mW (default 500mW).
    static constexpr uint32_t kPwmChannelMaxMw = 500;
};

} // namespace lg
