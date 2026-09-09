// components/signage/ControlSourceManager.cpp
#include "ControlSourceManager.hpp"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "ControlSrc";

namespace lg {

ControlSourceManager::ControlSourceManager() = default;

ControlSourceManager::~ControlSourceManager() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t ControlSourceManager::init() {
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_) return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < 8; ++i) {
        states_[i] = ControlState{};
        states_[i].source = static_cast<ControlSource>(i);
        states_[i].active = false;
    }
    current_owner_ = ControlSource::Idle;
    return ESP_OK;
}

void ControlSourceManager::evaluate_priority(uint32_t current_ms) {
    ControlSource highest = ControlSource::Idle;
    
    for (int i = 7; i >= 0; --i) {
        if (states_[i].active) {
            if (states_[i].expires_ms != 0 && current_ms >= states_[i].expires_ms) {
                states_[i].active = false; // Lease expired
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

bool ControlSourceManager::acquire(ControlSource source, uint32_t lease_ms,
                                    uint8_t zone_mask, uint8_t layer_mask,
                                    bool takeover) {
    if (source == ControlSource::Idle) return true;
    if (!mutex_) return false;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    
    uint8_t index = static_cast<uint8_t>(source);
    states_[index].source = source;
    states_[index].active = true;
    states_[index].timestamp_ms = now;
    states_[index].expires_ms = (lease_ms == 0) ? 0 : (now + lease_ms);
    states_[index].zone_mask = zone_mask;
    states_[index].layer_mask = layer_mask;
    states_[index].takeover = takeover;
    
    evaluate_priority(now);
    bool won = (current_owner_ == source);
    xSemaphoreGive(mutex_);
    
    return won;
}

void ControlSourceManager::release(ControlSource source) {
    if (source == ControlSource::Idle) return;
    if (!mutex_) return;
    
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    
    uint8_t index = static_cast<uint8_t>(source);
    states_[index].active = false;
    
    evaluate_priority(now);
    xSemaphoreGive(mutex_);
}

ControlSource ControlSourceManager::resolve(uint8_t zone_idx, uint8_t layer_idx, uint32_t current_ms) const {
    const uint8_t z_bit = (zone_idx < 8) ? (1 << zone_idx) : 0;
    const uint8_t l_bit = (layer_idx < 3) ? (1 << layer_idx) : 0;

    for (int i = 7; i >= 0; --i) {
        if (states_[i].active) {
            if (states_[i].expires_ms != 0 && current_ms >= states_[i].expires_ms) {
                continue; // expired
            }
            if ((states_[i].zone_mask & z_bit) && (states_[i].layer_mask & l_bit)) {
                return static_cast<ControlSource>(i);
            }
        }
    }
    return ControlSource::Idle;
}

} // namespace lg
