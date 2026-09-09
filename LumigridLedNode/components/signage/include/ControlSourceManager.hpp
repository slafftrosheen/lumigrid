// components/signage/include/ControlSourceManager.hpp
#pragma once
#include "OutputController.hpp"
#include "ControlSource.hpp"
#include "esp_timer.h"

namespace lg {

class ControlSourceManager {
public:
    ControlSourceManager(OutputController& controller);

    esp_err_t init();

    // Request to take control at the specified source priority.
    // Returns true if control was granted or priority is already active.
    // A lease of 0 means indefinite (until explicitly released).
    bool acquire(ControlSource source, uint32_t lease_ms = 0);
    
    // Release control for a specific source.
    void release(ControlSource source);

    // Attempt to begin a frame.
    // Returns true if the caller's source is the highest active priority.
    // If true, the output is locked until commit_frame() or abort_frame().
    bool begin_frame(ControlSource source);

    // Get output references (only valid between begin_frame and commit_frame)
    AddressableDriver& pixels() { return controller_.pixels(); }
    Pca9685Driver& pwm() { return controller_.pwm(); }
    OutputController& controller() { return controller_; }

    esp_err_t commit_frame(ControlSource source, bool enable_output = true);
    void abort_frame(ControlSource source);
    
    // Returns the active owner of the output.
    ControlSource current_owner() const { return current_owner_; }

private:
    OutputController& controller_;
    SemaphoreHandle_t mutex_ = nullptr;
    
    struct Lease {
        bool active = false;
        uint32_t expires_at_ms = 0; // 0 = indefinite
    };
    Lease leases_[8]; // 8 ControlSource enum values
    
    ControlSource current_owner_ = ControlSource::Idle;
    ControlSource frame_lock_ = ControlSource::Idle;
    bool frame_active_ = false;

    void evaluate_priority(uint32_t current_ms);
};

} // namespace lg
