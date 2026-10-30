#include "ClockDiscipline.hpp"

#include "esp_timer.h"

#include <algorithm>
#include <limits>

namespace lg {
namespace {

constexpr int64_t kLargeCorrectionUs = 100000;
constexpr int64_t kMaxSlewPerPacketUs = 2000;
constexpr uint64_t kLockTimeoutUs = 3000000;

// Wire timestamps are unsigned milliseconds; the timer and offsets are signed
// microseconds. Reject unrepresentable input before multiplication or mutation.
bool timestamp_us(uint64_t milliseconds, int64_t& result) {
    if (milliseconds > static_cast<uint64_t>(INT64_MAX) / 1000ULL) return false;
    result = static_cast<int64_t>(milliseconds * 1000ULL);
    return true;
}

int64_t saturating_add(int64_t left, int64_t right) {
    if (right > 0 && left > INT64_MAX - right) return INT64_MAX;
    if (right < 0 && left < INT64_MIN - right) return INT64_MIN;
    return left + right;
}

int64_t saturating_subtract(int64_t left, int64_t right) {
    if (right > 0 && left < INT64_MIN + right) return INT64_MIN;
    if (right < 0 && left > INT64_MAX + right) return INT64_MAX;
    return left - right;
}

uint64_t sample_age_us(uint64_t now, uint64_t last) {
    // A timer discontinuity is not a fresh update.
    return now >= last ? now - last : UINT64_MAX;
}

uint64_t abs_i64(int64_t value) {
    return value < 0
        ? static_cast<uint64_t>(-(value + 1)) + 1
        : static_cast<uint64_t>(value);
}

} // namespace

uint64_t ClockDiscipline::monotonic_us() {
    const int64_t value = esp_timer_get_time();
    return value > 0 ? static_cast<uint64_t>(value) : 0;
}

uint64_t ClockDiscipline::now_ms() const {
    const int64_t mono_us = static_cast<int64_t>(monotonic_us());

    portENTER_CRITICAL(&mutex_);
    const int64_t adjusted_us = saturating_add(mono_us, offset_us_);
    portEXIT_CRITICAL(&mutex_);

    return adjusted_us > 0 ? static_cast<uint64_t>(adjusted_us) / 1000ULL : 0;
}

bool ClockDiscipline::has_epoch() const {
    portENTER_CRITICAL(&mutex_);
    const bool epoch_valid = epoch_valid_;
    portEXIT_CRITICAL(&mutex_);
    return epoch_valid;
}

void ClockDiscipline::set_epoch(uint64_t epoch_ms) {
    const int64_t mono_us = static_cast<int64_t>(monotonic_us());
    int64_t target_us = 0;
    if (!timestamp_us(epoch_ms, target_us)) return;

    portENTER_CRITICAL(&mutex_);
    offset_us_ = target_us - mono_us;
    epoch_valid_ = true;
    locked_ = true;
    jitter_us_ = 0;
    last_error_us_ = 0;
    last_update_us_ = static_cast<uint64_t>(mono_us);
    portEXIT_CRITICAL(&mutex_);
}

void ClockDiscipline::discipline(uint64_t master_clock_ms, bool master_has_epoch) {
    const int64_t mono_us = static_cast<int64_t>(monotonic_us());
    int64_t target_us = 0;
    if (!timestamp_us(master_clock_ms, target_us)) return;
    const int64_t candidate_offset = target_us - mono_us;

    portENTER_CRITICAL(&mutex_);
    const int64_t error_us = saturating_subtract(candidate_offset, offset_us_);
    const bool fresh = locked_ &&
        sample_age_us(static_cast<uint64_t>(mono_us), last_update_us_) <= kLockTimeoutUs;
    const bool domain_changed = epoch_valid_ != master_has_epoch;

    if (!fresh || domain_changed || abs_i64(error_us) > kLargeCorrectionUs) {
        offset_us_ = candidate_offset;
    } else {
        offset_us_ += std::max(
            -kMaxSlewPerPacketUs,
            std::min(error_us, kMaxSlewPerPacketUs)
        );
    }

    const uint64_t absolute_error = abs_i64(error_us);
    const uint32_t bounded_error = absolute_error > std::numeric_limits<uint32_t>::max()
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(absolute_error);
    jitter_us_ = static_cast<uint32_t>(
        (uint64_t(jitter_us_) * 7ULL + bounded_error) / 8ULL
    );
    last_error_us_ = error_us > std::numeric_limits<int32_t>::max()
        ? std::numeric_limits<int32_t>::max()
        : error_us < std::numeric_limits<int32_t>::min()
            ? std::numeric_limits<int32_t>::min()
            : static_cast<int32_t>(error_us);
    // The flag describes the clock we now follow, not any source used earlier.
    epoch_valid_ = master_has_epoch;
    locked_ = true;
    last_update_us_ = static_cast<uint64_t>(mono_us);
    portEXIT_CRITICAL(&mutex_);
}

ClockSyncStatus ClockDiscipline::status() const {
    const uint64_t now_us = monotonic_us();

    portENTER_CRITICAL(&mutex_);
    ClockSyncStatus result;
    result.epoch_valid = epoch_valid_;
    const uint64_t age_us = sample_age_us(now_us, last_update_us_);
    result.locked = locked_ && age_us <= kLockTimeoutUs;
    result.offset_ms = last_error_us_ / 1000;
    result.jitter_us = jitter_us_;
    // locked_ also records whether any valid sample exists, including at t=0.
    result.last_update_age_ms = !locked_
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(std::min<uint64_t>(
              age_us / 1000ULL, std::numeric_limits<uint32_t>::max()));
    portEXIT_CRITICAL(&mutex_);

    return result;
}

} // namespace lg
