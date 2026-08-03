#include "ClockDiscipline.hpp"

#include "esp_timer.h"

#include <algorithm>
#include <limits>

namespace lg {
namespace {

constexpr int64_t kLargeCorrectionUs = 100000;
constexpr int64_t kMaxSlewPerPacketUs = 2000;

uint64_t abs_i64(int64_t value) {
    return value < 0
        ? static_cast<uint64_t>(-(value + 1)) + 1
        : static_cast<uint64_t>(value);
}

} // namespace

uint64_t ClockDiscipline::monotonic_us() {
    return static_cast<uint64_t>(esp_timer_get_time());
}

uint64_t ClockDiscipline::now_ms() const {
    const int64_t mono_us = static_cast<int64_t>(monotonic_us());

    portENTER_CRITICAL(&mutex_);
    const int64_t adjusted_us = mono_us + offset_us_;
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
    const int64_t target_us = static_cast<int64_t>(epoch_ms * 1000ULL);

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
    const int64_t target_us = static_cast<int64_t>(master_clock_ms * 1000ULL);
    const int64_t candidate_offset = target_us - mono_us;

    portENTER_CRITICAL(&mutex_);
    const int64_t error_us = candidate_offset - offset_us_;

    if (!locked_ || abs_i64(error_us) > kLargeCorrectionUs) {
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
    epoch_valid_ = epoch_valid_ || master_has_epoch;
    locked_ = true;
    last_update_us_ = static_cast<uint64_t>(mono_us);
    portEXIT_CRITICAL(&mutex_);
}

ClockSyncStatus ClockDiscipline::status() const {
    const uint64_t now_us = monotonic_us();

    portENTER_CRITICAL(&mutex_);
    ClockSyncStatus result;
    result.epoch_valid = epoch_valid_;
    result.locked = locked_;
    result.offset_ms = last_error_us_ / 1000;
    result.jitter_us = jitter_us_;
    result.last_update_age_ms = last_update_us_ == 0
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>((now_us - last_update_us_) / 1000ULL);
    portEXIT_CRITICAL(&mutex_);

    return result;
}

} // namespace lg
