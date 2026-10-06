// components/factory/RuntimeMemory.cpp
//
// LG-038.3: state for the canonical memory helpers declared in RuntimeMemory.hpp.
#include "RuntimeMemory.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lg {

RuntimeMemoryCounters& runtime_memory_counters() {
    static RuntimeMemoryCounters counters{};
    return counters;
}

TaskStackRegistry& task_stack_registry() {
    static TaskStackRegistry registry;
    return registry;
}

size_t TaskStackRegistry::sample_all() {
    // IDF takes the SMP kernel lock while gathering status and watermarks.
    // Looking up a task handle then reading it separately races task deletion.
    static_assert(sizeof(StackType_t) == 1, "Verify stack telemetry units for this port");
    TaskStatus_t live[32]{};
    const UBaseType_t count = uxTaskGetSystemState(live, 32, nullptr);
    Guard guard(mutex_);
    size_t sampled = 0;
    for (auto& slot : tasks_) {
        slot.active = false;
        if (!slot.registered || !slot.name) continue;
        for (UBaseType_t i = 0; i < count; ++i) {
            if (live[i].eCurrentState == eDeleted || !slot.handle || slot.handle != live[i].xHandle) continue;
            slot.active = true;
            const uint32_t free_bytes = live[i].usStackHighWaterMark;
            if (!slot.minimum_free_bytes || free_bytes < slot.minimum_free_bytes) slot.minimum_free_bytes = free_bytes;
            ++sampled;
            break;
        }
    }
    return sampled;
}

} // namespace lg
