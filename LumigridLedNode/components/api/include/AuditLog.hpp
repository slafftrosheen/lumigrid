// components/api/include/AuditLog.hpp
#pragma once
#include <cstdint>
#include <mutex>
#include "ApiAuth.hpp"

namespace lg::api {

struct AuditEvent {
    uint32_t timestamp_s;
    ApiRole role;
    char action[32];
    char detail[64];
};

class AuditLog {
public:
    static AuditLog& instance() {
        static AuditLog s_instance;
        return s_instance;
    }

    // Log an event. Thread-safe.
    void log(ApiRole role, const char* action, const char* detail);

    // Retrieve events in chronological order
    // Returns the number of events copied into `out_events`
    size_t get_events(AuditEvent* out_events, size_t max_events);

private:
    AuditLog() = default;

    static constexpr size_t kMaxEvents = 50;
    AuditEvent events_[kMaxEvents] = {};
    size_t head_ = 0; // Where the next write goes
    size_t count_ = 0; // Number of valid events (up to kMaxEvents)
    std::mutex mutex_;
};

} // namespace lg::api
