#pragma once
#include "OutputController.hpp"
#include "esp_timer.h"

namespace lg {

enum class OutputPriority : uint8_t {
    Background = 0, // Fallback/Signage
    Signage = 1,    // Normal Signage
    ArtNet = 2,     // Network streaming
    Factory = 3,    // Factory Test
    EStop = 4       // Hardware safety
};

class OutputArbitrator {
public:
    OutputArbitrator(OutputController& controller);

    esp_err_t init();

    // Request to take control at the specified priority.
    // Returns true if control was granted or priority is already active.
    // A lease of 0 means indefinite (until explicitly released).
    bool acquire(OutputPriority priority, uint32_t lease_ms = 0);
    
    // Release control for a specific priority.
    void release(OutputPriority priority);

    // Attempt to begin a frame.
    // Returns true if the caller's priority is the highest active priority.
    // If true, the output is locked until commit_frame() or abort_frame().
    bool begin_frame(OutputPriority priority);

    // Get output references (only valid between begin_frame and commit_frame)
    AddressableLeds& pixels() { return controller_.pixels(); }
    Pca9685& pwm() { return controller_.pwm(); }
    OutputController& controller() { return controller_; }

    esp_err_t commit_frame(OutputPriority priority, bool enable_output = true);
    void abort_frame(OutputPriority priority);

private:
    OutputController& controller_;
    SemaphoreHandle_t mutex_ = nullptr;
    
    struct Lease {
        bool active = false;
        uint32_t expires_at_ms = 0; // 0 = indefinite
    };
    Lease leases_[5];
    
    OutputPriority current_owner_ = OutputPriority::Background;
    OutputPriority frame_lock_ = OutputPriority::Background;
    bool frame_active_ = false;

    void evaluate_priority(uint32_t current_ms);
};

} // namespace lg
