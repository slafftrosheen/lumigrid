// components/factory/include/RuntimeMemory.hpp
//
// LG-038.3: Canonical runtime memory truth and admission control.
//
// Rules encoded here:
//   - The primary product metric is MALLOC_CAP_8BIT heap. MALLOC_CAP_INTERNAL
//     is exposed for diagnostics only; it is NOT general usable heap and must
//     never be surfaced as "free RAM" in the UI (LG-038.3 §59).
//   - Addressable topology and heavy operations must preserve a post-operation
//     reserve (LG-038.3 §39, §43-44). Admission failure is reported explicitly,
//     never as a bare ESP_ERR_NO_MEM (LG-038.3 §41).
//   - Per-task stack high-water telemetry is sampled on demand (diagnostics
//     request) or at most once per 30-60 s. No monitor task (LG-038.3 §30).
#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <array>
#include <cstring>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lg {

// ── Runtime memory snapshot ───────────────────────────────────────────────────

struct RuntimeMemorySnapshot {
    uint32_t free_8bit = 0;
    uint32_t largest_8bit = 0;

    uint32_t free_internal = 0;
    uint32_t largest_internal = 0;

    uint32_t minimum_free_heap = 0;

    uint32_t free_dma = 0;
    uint32_t largest_dma = 0;
};

inline RuntimeMemorySnapshot capture_runtime_memory() {
    RuntimeMemorySnapshot snap{};
    snap.free_8bit        = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    snap.largest_8bit     = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    snap.free_internal    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snap.largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snap.minimum_free_heap = esp_get_minimum_free_heap_size();
    snap.free_dma         = heap_caps_get_free_size(MALLOC_CAP_DMA);
    snap.largest_dma      = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    return snap;
}

// Largest block / free heap ratio. Diagnostics-only fragmentation metric
// (LG-038.3 §46). Returns 0.0 when there is no free heap.
inline float fragmentation_ratio(const RuntimeMemorySnapshot& snap) {
    if (snap.free_8bit == 0) return 0.0f;
    return static_cast<float>(snap.largest_8bit) / static_cast<float>(snap.free_8bit);
}

// ── Product headroom gates (engineering budgets, not IDF minima) ─────────────
// LG-038.3 §1: raise these if measurements demand it; never lower them to make
// a gate pass.
constexpr uint32_t kFirstBootFree8Target       = 32u * 1024u;
constexpr uint32_t kFirstBootLargest8Target    = 16u * 1024u;
constexpr uint32_t kNormalReadyFree8Target     = 24u * 1024u;
constexpr uint32_t kNormalReadyLargest8Target  = 12u * 1024u;
constexpr uint32_t kMaxTopologyFree8Target     = 16u * 1024u;
constexpr uint32_t kMaxTopologyLargest8Target  = 8u * 1024u;

// Post-allocation reserve enforced before accepting a new addressable topology
// or starting a heavy operation (LG-038.3 §39, §43).
constexpr uint32_t kPostTopologyHeapReserveBytes = 16u * 1024u;

// ── Allocation-failure counters (LG-038.3 §57) ───────────────────────────────

struct RuntimeMemoryCounters {
    std::atomic<uint32_t> api_allocation_failures{0};
    std::atomic<uint32_t> preview_allocation_failures{0};
    std::atomic<uint32_t> rmt_allocation_failures{0};
    std::atomic<uint32_t> factory_task_start_failures{0};
    std::atomic<uint32_t> network_task_start_failures{0};
};

RuntimeMemoryCounters& runtime_memory_counters();

inline void note_api_allocation_failure()      { runtime_memory_counters().api_allocation_failures++; }
inline void note_preview_allocation_failure()  { runtime_memory_counters().preview_allocation_failures++; }
inline void note_rmt_allocation_failure()      { runtime_memory_counters().rmt_allocation_failures++; }
inline void note_factory_task_start_failure()  { runtime_memory_counters().factory_task_start_failures++; }
inline void note_network_task_start_failure()  { runtime_memory_counters().network_task_start_failures++; }

