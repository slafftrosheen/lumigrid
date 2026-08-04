#include "OutputArbitrator.hpp"

namespace lg {

OutputArbitrator::OutputArbitrator(OutputController& controller)
    : controller_(controller) {}

esp_err_t OutputArbitrator::init() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void OutputArbitrator::evaluate_priority(uint32_t current_ms) {
    OutputPriority highest = OutputPriority::Background;
    
    for (int i = 4; i >= 0; --i) {
        if (leases_[i].active) {
            if (leases_[i].expires_at_ms != 0 && current_ms >= leases_[i].expires_at_ms) {
                leases_[i].active = false;
            } else {
                highest = static_cast<OutputPriority>(i);
                break;
            }
        }
    }
    current_owner_ = highest;
}

bool OutputArbitrator::acquire(OutputPriority priority, uint32_t lease_ms) {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const int idx = static_cast<int>(priority);
    
    leases_[idx].active = true;
    leases_[idx].expires_at_ms = lease_ms == 0 ? 0 : now + lease_ms;
    
    evaluate_priority(now);
    
    const bool granted = (current_owner_ == priority);
    xSemaphoreGive(mutex_);
    
    return granted;
}

void OutputArbitrator::release(OutputPriority priority) {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    const int idx = static_cast<int>(priority);
    leases_[idx].active = false;
    
    evaluate_priority(static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
    
    xSemaphoreGive(mutex_);
}

bool OutputArbitrator::begin_frame(OutputPriority priority) {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    evaluate_priority(now);
    
    if (current_owner_ != priority) {
        xSemaphoreGive(mutex_);
        return false;
    }
    
    if (frame_active_) {
        // Someone else is holding the lock (should not happen if priorities are strictly respected, 
        // but could happen if abort_frame/commit_frame wasn't called).
        xSemaphoreGive(mutex_);
        return false;
    }
    
    frame_active_ = true;
    frame_lock_ = priority;
    
    // Acquire output controller lock
    controller_.lock();
    
    xSemaphoreGive(mutex_);
    return true;
}

esp_err_t OutputArbitrator::commit_frame(OutputPriority priority, bool enable_output) {
    if (mutex_ == nullptr) return ESP_FAIL;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    if (!frame_active_ || frame_lock_ != priority) {
        xSemaphoreGive(mutex_);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (enable_output) {
        controller_.enable();
    }
    
    esp_err_t err = controller_.commit();
    controller_.unlock();
    
    frame_active_ = false;
    xSemaphoreGive(mutex_);
    return err;
}

void OutputArbitrator::abort_frame(OutputPriority priority) {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    
    if (!frame_active_ || frame_lock_ != priority) {
        xSemaphoreGive(mutex_);
        return;
    }
    
    controller_.unlock();
    frame_active_ = false;
    xSemaphoreGive(mutex_);
}

} // namespace lg
