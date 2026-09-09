// components/signage/PowerBudget.cpp
#include "PowerBudget.hpp"
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

    // 2. Per-fixture power calculation and local fixture limit enforcement
    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fix = cfg.fixtures[f];
        bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                       fix.profile == FixtureProfile::PWM_RGB ||
                       fix.profile == FixtureProfile::PWM_RGBW);
        if (fix.num_pixels == 0 && !is_pwm) continue;

        FixtureFrame& ff = frame.fixtures[f];

        uint32_t fixture_idle_mw = 0;
        uint32_t fixture_active_mw = 0;

        if (!is_pwm) {
            uint16_t pcount = std::min<uint16_t>(fix.num_pixels, MAX_PIXELS);
            fixture_idle_mw = static_cast<uint32_t>(pcount) * kPixelIdleMw;
            for (uint16_t p = 0; p < pcount; ++p) {
                const LightValue& lv = ff.pixels[p];
                uint32_t sum = uint32_t(lv.r) + lv.g + lv.b + lv.w;
                fixture_active_mw += (sum * kPixelActiveMw) / 765;
            }

            // Enforce local fixture limit if configured
            if (fix.max_milliwatts > 0) {
                uint32_t fixture_total = fixture_idle_mw + fixture_active_mw;
                if (fixture_total > fix.max_milliwatts) {
                    throttled = true;
                    if (fix.max_milliwatts <= fixture_idle_mw) {
                        for (uint16_t p = 0; p < pcount; ++p) {
                            ff.pixels[p] = {0, 0, 0, 0};
                        }
                        fixture_active_mw = 0;
                    } else if (fixture_active_mw > 0) {
                        uint32_t allowed_active = fix.max_milliwatts - fixture_idle_mw;
                        uint8_t local_scale = static_cast<uint8_t>((uint64_t(allowed_active) * 255) / fixture_active_mw);
                        for (uint16_t p = 0; p < pcount; ++p) {
                            LightValue& lv = ff.pixels[p];
                            lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                            lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                            lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                            lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                        }
                        fixture_active_mw = (fixture_active_mw * local_scale) / 255;
                    }
                }
            }
        } else {
            // PWM fixtures have negligible quiescent consumption
            uint8_t num_channels = 1;
            if (fix.profile == FixtureProfile::PWM_RGB) num_channels = 3;
            else if (fix.profile == FixtureProfile::PWM_RGBW) num_channels = 4;

            for (uint8_t ch = 0; ch < num_channels; ++ch) {
                fixture_active_mw += (uint32_t(ff.pwm[ch]) * kPwmChannelMaxMw) / 4095;
            }

            // Enforce local fixture limit if configured
            if (fix.max_milliwatts > 0 && fixture_active_mw > fix.max_milliwatts && fixture_active_mw > 0) {
                throttled = true;
                uint8_t local_scale = static_cast<uint8_t>((uint64_t(fix.max_milliwatts) * 255) / fixture_active_mw);
                for (uint8_t ch = 0; ch < num_channels; ++ch) {
                    ff.pwm[ch] = static_cast<uint16_t>((uint32_t(ff.pwm[ch]) * local_scale) / 255);
                }
                LightValue& lv = ff.pixels[0];
                lv.r = static_cast<uint8_t>((uint16_t(lv.r) * local_scale) / 255);
                lv.g = static_cast<uint8_t>((uint16_t(lv.g) * local_scale) / 255);
                lv.b = static_cast<uint8_t>((uint16_t(lv.b) * local_scale) / 255);
                lv.w = static_cast<uint8_t>((uint16_t(lv.w) * local_scale) / 255);
                fixture_active_mw = (fixture_active_mw * local_scale) / 255;
            }
        }

        total_idle_mw += fixture_idle_mw;
        total_active_mw += fixture_active_mw;
    }

    const uint32_t total_mw = total_idle_mw + total_active_mw;
    last_power_mw_ = total_mw;

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
        for (uint8_t f = 0; f < 16; ++f) {
            FixtureFrame& ff = frame.fixtures[f];
            const FixtureConfig& fix = cfg.fixtures[f];
            bool is_pwm = (fix.profile == FixtureProfile::PWM_Single ||
                           fix.profile == FixtureProfile::PWM_RGB ||
                           fix.profile == FixtureProfile::PWM_RGBW);
            if (fix.num_pixels == 0 && !is_pwm) continue;

            if (!is_pwm) {
                uint16_t pcount = std::min<uint16_t>(fix.num_pixels, MAX_PIXELS);
                for (uint16_t p = 0; p < pcount; ++p) {
                    LightValue& lv = ff.pixels[p];
                    lv.r = static_cast<uint8_t>((uint16_t(lv.r) * final_scale) / 255);
                    lv.g = static_cast<uint8_t>((uint16_t(lv.g) * final_scale) / 255);
                    lv.b = static_cast<uint8_t>((uint16_t(lv.b) * final_scale) / 255);
                    lv.w = static_cast<uint8_t>((uint16_t(lv.w) * final_scale) / 255);
                }
            } else {
                for (uint8_t ch = 0; ch < 4; ++ch) {
                    ff.pwm[ch] = static_cast<uint16_t>((uint32_t(ff.pwm[ch]) * final_scale) / 255);
                }
                LightValue& lv = ff.pixels[0];
                lv.r = static_cast<uint8_t>((uint16_t(lv.r) * final_scale) / 255);
                lv.g = static_cast<uint8_t>((uint16_t(lv.g) * final_scale) / 255);
                lv.b = static_cast<uint8_t>((uint16_t(lv.b) * final_scale) / 255);
                lv.w = static_cast<uint8_t>((uint16_t(lv.w) * final_scale) / 255);
            }
        }
    }

    return total_mw;
}

} // namespace lg
