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

    size_t slot = id % kCapacity;
    entries_[slot].id = id;
    entries_[slot].domain = domain;
    entries_[slot].state = CommandState::Accepted;
    entries_[slot].accepted_ms = current_time_ms();
    entries_[slot].applied_ms = 0;
    entries_[slot].error_code = 0;
    entries_[slot].generation = generation;

    return id;
}

void CommandJournal::applying(uint32_t id) {
    if (id == 0) return;
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Applying;
    }
}

void CommandJournal::applied(uint32_t id) {
    if (id == 0) return;
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Applied;
        entries_[slot].applied_ms = current_time_ms();
    }
}

void CommandJournal::failed(uint32_t id, int32_t error) {
    if (id == 0) return;
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Failed;
        entries_[slot].error_code = error;
    }
}

void CommandJournal::superseded(uint32_t id) {
    if (id == 0) return;
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        entries_[slot].state = CommandState::Superseded;
    }
}

bool CommandJournal::get(uint32_t id, CommandReceipt& out) const {
    if (id == 0) return false;
    size_t slot = id % kCapacity;
    if (entries_[slot].id == id) {
        out = entries_[slot];
        return true;
    }
    return false;
}

bool CommandJournal::get_latest(CommandDomain domain, CommandReceipt& out) const {
    uint32_t max_id = 0;
    int best_slot = -1;
    for (size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i].id != 0 && entries_[i].domain == domain && entries_[i].id > max_id) {
            max_id = entries_[i].id;
            best_slot = static_cast<int>(i);
        }
    }
    if (best_slot >= 0) {
        out = entries_[best_slot];
        return true;
    }
    return false;
}

} // namespace lg
