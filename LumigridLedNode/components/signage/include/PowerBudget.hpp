// components/signage/include/PowerBudget.hpp
#pragma once
#include <cstdint>
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"

namespace lg {

constexpr uint16_t kDefaultPowerRampMs = 750;

/// Conservative estimated electrical model per pixel type.
///
/// NOTE (Safety / Architecture Disclaimer):
/// Power limiting is an estimated software brightness limiter.
/// It does not replace correctly sized PSU, wiring, fuses,
/// MOSFET ratings or branch protection.
struct PixelPowerModel {
    uint16_t idle_mw = 5;
    uint16_t max_rgb_mw = 295;
    uint16_t max_rgbw_mw = 380;
};

/// Power transient diagnostics structure (LG-HW-004 §22)
struct PowerTransientDiagnostics {
    uint32_t enable_events = 0;
    uint32_t ramp_events = 0;

    uint32_t requested_power_mw = 0;
    uint32_t limited_power_mw = 0;

    uint32_t previous_limited_power_mw = 0;
    uint32_t max_observed_step_mw = 0;

    uint32_t power_step_clamps = 0;

    bool estimate_complete = true;

    uint16_t ramp_ms = kDefaultPowerRampMs;
};

/// Multi-tier per-frame power limiter with quiescent modeling, soft-start,
/// and bounded power-step transient control.
class PowerBudget {
public:
    /// Set the maximum power draw in milliwatts. 0 = unlimited.
    void set_limit_mw(uint16_t max_mw) { limit_mw_ = max_mw; }
    uint16_t limit_mw() const { return limit_mw_; }

    /// Configure soft-start ramp. When enabled, brightness ramps from 0
    /// to full over ramp_ms milliseconds after enable().
    void set_soft_start(bool enabled, uint16_t ramp_ms);

    /// Called when outputs are enabled or on high-risk transitions to start the power ramp.
    void on_enable();
    void restart_ramp() { on_enable(); }

    /// Returns a brightness multiplier (0–255) for soft-start.
    uint8_t soft_start_factor(uint32_t ms_since_enable) const;

    /// Estimate total power from RenderFrame, apply local fixture clamping,
    /// soft-start, bounded power-step limiting, and global budget clamping in-place.
    /// Returns estimated mW before global clamping (requested).
    uint32_t enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms);

    /// Total count of frames where output was scaled due to local or global limits.
    uint32_t throttling_events() const { return throttling_events_; }

    /// Estimated requested power consumption in mW (before global limiting/soft-start).
    uint32_t requested_power_mw() const { return requested_power_mw_; }

    /// Estimated limited power consumption in mW (after global limiting/soft-start).
    uint32_t limited_power_mw() const { return limited_power_mw_; }

    /// Latest estimated limited power consumption in mW (legacy compatibility).
    uint32_t current_power_mw() const { return limited_power_mw_; }

    /// True if all enabled channels have a complete electrical power model.
    bool estimate_complete() const { return estimate_complete_; }

    uint32_t enable_events() const { return enable_events_; }
    uint32_t soft_start_events() const { return soft_start_events_; }
    uint32_t last_enable_ms() const { return last_enable_ms_; }
    uint32_t last_soft_start_duration_ms() const { return last_soft_start_duration_ms_; }
    uint32_t previous_limited_power_mw() const { return previous_limited_power_mw_; }
    uint32_t max_observed_step_mw() const { return max_observed_step_mw_; }
    uint32_t power_step_clamps() const { return power_step_clamps_; }
    uint16_t ramp_ms() const { return ramp_ms_; }

    PowerTransientDiagnostics diagnostics() const;

    static constexpr PixelPowerModel kPixelModel{5, 295, 380};

private:
    uint16_t limit_mw_ = 0;
    bool     soft_start_enabled_ = false;
    uint16_t ramp_ms_ = kDefaultPowerRampMs;
    uint32_t enable_ms_ = 0;
    uint32_t last_enforce_ms_ = 0;

    uint32_t throttling_events_ = 0;
    uint32_t requested_power_mw_ = 0;
    uint32_t limited_power_mw_ = 0;
    bool     estimate_complete_ = true;

    // HW-004 telemetry & power-step control
    uint32_t enable_events_ = 0;
    uint32_t soft_start_events_ = 0;
    uint32_t last_enable_ms_ = 0;
    uint32_t last_soft_start_duration_ms_ = 0;
    uint32_t previous_limited_power_mw_ = 0;
    uint32_t max_observed_step_mw_ = 0;
    uint32_t power_step_clamps_ = 0;
};

} // namespace lg
