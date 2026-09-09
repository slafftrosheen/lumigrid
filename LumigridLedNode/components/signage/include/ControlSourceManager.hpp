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

        for (int i = 7; i >= 0; --i) {
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if ((states[i].zone_mask & z_bit) && (states[i].layer_mask & l_bit)) {
                    return static_cast<ControlSource>(i);
                }
            }
        }
        return ControlSource::Idle;
    }

    ControlSource resolve_fixture(uint8_t fixture_idx, uint8_t zone_idx, uint32_t current_ms) const {
        const uint16_t f_bit = (fixture_idx < 16) ? (1U << fixture_idx) : 0;
        const uint8_t  z_bit = (zone_idx < 8) ? (1U << zone_idx) : 0;

        // 1. Direct fixture-level override takes precedence
        for (int i = 7; i >= 0; --i) {
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if (states[i].fixture_mask & f_bit) {
                    return static_cast<ControlSource>(i);
                }
            }
        }

        // 2. Fallback to zone-level ownership
        for (int i = 7; i >= 0; --i) {
            if (states[i].active) {
                if (states[i].expires_ms != 0 && current_ms >= states[i].expires_ms) {
                    continue;
                }
                if (states[i].zone_mask & z_bit) {
                    return static_cast<ControlSource>(i);
                }
            }
        }

        return ControlSource::Idle;
    }
};

/// Logical control source arbitration engine.
///
/// Resolves which control source (LiveDmx, ManualOverride, Scheduler,
/// ScenePlayback, Fallback) owns each zone, layer, and fixture.
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
                 uint16_t fixture_mask = 0,
                 bool takeover = false);
    
    // Release control for a specific source.
    void release(ControlSource source);

    // Get instantaneous thread-safe snapshot for lock-free frame rendering
    ControlSnapshot get_snapshot(uint32_t current_ms);

    // Resolve which source owns a specific zone and layer at this moment.
    ControlSource resolve(uint8_t zone_idx, uint8_t layer_idx, uint32_t current_ms) const;

    // Resolve which source owns a specific fixture at this moment (fixture override takes precedence).
    ControlSource resolve_fixture(uint8_t fixture_idx, uint8_t zone_idx, uint32_t current_ms) const;

    // Returns the highest active owner across the node.
    ControlSource current_owner() const { return current_owner_; }

    const ControlState& state(ControlSource source) const {
        return states_[static_cast<uint8_t>(source)];
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    ControlState states_[8]; // 8 ControlSource enum values
    ControlSource current_owner_ = ControlSource::Idle;

    void evaluate_priority(uint32_t current_ms);
};

} // namespace lg
