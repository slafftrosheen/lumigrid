// components/signage/include/ControlSourceManager.hpp
#pragma once
#include "ControlSource.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

struct ControlSnapshot {
    ControlState states[8]{};
    ControlSource current_owner = ControlSource::Idle;

    ControlSource resolve(uint8_t zone_idx, uint8_t layer_idx, uint32_t current_ms) const {
        const uint8_t z_bit = (zone_idx < 8) ? (1 << zone_idx) : 0;
        const uint8_t l_bit = (layer_idx < 3) ? (1 << layer_idx) : 0;

        for (ControlSource src : kPriorityOrder) {
            uint8_t i = static_cast<uint8_t>(src);
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if ((states[i].zone_mask & z_bit) && (states[i].layer_mask & l_bit)) {
                    return src;
                }
            }
        }
        return ControlSource::Idle;
    }

    ControlSource resolve_channel(uint8_t channel_idx, uint32_t current_ms) const {
        const uint16_t ch_bit = (channel_idx < 16) ? (1U << channel_idx) : 0;
        for (ControlSource src : kPriorityOrder) {
            uint8_t i = static_cast<uint8_t>(src);
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if (states[i].channel_mask & ch_bit) {
                    return src;
                }
            }
        }
        return ControlSource::Idle;
    }

    ControlSource resolve_fixture(uint8_t fixture_idx, uint8_t zone_idx, uint32_t current_ms) const {
        const uint16_t f_bit = (fixture_idx < 16) ? (1U << fixture_idx) : 0;
        const uint8_t  z_bit = (zone_idx < 8) ? (1U << zone_idx) : 0;

        // Resolution order per Item 7:
        // 1. Explicit fixture/channel LiveDmx overrides take precedence
        for (ControlSource src : kPriorityOrder) {
            uint8_t i = static_cast<uint8_t>(src);
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if (states[i].channel_mask & f_bit) {
                    return src;
                }
            }
        }

        // 2. Zone/layer LiveDmx / zone ownership fallback
        for (ControlSource src : kPriorityOrder) {
            uint8_t i = static_cast<uint8_t>(src);
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if (states[i].zone_mask & z_bit) {
                    return src;
                }
            }
        }

        return ControlSource::Idle;
    }
};

/// Logical control source arbitration engine.
///
/// Resolves which control source (LiveDmx, ManualOverride, Scheduler,
/// ScenePlayback, Fallback) owns each channel. Zone/layer methods remain
/// compatibility paths for legacy live-stream inputs.
/// Physical output commits belong to OutputScheduler.
class ControlSourceManager {
public:
    ControlSourceManager();
    ~ControlSourceManager();

    esp_err_t init();

    // Request to take control at the specified source priority.
    // Returns true if control was granted or priority is already active.
    // A lease of 0 means indefinite (until explicitly released).
    bool acquire(ControlSource source, uint32_t lease_ms = 0,
                 uint8_t zone_mask = 0xFF, uint8_t layer_mask = 0x07,
                 uint16_t channel_mask = 0,
                 bool takeover = false);
    
    // Release control for a specific source.
    void release(ControlSource source);

    // Get instantaneous thread-safe snapshot for lock-free frame rendering
    ControlSnapshot get_snapshot(uint32_t current_ms);

    // ACTUAL RENDERING AUTHORITY:
    // Resolve which source owns a specific zone and layer at this moment.
    ControlSource resolve(uint8_t zone_idx, uint8_t layer_idx, uint32_t current_ms) const;

    // Legacy compatibility wrapper. New channel-first code should use resolve_channel().
    ControlSource resolve_fixture(uint8_t fixture_idx, uint8_t zone_idx, uint32_t current_ms) const;

    // DIAGNOSTIC DOMINANT SOURCE ONLY:
    // Returns the highest active owner across the node (thread-safe by value).
    // NOTE: This is a diagnostic dominant winner, NOT an authority for rendering decisions.
    // Use resolve() or resolve_fixture() for rendering.
    ControlSource current_owner() const;

    // Returns the control state snapshot for a source (thread-safe by value, Item 9).
    ControlState state(ControlSource source) const;

    bool is_estop_active() const {
        return state(ControlSource::EStop).active;
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    ControlState states_[8]; // 8 ControlSource enum values
    ControlSource current_owner_ = ControlSource::Idle;

    void evaluate_priority(uint32_t current_ms);
};

} // namespace lg
