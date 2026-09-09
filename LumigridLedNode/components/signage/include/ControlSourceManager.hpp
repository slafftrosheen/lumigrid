// components/signage/include/ControlSourceManager.hpp
#pragma once
#include "ControlSource.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lg {

/// Logical control source arbitration engine.
///
/// Resolves which control source (LiveDmx, ManualOverride, Scheduler,
/// ScenePlayback, Fallback) owns each zone and layer at any given instant.
/// Does not touch hardware drivers; physical output commits belong to OutputScheduler.
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
                 bool takeover = false);
    
    // Release control for a specific source.
    void release(ControlSource source);

    // Resolve which source owns a specific zone and layer at this moment.
    ControlSource resolve(uint8_t zone_idx, uint8_t layer_idx, uint32_t current_ms) const;

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
