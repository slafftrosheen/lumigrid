#include "AuditLog.hpp"
#include <cstring>
#include <algorithm>
#include "esp_timer.h"
#include "esp_log.h"

namespace lg::api {

static const char* TAG = "audit_log";

void AuditLog::log(ApiRole role, const char* action, const char* detail) {
    std::lock_guard<std::mutex> lock(mutex_);

    AuditEvent& event = events_[head_];
    
    // Use system uptime in seconds for timestamps
    event.timestamp_s = static_cast<uint32_t>(esp_timer_get_time() / 1'000'000);
    event.role = role;
    
    std::strncpy(event.action, action ? action : "", sizeof(event.action) - 1);
    event.action[sizeof(event.action) - 1] = '\0';
    
    std::strncpy(event.detail, detail ? detail : "", sizeof(event.detail) - 1);
    event.detail[sizeof(event.detail) - 1] = '\0';

    head_ = (head_ + 1) % kMaxEvents;
    if (count_ < kMaxEvents) {
        count_++;
    }
    
    ESP_LOGI(TAG, "[Audit] Role=%d Action='%s' Detail='%s'", static_cast<int>(role), action, detail);
}

size_t AuditLog::get_events(AuditEvent* out_events, size_t max_events) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!out_events || max_events == 0) return 0;

    size_t returned = std::min(count_, max_events);
    
    // To return chronological order: oldest first.
    // The oldest event is at index (head_ - count_ + kMaxEvents) % kMaxEvents
    size_t read_idx = (head_ + kMaxEvents - count_) % kMaxEvents;
    
    for (size_t i = 0; i < returned; ++i) {
        out_events[i] = events_[read_idx];
        read_idx = (read_idx + 1) % kMaxEvents;
    }

    return returned;
}

} // namespace lg::api
