// components/signage/include/PowerBudget.hpp
#pragma once
#include <cstdint>
#include "RenderTypes.hpp"
#include "SignageTypes.hpp"
#include "PowerConfigLimits.hpp"
#include "PowerModel.hpp"

namespace lg {

constexpr uint16_t kDefaultPowerRampMs = 750;

/// Power transient diagnostics structure (LG-HW-004 & LG-HW-005)
struct PowerTransientDiagnostics {
    uint32_t enable_events = 0;
    uint32_t ramp_events = 0;

    PowerMilliwatts requested_power_mw = 0;
    PowerMilliwatts limited_power_mw = 0;
    PowerMilliwatts limit_mw = 0;
    uint32_t throttling_events = 0;

    PowerMilliwatts previous_limited_power_mw = 0;
    PowerMilliwatts max_observed_step_mw = 0;

    uint32_t power_step_clamps = 0;

    bool estimate_complete = true;
    PowerEstimateState model_state = PowerEstimateState::Complete;
    uint16_t unmodeled_channel_mask = 0;

    uint16_t ramp_ms = kDefaultPowerRampMs;

    uint32_t channel_throttle_events = 0;
    uint32_t unknown_load_warnings = 0;
    uint32_t overflow_rejection_attempts = 0;

    SupplyDomainDiagnostics supply_domains[kMaxPowerSupplies]{};
};

/// Multi-tier per-frame power limiter with quiescent modeling, soft-start,
/// multi-supply domain budgeting, and bounded power-step transient control.
class PowerBudget {
public:
    /// Set the maximum power draw in milliwatts. 0 = unlimited.
    void set_limit_mw(PowerMilliwatts max_mw) { limit_mw_ = max_mw; }
    PowerMilliwatts limit_mw() const { return limit_mw_; }

    /// Configure soft-start ramp. When enabled, brightness ramps from 0
    /// to full over ramp_ms milliseconds after enable().
    void set_soft_start(bool enabled, uint16_t ramp_ms);

    /// Called when outputs are enabled or on high-risk transitions to start the power ramp.
    void on_enable();
    void restart_ramp() { on_enable(); }

    /// Returns a brightness multiplier (0–255) for soft-start.
    uint8_t soft_start_factor(uint32_t ms_since_enable) const;

    /// Estimate total power from RenderFrame, apply local fixture clamping,
    /// independent supply-domain limits, soft-start, bounded power-step limiting,
    /// and global budget clamping in-place.
    /// Recalculates final modeled consumption from actual post-scaled output values.
    /// Returns estimated mW before any power limiting (requested).
    PowerMilliwatts enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms);

    /// Total count of frames where output was scaled due to local or global limits.
    uint32_t throttling_events() const { return throttling_events_; }

    /// Estimated requested power consumption in mW (before any limiting/soft-start).
    PowerMilliwatts requested_power_mw() const { return requested_power_mw_; }

    /// Estimated limited power consumption in mW (after global limiting/soft-start).
    PowerMilliwatts limited_power_mw() const { return limited_power_mw_; }

    /// Latest estimated limited power consumption in mW (legacy compatibility).
    PowerMilliwatts current_power_mw() const { return limited_power_mw_; }

    /// True if all enabled channels have a complete electrical power model.
    bool estimate_complete() const { return estimate_complete_; }
    PowerEstimateState model_state() const { return model_state_; }
    uint16_t unmodeled_channel_mask() const { return unmodeled_channel_mask_; }

    uint32_t enable_events() const { return enable_events_; }
    uint32_t soft_start_events() const { return soft_start_events_; }
    uint32_t last_enable_ms() const { return last_enable_ms_; }
    uint32_t last_soft_start_duration_ms() const { return last_soft_start_duration_ms_; }
    PowerMilliwatts previous_limited_power_mw() const { return previous_limited_power_mw_; }
    PowerMilliwatts max_observed_step_mw() const { return max_observed_step_mw_; }
    uint32_t power_step_clamps() const { return power_step_clamps_; }
    uint16_t ramp_ms() const { return ramp_ms_; }

    /// Base transient control on the last confirmed physical command, not a
    /// rendered frame that may have been skipped, rejected or further limited.
    void observe_output_power(PowerMilliwatts mw) { previous_limited_power_mw_ = mw; }

    PowerTransientDiagnostics diagnostics() const;
    OutputPowerRequest output_request() const { return output_request_; }

    static constexpr PixelPowerModel kPixelModel = power_model::kPixel;

private:
    PowerMilliwatts limit_mw_ = 0;
    bool     soft_start_enabled_ = false;
    uint16_t ramp_ms_ = kDefaultPowerRampMs;
    uint32_t enable_ms_ = 0;
    uint32_t last_enforce_ms_ = 0;

    uint32_t throttling_events_ = 0;
    PowerMilliwatts requested_power_mw_ = 0;
    PowerMilliwatts limited_power_mw_ = 0;
    OutputPowerRequest output_request_{};
    bool     estimate_complete_ = true;
    PowerEstimateState model_state_ = PowerEstimateState::Complete;
    uint16_t unmodeled_channel_mask_ = 0;

    // Per-supply throttle event counters
    uint32_t supply_throttle_events_[kMaxPowerSupplies]{};
    uint64_t supply_last_load_mw_[kMaxPowerSupplies]{};

    // HW-004 telemetry & power-step control
    uint32_t enable_events_ = 0;
    uint32_t soft_start_events_ = 0;
    uint32_t last_enable_ms_ = 0;
    uint32_t last_soft_start_duration_ms_ = 0;
    PowerMilliwatts previous_limited_power_mw_ = 0;
    PowerMilliwatts max_observed_step_mw_ = 0;
    uint32_t power_step_clamps_ = 0;

    // HW-005 telemetry
    uint32_t channel_throttle_events_ = 0;
    uint32_t unknown_load_warnings_ = 0;
    uint32_t overflow_rejection_attempts_ = 0;
    PowerSupplyConfig last_supplies_[kMaxPowerSupplies]{};
};

} // namespace lg
