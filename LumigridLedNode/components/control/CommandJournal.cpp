#include "CommandReceipt.hpp"
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

uint32_t CommandJournal::accept(CommandDomain domain, uint32_t generation) {
    uint32_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    if (id == 0) {
        id = next_id_.fetch_add(1, std::memory_order_relaxed);
    }

    const uint32_t timestamp = current_time_ms();
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    CommandReceipt& entry = entries_[slot];
    entry = {};
    entry.id = id;
    entry.domain = domain;
    entry.state = CommandState::Accepted;
    entry.accepted_ms = timestamp;
    entry.applied_ms = 0;
    entry.error_code = 0;
    entry.generation = generation;

    return id;
}

void CommandJournal::applying(uint32_t id) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Applying;
    }
}

void CommandJournal::applied(uint32_t id) {
    if (id == 0) return;
    const uint32_t timestamp = current_time_ms();
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Applied;
        entries_[slot].applied_ms = timestamp;
    }
}

void CommandJournal::failed(uint32_t id, int32_t error) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Failed;
        entries_[slot].error_code = error;
    }
}

void CommandJournal::superseded(uint32_t id) {
    if (id == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Superseded;
    }
}

bool CommandJournal::get(uint32_t id, CommandReceipt& out) const {
    if (id == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        out = entries_[slot];
        return true;
    }
    return false;
}

bool CommandJournal::get_latest(CommandDomain domain, CommandReceipt& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t max_id = 0;
    int best_slot = -1;
    for (size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i].id != 0 && entries_[i].domain == domain) {
            if (best_slot < 0 || newer(entries_[i].id, max_id)) {
                max_id = entries_[i].id;
                best_slot = static_cast<int>(i);
            }
        }
    }
    if (best_slot >= 0) {
        out = entries_[best_slot];
        return true;
    }
    return false;
}

} // namespace lg
