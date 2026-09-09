// components/signage/OutputScheduler.cpp
#include "OutputScheduler.hpp"
#include <algorithm>

namespace lg {

void OutputScheduler::schedule(
    const RenderFrame& frame,
    const DeviceConfig& device_cfg,
    ControlSourceManager& source_manager,
    ControlSource source
) {
    if (!source_manager.begin_frame(source)) {
        return;
    }

    source_manager.pixels().clear();
    source_manager.pwm().clear();

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single || fixture.profile == FixtureProfile::PWM_RGB || fixture.profile == FixtureProfile::PWM_RGBW);
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
                uint16_t pcount = std::min<uint16_t>(fixture.num_pixels, MAX_PIXELS);
                for (uint16_t px = 0; px < pcount; ++px) {
                    Rgb c = fx_frame.pixels[px];
                    uint8_t w = 0;
                    
                    // Explicit support for SK6812 dedicated white channel
                    if (fixture.profile == FixtureProfile::SK6812) {
                        // Very naive RGB -> RGBW conversion just for proof of abstraction.
                        w = std::min({c.r, c.g, c.b});
                        c.r -= w;
                        c.g -= w;
                        c.b -= w;
                    }
                    
                    source_manager.pixels().set(phys.hardware_id, px, c.r, c.g, c.b, w);
                }
            } else if (phys.type == OutputType::PWM) {
                uint16_t duty = fx_frame.pwm;
                if (fixture.profile == FixtureProfile::PWM_RGB || fixture.profile == FixtureProfile::PWM_RGBW) {
                    Rgb c = fx_frame.pixels[0];
                    if (i == 0) duty = (static_cast<uint32_t>(c.r) * 4095) / 255;
                    else if (i == 1) duty = (static_cast<uint32_t>(c.g) * 4095) / 255;
                    else if (i == 2) duty = (static_cast<uint32_t>(c.b) * 4095) / 255;
                    else if (i == 3 && fixture.profile == FixtureProfile::PWM_RGBW) {
                        uint8_t w = std::min({c.r, c.g, c.b});
                        duty = (static_cast<uint32_t>(w) * 4095) / 255;
                    }
                }
                source_manager.pwm().set(phys.hardware_id, duty);
            }
        }
    }

    source_manager.commit_frame(source);
}

} // namespace lg
