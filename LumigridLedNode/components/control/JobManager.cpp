#include "JobManager.hpp"
#include <cstring>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
static uint32_t current_time_ms() {
    return esp_log_timestamp();
}
#else
static uint32_t current_time_ms() {
    return 0;
}
#endif

namespace lg {

esp_err_t JobManager::start_job(JobType type, uint32_t* job_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (current_job_.job_id != 0) {
        if (current_job_.state == JobState::Queued ||
            current_job_.state == JobState::Preparing ||
            current_job_.state == JobState::Running ||
            current_job_.state == JobState::Verifying) {
            return ESP_ERR_INVALID_STATE;
        }

        // Terminal jobs (Completed, Failed, Cancelled) may be archived to history
        size_t hist_slot = current_job_.job_id % kHistoryCapacity;
        history_[hist_slot] = current_job_;
    }

    uint32_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    if (id == 0) {
        id = next_id_.fetch_add(1, std::memory_order_relaxed);
    }

    current_job_ = OperationJob{};
    current_job_.job_id = id;
    current_job_.type = type;
    current_job_.state = JobState::Queued;
    current_job_.progress = 0;
    current_job_.started_ms = current_time_ms();
    current_job_.updated_ms = current_job_.started_ms;

    if (job_id) {
        *job_id = id;
    }
    return ESP_OK;
}

void JobManager::update_progress(uint32_t job_id, JobState state, uint8_t progress, int32_t error, const char* detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_job_.job_id == job_id) {
        current_job_.state = state;
        current_job_.progress = (progress > 100) ? 100 : progress;
        current_job_.updated_ms = current_time_ms();
        current_job_.error_code = error;
        if (detail) {
            std::strncpy(current_job_.error_detail, detail, sizeof(current_job_.error_detail) - 1);
            current_job_.error_detail[sizeof(current_job_.error_detail) - 1] = '\0';
        }
    }
}

void JobManager::complete_job(uint32_t job_id) {
    update_progress(job_id, JobState::Completed, 100, 0, nullptr);
}

void JobManager::fail_job(uint32_t job_id, int32_t error, const char* detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_job_.job_id == job_id) {
        current_job_.state = JobState::Failed;
        current_job_.updated_ms = current_time_ms();
        current_job_.error_code = error;
        if (detail) {
            std::strncpy(current_job_.error_detail, detail, sizeof(current_job_.error_detail) - 1);
            current_job_.error_detail[sizeof(current_job_.error_detail) - 1] = '\0';
        }
    }
}

void JobManager::cancel_job(uint32_t job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_job_.job_id == job_id) {
        current_job_.state = JobState::Cancelled;
        current_job_.updated_ms = current_time_ms();
    }
}

bool JobManager::get_job(uint32_t job_id, OperationJob& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_job_.job_id == job_id) {
        out = current_job_;
        return true;
    }
    size_t hist_slot = job_id % kHistoryCapacity;
    if (history_[hist_slot].job_id == job_id) {
        out = history_[hist_slot];
        return true;
    }
    return false;
}

bool JobManager::get_current(OperationJob& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_job_.job_id != 0) {
        out = current_job_;
        return true;
    }
    return false;
}

} // namespace lg
