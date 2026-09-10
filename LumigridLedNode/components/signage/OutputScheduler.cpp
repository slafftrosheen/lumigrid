// components/signage/OutputScheduler.cpp
#include "OutputScheduler.hpp"
#include "ConfigStore.hpp"
#include <algorithm>

namespace lg {

esp_err_t OutputScheduler::schedule(
    const RenderFrame& frame,
    const DeviceConfig& device_cfg,
    OutputController& controller
) {
    // ── Pass 1: Validate entire render/output mapping before modifying driver state (Batch 6 Item 9) ──
    uint16_t rmt_lanes_claimed = 0;
    uint16_t pwm_channels_claimed = 0;

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        if (!ConfigStore::is_fixture_configured(fixture, device_cfg)) {
            continue;
        }

        const bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                             fixture.profile == FixtureProfile::PWM_RGB ||
                             fixture.profile == FixtureProfile::PWM_RGBW);
        const bool is_addressable = (fixture.profile == FixtureProfile::WS2812 ||
                                     fixture.profile == FixtureProfile::SK6812);

        if (!is_pwm && !is_addressable) {
            return ESP_ERR_INVALID_ARG;
        }

        if (is_addressable) {
            if (fixture.num_pixels == 0 || fixture.num_pixels > MAX_PIXELS) {
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
            if (rmt_lanes_claimed & (1U << phys.hardware_id)) {
                return ESP_ERR_INVALID_ARG; // Duplicate incompatible physical mapping
            }
            rmt_lanes_claimed |= (1U << phys.hardware_id);
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
                if (pwm_channels_claimed & (1U << phys.hardware_id)) {
                    return ESP_ERR_INVALID_ARG; // Duplicate incompatible physical mapping
                }
                pwm_channels_claimed |= (1U << phys.hardware_id);
            }
        }
    }

    // ── Pass 2: Driver state mutation & staging (Batch 7 Item 10: lock held across staging transaction) ──
    controller.lock();
    controller.pixels().clear_buffer();
    controller.pwm().set_all(0);

    for (uint8_t f = 0; f < 16; ++f) {
        const FixtureConfig& fixture = device_cfg.fixtures[f];
        if (!ConfigStore::is_fixture_configured(fixture, device_cfg)) {
            continue;
        }

        bool is_pwm = (fixture.profile == FixtureProfile::PWM_Single ||
                       fixture.profile == FixtureProfile::PWM_RGB ||
                       fixture.profile == FixtureProfile::PWM_RGBW);

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

    esp_err_t commit_err = controller.try_commit_locked();
    controller.unlock();
    return commit_err;
}

} // namespace lg


