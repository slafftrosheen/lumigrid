// components/control/include/OperationJob.hpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace lg {

enum class JobType : uint8_t {
    None,
    Ota,
    Restore,
    BurnIn,
    ConfigMigration
};

enum class JobState : uint8_t {
    Queued,
    Preparing,
    Running,
    Verifying,
    Completed,
    Failed,
    Cancelled
};

inline const char* job_type_name(JobType type) {
    switch (type) {
        case JobType::Ota: return "ota";
        case JobType::Restore: return "restore";
        case JobType::BurnIn: return "burn_in";
        case JobType::ConfigMigration: return "config_migration";
        default: return "none";
    }
}

inline const char* job_state_name(JobState state) {
    switch (state) {
        case JobState::Queued: return "queued";
        case JobState::Preparing: return "preparing";
        case JobState::Running: return "running";
        case JobState::Verifying: return "verifying";
        case JobState::Completed: return "completed";
        case JobState::Failed: return "failed";
        case JobState::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

struct OperationJob {
    uint32_t job_id{0};
    JobType type{JobType::None};
    JobState state{JobState::Queued};
    uint8_t progress{0}; // 0 - 100%
    uint32_t started_ms{0};
    uint32_t updated_ms{0};
    int32_t error_code{0};
    char error_detail[64]{};
};

} // namespace lg
