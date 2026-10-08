// components/signage/PowerBudget.cpp
#include "PowerBudget.hpp"
#include "ChannelHardware.hpp"
#include "esp_timer.h"
#include <algorithm>

namespace lg {

void PowerBudget::set_soft_start(bool enabled, uint16_t ramp_ms) {
    soft_start_enabled_ = enabled;
    ramp_ms_ = ramp_ms > 0 ? ramp_ms : kDefaultPowerRampMs;
}

void PowerBudget::on_enable() {
    enable_events_++;
    soft_start_events_++;
    enable_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    last_enable_ms_ = enable_ms_;
    last_soft_start_duration_ms_ = ramp_ms_;
    previous_limited_power_mw_ = 0;
}

uint8_t PowerBudget::soft_start_factor(uint32_t ms_since_enable) const {
    if (!soft_start_enabled_) return 255;
    if (ms_since_enable >= ramp_ms_) return 255;
    return static_cast<uint8_t>((uint32_t(ms_since_enable) * 255) / ramp_ms_);
}

PowerTransientDiagnostics PowerBudget::diagnostics() const {
    PowerTransientDiagnostics d{};
    d.enable_events = enable_events_;
    d.ramp_events = soft_start_events_;
    d.requested_power_mw = requested_power_mw_;
    d.limited_power_mw = limited_power_mw_;
    d.previous_limited_power_mw = previous_limited_power_mw_;
    d.max_observed_step_mw = max_observed_step_mw_;
    d.power_step_clamps = power_step_clamps_;
    d.estimate_complete = estimate_complete_;
    d.ramp_ms = ramp_ms_;
    return d;
}

uint32_t PowerBudget::enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms) {
    // Determine frame period in ms
    uint32_t frame_period_ms = 20;
    if (last_enforce_ms_ > 0 && current_ms > last_enforce_ms_) {
        frame_period_ms = std::min<uint32_t>(current_ms - last_enforce_ms_, 100);
    }
    last_enforce_ms_ = current_ms;

    // 1. Calculate soft-start factor
    uint8_t ss_factor = 255;
    if (soft_start_enabled_) {
        uint32_t ms_since_enable = (current_ms >= enable_ms_) ? (current_ms - enable_ms_) : 0;
        ss_factor = soft_start_factor(ms_since_enable);
    }

    uint32_t total_idle_mw = 0;
    uint32_t total_active_mw = 0;
    bool throttled = false;
    estimate_complete_ = true;

    // 2. Per-channel power calculation and local channel limit enforcement
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (!ccfg.enabled) continue;

        const auto hw = channel_hardware(ch);
        ChannelFrame& cf = frame.channels[ch];

        uint32_t channel_idle_mw = 0;
        uint32_t channel_active_mw = 0;

        if (hw.kind == ChannelKind::Addressable) {
            uint16_t pcount = std::min<uint16_t>(ccfg.pixel_count, MAX_PIXELS);
            channel_idle_mw = static_cast<uint32_t>(pcount) * kPixelModel.idle_mw;
            const bool is_rgbw = (ccfg.addressable_type == AddressableType::SK6812);
            for (uint16_t p = 0; p < pcount; ++p) {
                const LightValue& lv = cf.pixels[p];
                if (is_rgbw) {
                    uint32_t sum = uint32_t(lv.r) + lv.g + lv.b + lv.w;
                    channel_active_mw += (sum * kPixelModel.max_rgbw_mw) / 1020;
                } else {
                    uint32_t sum = uint32_t(lv.r) + lv.g + lv.b;
                    channel_active_mw += (sum * kPixelModel.max_rgb_mw) / 765;
                }
            }

            // Enforce local channel limit if configured
            if (ccfg.max_milliwatts > 0) {
                uint32_t channel_total = channel_idle_mw + channel_active_mw;
                if (channel_total > ccfg.max_milliwatts) {
                    throttled = true;
                    if (ccfg.max_milliwatts <= channel_idle_mw) {
                        for (uint16_t p = 0; p < pcount; ++p) {
                            cf.pixels[p] = {0, 0, 0, 0};
                        }
                        channel_active_mw = 0;
                    } else if (channel_active_mw > 0) {
                        uint32_t allowed_active = ccfg.max_milliwatts - channel_idle_mw;
                        uint8_t local_scale = static_cast<uint8_t>((uint64_t(allowed_active) * 255) / channel_active_mw);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            LightValue& lv = cf.pixels[p];
                            lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                            lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                            lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                            lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                        }
                        channel_active_mw = (channel_active_mw * local_scale) / 255;
                    }
                }
            }
        } else {
            // PWM channels: modelled only when max_milliwatts > 0.
            // If max_milliwatts == 0, power model is unknown/incomplete.
            if (ccfg.max_milliwatts > 0) {
                channel_active_mw = (uint64_t(cf.pwm) * ccfg.max_milliwatts) / 4095;
            } else {
                channel_active_mw = 0;
                estimate_complete_ = false;
            }

            // Enforce local channel limit if configured
            if (ccfg.max_milliwatts > 0 && channel_active_mw > ccfg.max_milliwatts && channel_active_mw > 0) {
                throttled = true;
                uint8_t local_scale = static_cast<uint8_t>((uint64_t(ccfg.max_milliwatts) * 255) / channel_active_mw);
                cf.pwm = static_cast<uint16_t>((uint32_t(cf.pwm) * local_scale) / 255);
                LightValue& lv = cf.pixels[0];
                lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                channel_active_mw = (channel_active_mw * local_scale) / 255;
            }
        }

        total_idle_mw += channel_idle_mw;
        total_active_mw += channel_active_mw;
    }

    const uint32_t total_mw = total_idle_mw + total_active_mw;
    requested_power_mw_ = total_mw;

    // 3. Determine if global budget scaling is needed
    uint8_t power_scale = 255;
    if (limit_mw_ > 0 && total_mw > limit_mw_) {
        throttled = true;
        if (total_active_mw > 0) {
            uint32_t available_active_mw = (limit_mw_ > total_idle_mw) ? (limit_mw_ - total_idle_mw) : 0;
            power_scale = static_cast<uint8_t>((uint64_t(available_active_mw) * 255) / total_active_mw);
        } else {
            power_scale = 0;
        }
    }

    // Baseline combined scale (soft-start * global limit)
    uint16_t combined_scale = (uint16_t(ss_factor) * power_scale) / 255;
    uint16_t final_scale = combined_scale;

    // 4. Bounded power-step control (LG-HW-004 §18/§19)
    if (limit_mw_ > 0 && ramp_ms_ > 0) {
        const uint32_t max_step_mw = std::max<uint32_t>(1, (uint32_t(limit_mw_) * frame_period_ms) / ramp_ms_);
        const uint32_t candidate_active_mw = (uint64_t(total_active_mw) * combined_scale) / 255;
        const uint32_t candidate_total_mw = total_idle_mw + candidate_active_mw;

        if (candidate_total_mw > previous_limited_power_mw_ + max_step_mw) {
            const uint32_t max_allowed_total_mw = previous_limited_power_mw_ + max_step_mw;
            power_step_clamps_++;
            throttled = true;

            const uint32_t allowed_active_mw = (max_allowed_total_mw > total_idle_mw) ? (max_allowed_total_mw - total_idle_mw) : 0;
            const uint16_t step_scale = (total_active_mw > 0) ? static_cast<uint16_t>((uint64_t(allowed_active_mw) * 255) / total_active_mw) : 0;
            final_scale = std::min<uint16_t>(combined_scale, step_scale);
        }
    }

    if (throttled) {
        throttling_events_++;
    }

    // 5. Apply final scale globally across all active channels (preserving relative ratios)
    if (final_scale < 255) {
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
            const ChannelConfig& ccfg = cfg.channels[ch];
            if (!ccfg.enabled) continue;

            const auto hw = channel_hardware(ch);
            ChannelFrame& cf = frame.channels[ch];

            if (hw.kind == ChannelKind::Addressable) {
                uint16_t pcount = std::min<uint16_t>(ccfg.pixel_count, MAX_PIXELS);
                for (uint16_t p = 0; p < pcount; ++p) {
                    LightValue& lv = cf.pixels[p];
                    lv.r = static_cast<uint8_t>((uint16_t(lv.r) * final_scale) / 255);
                    lv.g = static_cast<uint8_t>((uint16_t(lv.g) * final_scale) / 255);
                    lv.b = static_cast<uint8_t>((uint16_t(lv.b) * final_scale) / 255);
                    lv.w = static_cast<uint8_t>((uint16_t(lv.w) * final_scale) / 255);
                }
            } else {
                cf.pwm = static_cast<uint16_t>((uint32_t(cf.pwm) * final_scale) / 255);
                LightValue& lv = cf.pixels[0];
                lv.r = static_cast<uint8_t>((uint16_t(lv.r) * final_scale) / 255);
                lv.g = static_cast<uint8_t>((uint16_t(lv.g) * final_scale) / 255);
                lv.b = static_cast<uint8_t>((uint16_t(lv.b) * final_scale) / 255);
                lv.w = static_cast<uint8_t>((uint16_t(lv.w) * final_scale) / 255);
            }
        }
        uint32_t limited_active_mw = (uint64_t(total_active_mw) * final_scale) / 255;
        limited_power_mw_ = total_idle_mw + limited_active_mw;
    } else {
        limited_power_mw_ = requested_power_mw_;
    }

    // 6. Track power step delta
    const uint32_t step_mw = (limited_power_mw_ > previous_limited_power_mw_) ?
                             (limited_power_mw_ - previous_limited_power_mw_) : 0;
    if (step_mw > max_observed_step_mw_) {
        max_observed_step_mw_ = step_mw;
    }
    previous_limited_power_mw_ = limited_power_mw_;

    return total_mw;
}

} // namespace lg
