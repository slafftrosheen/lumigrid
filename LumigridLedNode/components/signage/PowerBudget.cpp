// components/signage/PowerBudget.cpp
#include "PowerBudget.hpp"
#include "ChannelHardware.hpp"
#include "esp_timer.h"
#include <algorithm>

namespace lg {

void PowerBudget::set_soft_start(bool enabled, uint16_t ramp_ms) {
    soft_start_enabled_ = enabled;
    ramp_ms_ = ramp_ms > 0 ? ramp_ms : 1;
}

void PowerBudget::on_enable() {
    enable_ms_ = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint8_t PowerBudget::soft_start_factor(uint32_t ms_since_enable) const {
    if (!soft_start_enabled_) return 255;
    if (ms_since_enable >= ramp_ms_) return 255;
    // Linear ramp from 0 to 255 over ramp_ms_
    return static_cast<uint8_t>((uint32_t(ms_since_enable) * 255) / ramp_ms_);
}

uint32_t PowerBudget::enforce(RenderFrame& frame, const DeviceConfig& cfg, uint32_t current_ms) {
    // 1. Calculate soft-start factor
    uint8_t ss_factor = 255;
    if (soft_start_enabled_) {
        uint32_t ms_since_enable = current_ms - enable_ms_;
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

    // 3. Determine if global scaling is needed
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

    if (throttled) {
        throttling_events_++;
    }

    // Combine soft start and power scale
    const uint16_t final_scale = (uint16_t(ss_factor) * power_scale) / 255;

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

    return total_mw;
}

} // namespace lg
