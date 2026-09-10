// components/signage/OutputScheduler.cpp
#include "OutputScheduler.hpp"
#include <algorithm>

namespace lg {

esp_err_t OutputScheduler::schedule(
    const RenderFrame& frame,
    const DeviceConfig& device_cfg,
    OutputController& controller
) {
    controller.pixels().clear();
    controller.pwm().clear();

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                       fixture.profile == FixtureProfile::PWM_RGB ||
                       fixture.profile == FixtureProfile::PWM_RGBW);
        if (fixture.num_pixels == 0 && !is_pwm) {
            continue;
        }

        const FixtureFrame& fx_frame = frame.fixtures[f];

        for (uint8_t i = 0; i < 4; ++i) {
            uint8_t p_idx = fixture.physical_output_index[i];
            if (p_idx >= 16) continue;
            
            const PhysicalOutput& phys = device_cfg.physical_outputs[p_idx];
            if (!phys.enabled) continue;

            if (phys.type == OutputType::RMT) {
                if (fixture.num_pixels > MAX_PIXELS) {
                    // Malformed configured size must not be silently clamped at runtime
                    continue;
                }
                for (uint16_t px = 0; px < fixture.num_pixels; ++px) {
                    const LightValue& lv = fx_frame.pixels[px];
                    controller.pixels().set(phys.hardware_id, px, lv.r, lv.g, lv.b, lv.w);
                }
            } else if (phys.type == OutputType::PWM) {
                controller.pwm().set(phys.hardware_id, fx_frame.pwm[i]);
            }
        }
    }

    return controller.try_commit();
}

} // namespace lg