// ── Memory admission (LG-038.3 §39, §43-44) ──────────────────────────────────

struct MemoryAdmissionResult {
    bool accepted = false;
    // Bytes that would remain free if the requested allocation were granted.
    uint32_t projected_free_bytes = 0;
    uint32_t required_bytes = 0;
    uint32_t reserve_bytes = 0;
};

struct JsonScratchBudget {
    bool bounded = false;
    uint32_t nodes = 0;
    uint32_t scratch_bytes = 0;
};

// Pure lexical sizing pass. cJSON still owns syntax validation. Colons count
// object values; the first array element and each array comma count array values.
inline JsonScratchBudget json_scratch_budget(const char* body, size_t length, size_t node_bytes) {
    JsonScratchBudget budget{};
    if (!body || length > 32768 || node_bytes > 1024) return budget;
    char containers[32]{};
    size_t depth = 0, nodes = 1;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        const char ch = body[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') { quoted = true; continue; }
        if (ch == '{' || ch == '[') {
            if (depth == sizeof(containers)) return budget;
            containers[depth++] = ch;
            if (ch == '[') {
                size_t next = i + 1;
                while (next < length && (body[next] == ' ' || body[next] == '\r' || body[next] == '\n' || body[next] == '\t')) ++next;
                if (next < length && body[next] != ']') ++nodes;
            }
        } else if (ch == '}' || ch == ']') {
            if (!depth) return budget;
            --depth;
        } else if (ch == ':' || (ch == ',' && depth && containers[depth - 1] == '[')) ++nodes;
    }
    if (quoted || depth) return budget;
    budget.bounded = true;
    budget.nodes = nodes;
    budget.scratch_bytes = nodes * (node_bytes + 48u) + length + 128u;
    return budget;
}

inline MemoryAdmissionResult check_memory_admission_snapshot(
    const RuntimeMemorySnapshot& snap,
    uint32_t required_scratch_bytes,
    uint32_t post_reserve_bytes = kPostTopologyHeapReserveBytes,
    uint32_t largest_allocation_bytes = 0) {
    MemoryAdmissionResult res{};
    res.required_bytes = required_scratch_bytes;
    res.reserve_bytes = post_reserve_bytes;
    // Subtraction after the bounds check avoids overflow in scratch + reserve.
    // Scratch must fit contiguously; the reserve may span multiple heap blocks.
    if (required_scratch_bytes <= snap.free_8bit &&
        snap.free_8bit - required_scratch_bytes >= post_reserve_bytes &&
        snap.largest_8bit >= (largest_allocation_bytes ? largest_allocation_bytes : required_scratch_bytes)) {
        res.accepted = true;
        res.projected_free_bytes = snap.free_8bit - required_scratch_bytes;
    }
    return res;
}

inline MemoryAdmissionResult check_memory_admission(
    uint32_t required_scratch_bytes,
    uint32_t post_reserve_bytes = kPostTopologyHeapReserveBytes) {
    return check_memory_admission_snapshot(capture_runtime_memory(), required_scratch_bytes,
                                          post_reserve_bytes);
}

// ── Per-task stack high-water registry (LG-038.3 §29-30) ─────────────────────
//
// The pinned ESP-IDF 5.2.1 Xtensa port defines StackType_t as uint8_t:
// uxTaskGetStackHighWaterMark() therefore returns BYTES. Configured stacks are declared in bytes
// at xTaskCreate time.

struct TaskStackInfo {
    const char* name = nullptr;         // FreeRTOS task name
    uint32_t configured_bytes = 0;      // stack size passed to xTaskCreate
    uint32_t minimum_free_bytes = 0;    // sampled high-water mark (bytes)
    bool registered = false;            // task has been observed alive
    bool active = false;                // present in the latest kernel snapshot
    TaskHandle_t handle = nullptr;       // compared as an identity; never dereferenced
};

