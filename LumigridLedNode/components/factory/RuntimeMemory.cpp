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
    size_t sampled = 0;
    for (size_t i = 0; i < kMaxTrackedTasks; ++i) {
        TaskStackInfo& slot = tasks_[i];
        if (!slot.registered || slot.name == nullptr) continue;
        TaskHandle_t handle = xTaskGetHandle(slot.name);
        if (handle == nullptr) continue;
        // ESP-IDF 5.2 / FreeRTOS 10.4: uxTaskGetStackHighWaterMark returns the
        // minimum free stack in StackType_t words. StackType_t is uint32_t on
        // the pinned Xtensa port, so multiply by sizeof(StackType_t) (4) to
        // report bytes.
        const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(handle);
        const uint32_t free_bytes =
            static_cast<uint32_t>(hwm_words) * static_cast<uint32_t>(sizeof(StackType_t));
        if (slot.minimum_free_bytes == 0 || free_bytes < slot.minimum_free_bytes) {
            slot.minimum_free_bytes = free_bytes;
        }
        ++sampled;
    }
    return sampled;
}

} // namespace lg
