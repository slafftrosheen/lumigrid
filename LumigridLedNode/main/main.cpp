#include "LumigridRuntime.hpp"
#include "RuntimeMemory.hpp"
#include "BoardOutputSafety.hpp"
#include "ResetForensics.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "RuntimeHardeningTests.hpp"
#include "Pca9685Driver.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* reset_reason_to_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_PIN";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && \
    CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
static void runtime_hardening_test_task(void*) {
  ESP_LOGW("lumigrid", "Dedicated runtime hardening test task started");
  const lg::RuntimeHardeningResult result = lg::run_runtime_hardening_tests();
  const char* result_name =
      result == lg::RuntimeHardeningResult::Passed ? "PASS" :
      result == lg::RuntimeHardeningResult::NotRun ? "NOT_RUN" : "FAIL";
  ESP_LOGI("lumigrid", "RUNTIME_HARDENING_RESULT=%s",
           result_name);
  vTaskDelete(nullptr);
}
#endif

extern "C" void app_main(void) {
  // LG-HW-004 §3/§4: Board-level early electrical output safety bootstrap
  lg::board::force_outputs_safe_early();
  // LG-HW-004 §10/§16: Initialize reset forensics and print concise serial banner
  lg::board::init_reset_forensics();

  const esp_reset_reason_t rst = esp_reset_reason();
  ESP_LOGI("lumigrid", "RESET reason=%d name=%s", (int)rst, reset_reason_to_name(rst));

  // LG-038.1/038.3: boot memory telemetry. The 8-bit allocatable heap is the
  // product metric; internal/DMA figures are diagnostics only (§59).
  const lg::RuntimeMemorySnapshot boot_mem = lg::capture_runtime_memory();
  ESP_LOGI("lumigrid",
           "BOOT_HEAP: free8=%lu largest8=%lu internal=%lu largestInternal=%lu "
           "freeHeap=%lu minimum=%lu",
           (unsigned long)boot_mem.free_8bit,
           (unsigned long)boot_mem.largest_8bit,
           (unsigned long)boot_mem.free_internal,
           (unsigned long)boot_mem.largest_internal,
           (unsigned long)boot_mem.minimum_free_heap,
           (unsigned long)esp_get_minimum_free_heap_size());

#if defined(CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS) && \
    CONFIG_LUMIGRID_ENABLE_RUNTIME_TESTS
  // The diagnostic image never initializes the product runtime, opens the
  // provisioned config/auth namespaces, or initializes output drivers.
  // RuntimeHardeningTests owns its isolated fixtures and test-only namespaces.
  lg::Pca9685Driver::enable_test_output_mock();
  constexpr uint32_t kHardeningTestTaskStackBytes = 16 * 1024;
  if (xTaskCreate(runtime_hardening_test_task, "runtime_tests",
                  kHardeningTestTaskStackBytes, nullptr, 1, nullptr) != pdPASS) {
    ESP_LOGE("lumigrid", "RUNTIME_HARDENING_RESULT=NOT_RUN error=no-memory");
    return;
  }
  ESP_LOGI("lumigrid", "Diagnostic test task started; app_main returns");
  return;
#else
  static lg::LumigridRuntime runtime;

  esp_err_t boot_err = runtime.init();
  ESP_LOGI("lumigrid",
           "BOOT_DIAG: reason=%d name=%s bootCount=%d free8=%lu largest8=%lu minFree=%lu",
           (int)rst, reset_reason_to_name(rst), runtime.boot_count(),
           (unsigned long)boot_mem.free_8bit,
           (unsigned long)boot_mem.largest_8bit,
           (unsigned long)boot_mem.minimum_free_heap);
  if (boot_err == ESP_OK) {
    boot_err = runtime.start();
  }

  if (boot_err != ESP_OK) {
    ESP_LOGE("lumigrid", "BOOT_FAILED error=%s state=%s stage=%s failing=%s",
             esp_err_to_name(boot_err),
             runtime.startup_state_str(),
             runtime.startup_stage_str(),
             runtime.failing_stage_str());
    // Remain available only for fatal recovery handling: the recovery task
    // (GPIO0 factory reset) and Safe-Mode API keep running independently.
    // LG-038.3 §5: return even on failure so the main task's 10 KB stack is
    // not pinned forever by a dead boot path.
    return;
  }

  // LG-038.3 §5/§77: report startup truth once, then RETURN. ESP-IDF deletes
  // the main task when app_main() returns, reclaiming its 10240-byte stack.
  // The static runtime object stays alive; no heartbeat task replaces the
  // loop (§6): state is observable via /health, RuntimeSnapshot and
  // /api/v1/diagnostics/memory.
  const lg::RuntimeMemorySnapshot ready_mem = lg::capture_runtime_memory();
  ESP_LOGI("lumigrid",
           "BOOT_STACK minimumFree=%lu bytes; READY free8=%lu largest8=%lu state=%s",
           (unsigned long)uxTaskGetStackHighWaterMark(nullptr) * (unsigned long)sizeof(StackType_t),
           (unsigned long)ready_mem.free_8bit,
           (unsigned long)ready_mem.largest_8bit,
           runtime.startup_state_str());
  ESP_LOGI("lumigrid", "Startup complete: main task exiting (stack reclaimed)");
#endif
}
