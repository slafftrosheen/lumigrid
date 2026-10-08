#pragma once

#include "esp_err.h"

namespace lg {

/// Centralized NVS initialization owner (LG-HW-002 §9).
/// Prevents subsystems from independently initializing or silently erasing NVS.
class NvsRuntime {
public:
    static esp_err_t initialise();
    static bool ready();
    static esp_err_t last_error();
    static bool destructive_recovery_required();
};

} // namespace lg
