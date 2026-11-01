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
    d.limit_mw = limit_mw_;
    d.throttling_events = throttling_events_;
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

namespace {
power_model::Load frame_channel_load(const ChannelFrame& frame, const DeviceConfig& cfg, uint8_t ch) {
    power_model::Load load;
    const auto& channel = cfg.channels[ch];
    if (!channel.enabled) return load;
    if (channel_hardware(ch).kind == ChannelKind::Addressable) {
        const uint16_t count = std::min<uint16_t>(channel.pixel_count, MAX_PIXELS);
        load.idle = power_model::units(uint64_t(count) * power_model::kPixel.idle_mw);
        const bool rgbw = channel.addressable_type == AddressableType::SK6812;
        for (uint16_t p = 0; p < count; ++p) {
            const auto& value = frame.pixels[p];
            load.active += power_model::pixel_active(value.r, value.g, value.b, rgbw ? value.w : 0);
        }
    } else {
        load.active = power_model::pwm_active(frame.pwm, power_model::pwm_rating(cfg, ch));
    }
    return load;
}

void scale_frame_channel(ChannelFrame& frame, const ChannelConfig& cfg, uint8_t ch, uint16_t scale) {
    const bool addressable = channel_hardware(ch).kind == ChannelKind::Addressable;
    const uint16_t count = addressable ? std::min<uint16_t>(cfg.pixel_count, MAX_PIXELS) : 1;
    for (uint16_t p = 0; p < count; ++p) {
        auto& value = frame.pixels[p];
        value.r = static_cast<uint8_t>((uint16_t(value.r) * scale) / power_model::kFullScale);
        value.g = static_cast<uint8_t>((uint16_t(value.g) * scale) / power_model::kFullScale);
        value.b = static_cast<uint8_t>((uint16_t(value.b) * scale) / power_model::kFullScale);
        value.w = static_cast<uint8_t>((uint16_t(value.w) * scale) / power_model::kFullScale);
    }
    if (!addressable) {
        frame.pwm = static_cast<uint16_t>((uint32_t(std::min<uint16_t>(frame.pwm, 4095)) * scale) /
                                          power_model::kFullScale);
    }
}
} // namespace

PowerMilliwatts PowerBudget::enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms) {
    uint32_t frame_period_ms = 20;
    if (last_enforce_ms_ != 0) {
        frame_period_ms = std::min<uint32_t>(current_ms - last_enforce_ms_, 100);
    }
    last_enforce_ms_ = current_ms;

    power_model::Load channels[kChannelCount]{};
    power_model::Load requested;
    uint16_t enabled_mask = 0;
    unmodeled_channel_mask_ = 0;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        if (!cfg.channels[ch].enabled) continue;
        enabled_mask |= static_cast<uint16_t>(1u << ch);
        channels[ch] = frame_channel_load(frame.channels[ch], cfg, ch);
        requested += channels[ch];
        if (channel_hardware(ch).kind != ChannelKind::Addressable && power_model::pwm_rating(cfg, ch) == 0) {
            unmodeled_channel_mask_ |= static_cast<uint16_t>(1u << ch);
        }
    }
    requested_power_mw_ = power_model::reported_mw(requested.total());
    output_request_ = {};
    output_request_.requested_mw = requested_power_mw_;
    estimate_complete_ = unmodeled_channel_mask_ == 0;
    model_state_ = power_model::model_state(enabled_mask, unmodeled_channel_mask_);
    if (!estimate_complete_) ++unknown_load_warnings_;

    // Scale only active demand. Re-estimate the quantized channel values after
    // each stage so a later domain/global limit never uses fictitious capacity.
    auto scale_channel = [&](uint8_t ch, uint16_t scale) {
        if (!cfg.channels[ch].enabled || scale == power_model::kFullScale) return;
        scale_frame_channel(frame.channels[ch], cfg.channels[ch], ch, scale);
        channels[ch] = frame_channel_load(frame.channels[ch], cfg, ch);
    };
    auto domain_load = [&](uint16_t mask) {
        power_model::Load load;
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
            if ((mask & (1u << ch)) != 0) load += channels[ch];
        }
        return load;
    };

    bool throttled = false;
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const uint16_t scale = power_model::scale_for_limit(channels[ch], power_model::channel_limit(cfg, ch));
        if (scale < power_model::kFullScale) {
            throttled = true;
            ++channel_throttle_events_;
            output_request_.limited_channel_mask |= static_cast<uint16_t>(1u << ch);
            scale_channel(ch, scale);
        }
    }

    for (uint8_t supply = 0; supply < kMaxPowerSupplies; ++supply) {
        const auto& psu = cfg.power_supplies[supply];
        last_supplies_[supply] = psu;
        if (!psu.enabled || psu.channel_mask == 0) continue;
        // Zero capacity also extinguishes unknown-rated PWM; an incomplete
        // model must not turn an explicit zero-capacity domain into free power.
        const uint64_t capacity = psu.usable_power_mw();
        const uint16_t scale = capacity == 0 ? 0 : power_model::scale_for_units(
            domain_load(psu.channel_mask), power_model::units(capacity));
        if (scale < power_model::kFullScale) {
            throttled = true;
            ++supply_throttle_events_[supply];
            output_request_.limited_supply_mask |= static_cast<uint8_t>(1u << supply);
            for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
                if ((psu.channel_mask & (1u << ch)) != 0) scale_channel(ch, scale);
            }
        }
    }

    const power_model::Load post_supply = domain_load(UINT16_MAX);
    const uint16_t budget_scale = power_model::scale_for_limit(post_supply, limit_mw_);
    if (budget_scale < power_model::kFullScale) throttled = true;
    const uint16_t start_scale = (uint16_t(soft_start_factor(current_ms - enable_ms_)) *
                                 power_model::kFullScale) / 255;
    uint16_t final_scale = static_cast<uint16_t>((uint32_t(start_scale) * budget_scale) /
                                               power_model::kFullScale);

    // Preserve the existing bounded rise policy, including when the optional
    // enable ramp is disabled. Fixed idle cannot be extinguished by software.
    if (limit_mw_ > 0 && ramp_ms_ > 0) {
        const uint64_t step_mw = std::max<uint64_t>(1, (uint64_t(limit_mw_) * frame_period_ms) / ramp_ms_);
        const uint64_t allowed = power_model::units(uint64_t(previous_limited_power_mw_) + step_mw);
        const uint16_t step_scale = power_model::scale_for_units(post_supply, allowed);
        if (step_scale < final_scale) {
            final_scale = step_scale;
            ++power_step_clamps_;
            throttled = true;
        }
    }
    if (throttled) ++throttling_events_;
    output_request_.throttled = throttled;
    if (final_scale < power_model::kFullScale) {
        for (uint8_t ch = 0; ch < kChannelCount; ++ch) scale_channel(ch, final_scale);
    }

    const power_model::Load limited = domain_load(UINT16_MAX);
    limited_power_mw_ = power_model::reported_mw(limited.total());
    for (uint8_t supply = 0; supply < kMaxPowerSupplies; ++supply) {
        const auto& psu = cfg.power_supplies[supply];
        supply_last_load_mw_[supply] = psu.enabled
            ? power_model::milliwatts(domain_load(psu.channel_mask).total()) : 0;
    }

    const PowerMilliwatts step_mw = limited_power_mw_ > previous_limited_power_mw_
        ? limited_power_mw_ - previous_limited_power_mw_ : 0;
    max_observed_step_mw_ = std::max(max_observed_step_mw_, step_mw);
    previous_limited_power_mw_ = limited_power_mw_;
    return requested_power_mw_;
}

} // namespace lg
