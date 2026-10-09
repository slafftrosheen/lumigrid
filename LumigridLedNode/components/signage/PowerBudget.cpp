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
    d.model_state = model_state_;
    d.unmodeled_channel_mask = unmodeled_channel_mask_;
    d.ramp_ms = ramp_ms_;

    d.channel_throttle_events = channel_throttle_events_;
    d.unknown_load_warnings = unknown_load_warnings_;
    d.overflow_rejection_attempts = overflow_rejection_attempts_;

    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        d.supply_domains[s].id = s;
        d.supply_domains[s].enabled = last_supplies_[s].enabled;
        d.supply_domains[s].voltage_mv = last_supplies_[s].voltage_mv;
        d.supply_domains[s].rated_current_ma = last_supplies_[s].rated_current_ma;
        d.supply_domains[s].usable_power_mw = last_supplies_[s].usable_power_mw();
        d.supply_domains[s].channel_mask = last_supplies_[s].channel_mask;
        d.supply_domains[s].throttle_events = supply_throttle_events_[s];
        d.supply_domains[s].estimated_load_mw = supply_last_load_mw_[s];
    }
    return d;
}

PowerMilliwatts PowerBudget::enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms) {
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

    uint64_t total_idle_mw = 0;
    uint64_t total_active_mw = 0;
    bool throttled = false;

    estimate_complete_ = true;
    model_state_ = PowerEstimateState::Complete;
    unmodeled_channel_mask_ = 0;

    // Per-channel estimated consumption trackers before scaling
    uint64_t ch_idle_mw[kChannelCount] = {};
    uint64_t ch_active_mw[kChannelCount] = {};

    // 2. Per-channel power calculation and local channel limit enforcement
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (!ccfg.enabled) continue;

        const auto hw = channel_hardware(ch);
        ChannelFrame& cf = frame.channels[ch];

        uint64_t channel_idle_mw = 0;
        uint64_t channel_active_mw = 0;

        if (hw.kind == ChannelKind::Addressable) {
            uint16_t pcount = std::min<uint16_t>(ccfg.pixel_count, MAX_PIXELS);
            channel_idle_mw = uint64_t(pcount) * kPixelModel.idle_mw;
            const bool is_rgbw = (ccfg.addressable_type == AddressableType::SK6812);
            for (uint16_t p = 0; p < pcount; ++p) {
                const LightValue& lv = cf.pixels[p];
                if (is_rgbw) {
                    uint64_t sum = uint64_t(lv.r) + lv.g + lv.b + lv.w;
                    channel_active_mw += (sum * kPixelModel.max_rgbw_mw) / 1020ULL;
                } else {
                    uint64_t sum = uint64_t(lv.r) + lv.g + lv.b;
                    channel_active_mw += (sum * kPixelModel.max_rgb_mw) / 765ULL;
                }
            }

            // Enforce local channel limit if configured
            const uint32_t local_limit = ccfg.channel_limit_mw;
            if (local_limit > 0) {
                uint64_t channel_total = channel_idle_mw + channel_active_mw;
                if (channel_total > local_limit) {
                    throttled = true;
                    if (local_limit <= channel_idle_mw) {
                        for (uint16_t p = 0; p < pcount; ++p) {
                            cf.pixels[p] = {0, 0, 0, 0};
                        }
                        channel_active_mw = 0;
                    } else if (channel_active_mw > 0) {
                        uint64_t allowed_active = local_limit - channel_idle_mw;
                        uint8_t local_scale = static_cast<uint8_t>((allowed_active * 255ULL) / channel_active_mw);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            LightValue& lv = cf.pixels[p];
                            lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                            lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                            lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                            lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                        }
                        channel_active_mw = (channel_active_mw * local_scale) / 255ULL;
                    }
                }
            }
        } else {
            // PWM channels: modelled based on rated_full_power_mw (HW-005.8/9)
            // An unconfigured PWM load cannot silently equal zero (HW-005.9)
            if (ccfg.rated_full_power_mw > 0) {
                channel_active_mw = (uint64_t(cf.pwm) * ccfg.rated_full_power_mw) / 4095ULL;
            } else {
                channel_active_mw = 0;
                estimate_complete_ = false;
                model_state_ = PowerEstimateState::Partial;
                unmodeled_channel_mask_ |= static_cast<uint16_t>(1U << ch);
            }

            // Enforce local channel limit if configured
            const uint32_t local_limit = ccfg.channel_limit_mw;
            if (local_limit > 0 && channel_active_mw > local_limit && channel_active_mw > 0) {
                throttled = true;
                channel_throttle_events_++;
                uint8_t local_scale = static_cast<uint8_t>((uint64_t(local_limit) * 255ULL) / channel_active_mw);
                cf.pwm = static_cast<uint16_t>((uint32_t(cf.pwm) * local_scale) / 255);
                LightValue& lv = cf.pixels[0];
                lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                channel_active_mw = (channel_active_mw * local_scale) / 255ULL;
            }
        }

        ch_idle_mw[ch] = channel_idle_mw;
        ch_active_mw[ch] = channel_active_mw;

        total_idle_mw += channel_idle_mw;
        total_active_mw += channel_active_mw;
    }

    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        last_supplies_[s] = cfg.power_supplies[s];
    }

    if (unmodeled_channel_mask_ != 0) {
        unknown_load_warnings_++;
        // If all enabled channels are unmodeled, state is Unknown
        bool any_modeled = false;
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
            if (cfg.channels[ch].enabled && (unmodeled_channel_mask_ & (1U << ch)) == 0) {
                any_modeled = true;
                break;
            }
        }
        model_state_ = any_modeled ? PowerEstimateState::Partial : PowerEstimateState::Unknown;
    }

    const uint64_t total_mw = total_idle_mw + total_active_mw;
    requested_power_mw_ = static_cast<PowerMilliwatts>(std::min<uint64_t>(total_mw, UINT32_MAX));

    // 3. Enforce independent external power-supply domains (LG-HW-005.6)
    for (uint8_t s = 0; s < kMaxPowerSupplies; ++s) {
        const auto& psu = cfg.power_supplies[s];
        if (!psu.enabled || psu.channel_mask == 0) {
            supply_last_load_mw_[s] = 0;
            continue;
        }

        uint64_t supply_idle_mw = 0;
        uint64_t supply_active_mw = 0;

        for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
            if ((psu.channel_mask & (1U << ch)) != 0 && cfg.channels[ch].enabled) {
                supply_idle_mw += ch_idle_mw[ch];
                supply_active_mw += ch_active_mw[ch];
            }
        }

        const uint64_t supply_total_mw = supply_idle_mw + supply_active_mw;
        supply_last_load_mw_[s] = supply_total_mw;
        const uint64_t usable_capacity_mw = psu.usable_power_mw();

        if (usable_capacity_mw > 0 && supply_total_mw > usable_capacity_mw) {
            throttled = true;
            supply_throttle_events_[s]++;

            uint8_t supply_scale = 0;
            if (usable_capacity_mw > supply_idle_mw && supply_active_mw > 0) {
                uint64_t allowed_active = usable_capacity_mw - supply_idle_mw;
                supply_scale = static_cast<uint8_t>((allowed_active * 255ULL) / supply_active_mw);
            }

            // Scale ONLY the channels assigned to this supply domain
            for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
                if ((psu.channel_mask & (1U << ch)) != 0 && cfg.channels[ch].enabled) {
                    const auto hw = channel_hardware(ch);
                    ChannelFrame& cf = frame.channels[ch];

                    if (hw.kind == ChannelKind::Addressable) {
                        uint16_t pcount = std::min<uint16_t>(cfg.channels[ch].pixel_count, MAX_PIXELS);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            LightValue& lv = cf.pixels[p];
                            lv.r = static_cast<uint8_t>((uint16_t(lv.r) * supply_scale) / 255);
                            lv.g = static_cast<uint8_t>((uint16_t(lv.g) * supply_scale) / 255);
                            lv.b = static_cast<uint8_t>((uint16_t(lv.b) * supply_scale) / 255);
                            lv.w = static_cast<uint8_t>((uint16_t(lv.w) * supply_scale) / 255);
                        }
                    } else {
                        cf.pwm = static_cast<uint16_t>((uint32_t(cf.pwm) * supply_scale) / 255);
                        LightValue& lv = cf.pixels[0];
                        lv.r = static_cast<uint8_t>((uint16_t(lv.r) * supply_scale) / 255);
                        lv.g = static_cast<uint8_t>((uint16_t(lv.g) * supply_scale) / 255);
                        lv.b = static_cast<uint8_t>((uint16_t(lv.b) * supply_scale) / 255);
                        lv.w = static_cast<uint8_t>((uint16_t(lv.w) * supply_scale) / 255);
                    }
                    ch_active_mw[ch] = (ch_active_mw[ch] * supply_scale) / 255ULL;
                }
            }
        }
    }

    // Recalculate total_active_mw after supply domain scaling
    total_active_mw = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        if (cfg.channels[ch].enabled) {
            total_active_mw += ch_active_mw[ch];
        }
    }

    // 4. Determine if global budget scaling is needed
    uint8_t power_scale = 255;
    const uint64_t post_supply_total_mw = total_idle_mw + total_active_mw;
    if (limit_mw_ > 0 && post_supply_total_mw > limit_mw_) {
        throttled = true;
        if (total_active_mw > 0) {
            uint64_t available_active_mw = (limit_mw_ > total_idle_mw) ? (limit_mw_ - total_idle_mw) : 0;
            power_scale = static_cast<uint8_t>((available_active_mw * 255ULL) / total_active_mw);
        } else {
            power_scale = 0;
        }
    }

    // Baseline combined scale (soft-start * global limit)
    uint16_t combined_scale = (uint16_t(ss_factor) * power_scale) / 255;
    uint16_t final_scale = combined_scale;

    // 5. Bounded power-step control (LG-HW-004 §18/§19)
    if (limit_mw_ > 0 && ramp_ms_ > 0) {
        const uint64_t max_step_mw = std::max<uint64_t>(1, (uint64_t(limit_mw_) * frame_period_ms) / ramp_ms_);
        const uint64_t candidate_active_mw = (total_active_mw * combined_scale) / 255ULL;
        const uint64_t candidate_total_mw = total_idle_mw + candidate_active_mw;

        if (candidate_total_mw > previous_limited_power_mw_ + max_step_mw) {
            const uint64_t max_allowed_total_mw = previous_limited_power_mw_ + max_step_mw;
            power_step_clamps_++;
            throttled = true;

            const uint64_t allowed_active_mw = (max_allowed_total_mw > total_idle_mw) ? (max_allowed_total_mw - total_idle_mw) : 0;
            const uint16_t step_scale = (total_active_mw > 0) ? static_cast<uint16_t>((allowed_active_mw * 255ULL) / total_active_mw) : 0;
            final_scale = std::min<uint16_t>(combined_scale, step_scale);
        }
    }

    if (throttled) {
        throttling_events_++;
    }

    // 6. Apply final scale globally across all active channels (preserving relative ratios)
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
    }

    // 7. HW-005.13: Recalculate modeled consumption from final actual output values
    uint64_t actual_post_idle_mw = 0;
    uint64_t actual_post_active_mw = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& ccfg = cfg.channels[ch];
        if (!ccfg.enabled) continue;

        const auto hw = channel_hardware(ch);
        const ChannelFrame& cf = frame.channels[ch];

        if (hw.kind == ChannelKind::Addressable) {
            uint16_t pcount = std::min<uint16_t>(ccfg.pixel_count, MAX_PIXELS);
            actual_post_idle_mw += uint64_t(pcount) * kPixelModel.idle_mw;
            const bool is_rgbw = (ccfg.addressable_type == AddressableType::SK6812);
            for (uint16_t p = 0; p < pcount; ++p) {
                const LightValue& lv = cf.pixels[p];
                if (is_rgbw) {
                    uint64_t sum = uint64_t(lv.r) + lv.g + lv.b + lv.w;
                    actual_post_active_mw += (sum * kPixelModel.max_rgbw_mw) / 1020ULL;
                } else {
                    uint64_t sum = uint64_t(lv.r) + lv.g + lv.b;
                    actual_post_active_mw += (sum * kPixelModel.max_rgb_mw) / 765ULL;
                }
            }
        } else {
            if (ccfg.rated_full_power_mw > 0) {
                actual_post_active_mw += (uint64_t(cf.pwm) * ccfg.rated_full_power_mw) / 4095ULL;
            }
        }
    }

    limited_power_mw_ = static_cast<PowerMilliwatts>(
        std::min<uint64_t>(actual_post_idle_mw + actual_post_active_mw, UINT32_MAX));

    // 8. Track power step delta
    const PowerMilliwatts step_mw = (limited_power_mw_ > previous_limited_power_mw_) ?
                                    (limited_power_mw_ - previous_limited_power_mw_) : 0;
    if (step_mw > max_observed_step_mw_) {
        max_observed_step_mw_ = step_mw;
    }
    previous_limited_power_mw_ = limited_power_mw_;

    return static_cast<PowerMilliwatts>(std::min<uint64_t>(total_mw, UINT32_MAX));
}

} // namespace lg