inline constexpr size_t kMaxTrackedTasks = 16;

class TaskStackRegistry {
    class Guard {
    public:
        explicit Guard(portMUX_TYPE& mux) : mux_(mux) { portENTER_CRITICAL(&mux_); }
        ~Guard() { portEXIT_CRITICAL(&mux_); }
    private:
        portMUX_TYPE& mux_;
    };
public:
    // Register a task by exact FreeRTOS name with its configured stack size.
    void register_task(const char* name, uint32_t configured_bytes) {
        const TaskHandle_t handle = xTaskGetHandle(name);
        Guard guard(mutex_);
        for (size_t i = 0; i < kMaxTrackedTasks; ++i) {
            TaskStackInfo& slot = tasks_[i];
            if (!slot.registered && slot.name == nullptr) {
                slot.name = name;
                slot.configured_bytes = configured_bytes;
                slot.minimum_free_bytes = 0;
                slot.registered = true;
                slot.handle = handle;
                return;
            }
            if (slot.registered && slot.name != nullptr &&
                std::strcmp(slot.name, name) == 0) {
                if (slot.handle != handle) slot.minimum_free_bytes = 0;
                slot.handle = handle;
                slot.configured_bytes = configured_bytes;
                return;
            }
        }
        // Registry full: telemetry is best-effort, never fatal.
    }

    // Unregister (e.g. one-shot worker exited).
    void unregister_task(const char* name) {
        Guard guard(mutex_);
        for (size_t i = 0; i < kMaxTrackedTasks; ++i) {
            TaskStackInfo& slot = tasks_[i];
            if (slot.registered && slot.name != nullptr &&
                std::strcmp(slot.name, name) == 0) {
                slot.registered = false;
                slot.name = nullptr;
                slot.configured_bytes = 0;
                slot.minimum_free_bytes = 0;
                slot.active = false;
                slot.handle = nullptr;
            }
        }
    }

    // Sample every registered, currently-live task via
    // uxTaskGetStackHighWaterMark (bytes on ESP-IDF 5.2). Keeps the observed
    // minimum. Returns the number of live tasks sampled.
    size_t sample_all();

    size_t copy(TaskStackInfo* out, size_t max) const {
        Guard guard(mutex_);
        size_t n = 0;
        for (size_t i = 0; i < kMaxTrackedTasks && n < max; ++i) {
            if (tasks_[i].registered && tasks_[i].active) {
                out[n++] = tasks_[i];
            }
        }
        return n;
    }

private:
    mutable portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
    mutable std::array<TaskStackInfo, kMaxTrackedTasks> tasks_{};
};

TaskStackRegistry& task_stack_registry();

// ── Health evaluation (LG-038.3 §45) ─────────────────────────────────────────

enum class MemoryHealth : uint8_t {
    Healthy = 0,
    LowRuntimeMemory = 1,   // free 8-bit below the Ready gate
    HeapFragmented = 2,     // largest block too small for headroom allocations
    Critical = 3,           // both
};

inline MemoryHealth evaluate_memory_health(const RuntimeMemorySnapshot& snap) {
    const bool low = snap.free_8bit < kNormalReadyFree8Target;
    const bool fragmented = snap.largest_8bit < kNormalReadyLargest8Target;
    if (low && fragmented) return MemoryHealth::Critical;
    if (low) return MemoryHealth::LowRuntimeMemory;
    if (fragmented) return MemoryHealth::HeapFragmented;
    return MemoryHealth::Healthy;
}

inline const char* memory_health_name(MemoryHealth h) {
    switch (h) {
        case MemoryHealth::LowRuntimeMemory: return "LOW_RUNTIME_MEMORY";
        case MemoryHealth::HeapFragmented:   return "HEAP_FRAGMENTED";
        case MemoryHealth::Critical:         return "CRITICAL";
        case MemoryHealth::Healthy:
        default:                             return "HEALTHY";
    }
}

} // namespace lg
