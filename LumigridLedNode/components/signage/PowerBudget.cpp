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

uint32_t PowerBudget::enforce(const AddressableLeds& pixels,
                              const Pca9685& pwm,
                              uint8_t& brightness) {
    if (limit_mw_ == 0) return 0; // No limit set

    // Estimate pixel power: sum of (R+G+B) across all pixels
    // Power per pixel = (R+G+B) / 765 * 60mA * 5V = (R+G+B) * 300 / 765
    // We use the brightness-scaled value since that's what actually gets sent.
    uint32_t total_pixel_mw = 0;
    // Since we can't access the raw buffer from the const interface,
    // approximate from brightness: average full-on pixel is ~180/255 duty.
    // Total pixels = 8 lanes * 30 pixels = 240 pixels.
    // Worst case per pixel at current brightness:
    const uint32_t pixel_fraction = (uint32_t(brightness) * 3 * kPixelMaxMw) / (255 * 3);
    total_pixel_mw = pixel_fraction * 240; // 8 lanes × 30 pixels

    // Estimate PWM power from current duty values
    uint32_t total_pwm_mw = 0;
    const auto& values = pwm.values();
    for (size_t ch = 0; ch < Pca9685::kChannels; ++ch) {
        total_pwm_mw += (uint32_t(values[ch]) * kPwmChannelMaxMw) / 4095;
    }

    const uint32_t total_mw = total_pixel_mw + total_pwm_mw;

    if (total_mw > limit_mw_) {
        // Scale brightness proportionally
        const uint32_t scaled = (uint32_t(brightness) * limit_mw_) / total_mw;
        brightness = static_cast<uint8_t>(std::min(scaled, uint32_t(255)));
    }

    return total_mw;
}

} // namespace lg
