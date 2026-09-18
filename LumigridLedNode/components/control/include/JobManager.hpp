// components/control/include/JobManager.hpp
#pragma once
#include "OperationJob.hpp"
#include <atomic>
#include <mutex>

namespace lg {

class JobManager {
public:
    static constexpr size_t kHistoryCapacity = 4;

    JobManager() = default;

    uint32_t start_job(JobType type);
    void update_progress(uint32_t job_id, JobState state, uint8_t progress, int32_t error = 0, const char* detail = nullptr);
    void complete_job(uint32_t job_id);
    void fail_job(uint32_t job_id, int32_t error, const char* detail = nullptr);
    void cancel_job(uint32_t job_id);
    bool get_job(uint32_t job_id, OperationJob& out) const;
    bool get_current(OperationJob& out) const;

private:
    std::atomic<uint32_t> next_id_{1};
    mutable std::mutex mutex_;
    OperationJob current_job_{};
    OperationJob history_[kHistoryCapacity]{};
};

} // namespace lg
