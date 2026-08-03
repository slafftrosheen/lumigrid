#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

namespace lg {

struct ClockSyncStatus {
    bool epoch_valid = false;
    bool locked = false;
    int32_t offset_ms = 0;
    uint32_t jitter_us = 0;
    uint32_t last_update_age_ms = 0;
};

/// Keeps playback on a monotonic ESP timer while applying bounded corrections
/// from SNTP or a trusted sync leader. It deliberately never changes system time.
class ClockDiscipline {
public:
    uint64_t now_ms() const;
    bool has_epoch() const;

    /// Establish an epoch from locally synchronized SNTP system time.
    void set_epoch(uint64_t epoch_ms);

    /// Apply a remote heartbeat timestamp observed at the current monotonic time.
    void discipline(uint64_t master_clock_ms, bool master_has_epoch);

    ClockSyncStatus status() const;

private:
    static uint64_t monotonic_us();

    mutable portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
    int64_t offset_us_ = 0;
    bool epoch_valid_ = false;
    bool locked_ = false;
    uint32_t jitter_us_ = 0;
    int32_t last_error_us_ = 0;
    uint64_t last_update_us_ = 0;
};

} // namespace lg
