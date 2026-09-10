// components/signage/OutputScheduler.cpp
#include "OutputScheduler.hpp"
#include <algorithm>

namespace lg {

esp_err_t OutputScheduler::schedule(
    const RenderFrame& frame,
    const DeviceConfig& device_cfg,
    OutputController& controller
) {
    // ── Pass 1: Validate entire render/output mapping before modifying driver state (Batch 5 Item 12) ──
    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        const bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                             fixture.profile == FixtureProfile::PWM_RGB ||
                             fixture.profile == FixtureProfile::PWM_RGBW);
        const bool is_addressable = (fixture.profile == FixtureProfile::WS2812 ||
                                     fixture.profile == FixtureProfile::SK6812);

        if (!is_pwm && !is_addressable) {
            return ESP_ERR_INVALID_ARG;
        }

        if (fixture.num_pixels == 0 && !is_pwm) {
            // Disabled / unconfigured addressable fixture
            continue;
        }

        if (is_addressable) {
            if (fixture.num_pixels > MAX_PIXELS) {
                return ESP_ERR_INVALID_ARG;
            }
            uint8_t p_idx = fixture.physical_output_index[0];
            if (p_idx >= 16) {
                return ESP_ERR_INVALID_ARG;
            }
            const PhysicalOutput& phys = device_cfg.physical_outputs[p_idx];
            if (!phys.enabled || phys.type != OutputType::RMT || phys.hardware_id >= AddressableDriver::kLanes) {
                return ESP_ERR_INVALID_ARG;
            }
        } else if (is_pwm) {
            uint8_t pwm_chans = (fixture.profile == FixtureProfile::PWM_Single) ? 1 :
                                (fixture.profile == FixtureProfile::PWM_RGB ? 3 : 4);
            for (uint8_t ch = 0; ch < pwm_chans; ++ch) {
                uint8_t p_idx = fixture.physical_output_index[ch];
                if (p_idx >= 16) {
                    return ESP_ERR_INVALID_ARG;
                }
                const PhysicalOutput& phys = device_cfg.physical_outputs[p_idx];
                if (!phys.enabled || phys.type != OutputType::PWM || phys.hardware_id >= Pca9685Driver::kChannels) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }

    // ── Pass 2: Driver state mutation & staging ──
    controller.pixels().clear_buffer();
    controller.pwm().set_all(0);

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                       fixture.profile == FixtureProfile::PWM_RGB ||
                       fixture.profile == FixtureProfile::PWM_RGBW);
        if (fixture.num_pixels == 0 && !is_pwm) {
            continue;
        }

        const FixtureFrame& fx_frame = frame.fixtures[f];

        if (!is_pwm) {
            uint8_t p_idx = fixture.physical_output_index[0];
            const PhysicalOutput& phys = device_cfg.physical_outputs[p_idx];
            for (uint16_t px = 0; px < fixture.num_pixels; ++px) {
                const LightValue& lv = fx_frame.pixels[px];
                controller.pixels().set(phys.hardware_id, px, lv.r, lv.g, lv.b, lv.w);
            }
        } else {
            uint8_t pwm_chans = (fixture.profile == FixtureProfile::PWM_Single) ? 1 :
                                (fixture.profile == FixtureProfile::PWM_RGB ? 3 : 4);
            for (uint8_t ch = 0; ch < pwm_chans; ++ch) {
                uint8_t p_idx = fixture.physical_output_index[ch];
                const PhysicalOutput& phys = device_cfg.physical_outputs[p_idx];
                controller.pwm().set(phys.hardware_id, fx_frame.pwm[ch]);
            }
        }
    }

    return controller.try_commit();
}

} // namespace lg

