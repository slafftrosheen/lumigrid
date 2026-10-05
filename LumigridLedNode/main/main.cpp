#include "LumigridRuntime.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main(void) {
  // LG-038.1: Boot memory telemetry baseline
  ESP_LOGI("lumigrid",
           "BOOT_HEAP: free8=%lu largest8=%lu internal=%lu largestInternal=%lu "
           "freeHeap=%lu minimum=%lu",
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)esp_get_minimum_free_heap_size());

  static lg::LumigridRuntime runtime;
  esp_err_t boot_err = runtime.init();
  if (boot_err == ESP_OK) boot_err = runtime.start();
  if (boot_err != ESP_OK) {
    ESP_LOGE("lumigrid", "BOOT_FAILED error=%s state=%s",
             esp_err_to_name(boot_err), runtime.startup_state_str());
  }
  ESP_LOGI("lumigrid", "BOOT_STACK minimumFree=%lu",
           (unsigned long)uxTaskGetStackHighWaterMark(nullptr));

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI("lumigrid", "Heartbeat: state=%s, free_heap=%lu",
             runtime.startup_state_str(),
             (unsigned long)esp_get_free_heap_size());
  }
}
