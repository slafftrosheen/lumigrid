// components/signage/OutputScheduler.cpp
#include "OutputScheduler.hpp"
#include "ChannelHardware.hpp"
#include <algorithm>

namespace lg {

esp_err_t OutputScheduler::schedule(
    const RenderFrame& frame,
    const DeviceConfig& device_cfg,
    OutputController& controller
) {
    // ── Pass 1: Validate configured channels ──
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& ccfg = device_cfg.channels[ch];
        if (!ccfg.enabled) {
            continue;
        }

        const auto hw = channel_hardware(ch);
        if (hw.kind == ChannelKind::Addressable) {
            if (ccfg.pixel_count == 0 || ccfg.pixel_count > MAX_PIXELS) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    // ── Pass 2: Driver state mutation & staging via RAII OutputTransaction ──
    OutputTransaction tx = controller.begin_transaction();
    tx.pixels().clear_buffer();
    tx.pwm().set_all(0);

    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        const ChannelConfig& ccfg = device_cfg.channels[ch];
        if (!ccfg.enabled) {
            continue;
        }

        const auto hw = channel_hardware(ch);
        const ChannelFrame& cf = frame.channels[ch];

        if (hw.kind == ChannelKind::Addressable) {
            const uint16_t pcount = std::min<uint16_t>(ccfg.pixel_count, MAX_PIXELS);
            for (uint16_t px = 0; px < pcount; ++px) {
                const LightValue& lv = cf.pixels[px];
                tx.pixels().set(hw.hardware_id, px, lv.r, lv.g, lv.b, lv.w);
            }
        } else {
            tx.pwm().set(hw.hardware_id, cf.pwm);
        }
    }

    return tx.commit();
}

} // namespace lg


