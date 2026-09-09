// components/signage/ControlSourceManager.cpp
#include "ControlSourceManager.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "ControlSrc";

namespace lg {

ControlSourceManager::ControlSourceManager(OutputController& controller) 
    : controller_(controller) {
}

esp_err_t ControlSourceManager::init() {
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) return ESP_ERR_NO_MEM;
    
    for (int i = 0; i < 8; ++i) {
        leases_[i].active = false;
    }
    return ESP_OK;
}

void ControlSourceManager::evaluate_priority(uint32_t current_ms) {
    ControlSource highest = ControlSource::Idle;
    
    for (int i = 7; i >= 0; --i) {
        if (leases_[i].active) {
            if (leases_[i].expires_at_ms != 0 && current_ms >= leases_[i].expires_at_ms) {
                leases_[i].active = false; // Lease expired
            } else {
                highest = static_cast<ControlSource>(i);
                break;
            }
        }
    }
    
    if (highest != current_owner_) {
        ESP_LOGI(TAG, "Control transitioned from %d to %d", 
                 static_cast<int>(current_owner_), static_cast<int>(highest));
        current_owner_ = highest;
    }
}

bool ControlSourceManager::acquire(ControlSource source, uint32_t lease_ms) {
    if (source == ControlSource::Idle) return true;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = esp_timer_get_time() / 1000;
    
    uint8_t index = static_cast<uint8_t>(source);
    leases_[index].active = true;
    leases_[index].expires_at_ms = (lease_ms == 0) ? 0 : (now + lease_ms);
    
    evaluate_priority(now);
    bool won = (current_owner_ == source);
    xSemaphoreGive(mutex_);
    
    return won;
}

void ControlSourceManager::release(ControlSource source) {
    if (source == ControlSource::Idle) return;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = esp_timer_get_time() / 1000;
    
    uint8_t index = static_cast<uint8_t>(source);
    leases_[index].active = false;
    
    evaluate_priority(now);
    xSemaphoreGive(mutex_);
}

bool ControlSourceManager::begin_frame(ControlSource source) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = esp_timer_get_time() / 1000;
    
    evaluate_priority(now);
    
    if (current_owner_ != source) {
        xSemaphoreGive(mutex_);
        return false;
    }
    
    if (frame_active_) {
        // Should not happen unless caller forgot to commit/abort
        ESP_LOGW(TAG, "begin_frame called while frame already active by %d", static_cast<int>(frame_lock_));
        xSemaphoreGive(mutex_);
        return false;
    }
    
    frame_active_ = true;
    frame_lock_ = source;
    xSemaphoreGive(mutex_);
    return true;
}

esp_err_t ControlSourceManager::commit_frame(ControlSource source, bool enable_output) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!frame_active_ || frame_lock_ != source) {
        xSemaphoreGive(mutex_);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err;
    if (enable_output) {
        err = controller_.commit();
    } else {
        err = controller_.blackout();
    }
    
    frame_active_ = false;
    xSemaphoreGive(mutex_);
    return err;
}

void ControlSourceManager::abort_frame(ControlSource source) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (frame_active_ && frame_lock_ == source) {
        frame_active_ = false;
    }
    xSemaphoreGive(mutex_);
}

} // namespace lg
