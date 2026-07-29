#include "output/lg_render_pipeline.hpp"
#include "output/lg_pixel_mapper.hpp"
#include "diagnostics/Metrics.h"

namespace lg {

RenderPipeline::RenderPipeline(const NodeConfig& config, OutputManager* output)
    : mapper_(config), output_(output) {}

esp_err_t RenderPipeline::start() {
  ready_frames_ = xQueueCreate(2, sizeof(LogicalFrame));
  if (ready_frames_ == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  started_ = true;
  return xTaskCreatePinnedToCore(
             output_task_entry, "lg_output", 8192, this, 8, nullptr, 1) == pdPASS
             ? ESP_OK
             : ESP_FAIL;
}

void RenderPipeline::submit_latest(const LogicalFrame& frame) {
  if (xQueueSend(ready_frames_, &frame, 0) == pdPASS) {
    return;
  }
  LogicalFrame discarded;
  xQueueReceive(ready_frames_, &discarded, 0);
  xQueueSend(ready_frames_, &frame, 0);
  Metrics::incrementDropped();
}

void RenderPipeline::output_task_entry(void* argument) {
  static_cast<RenderPipeline*>(argument)->output_task();
  vTaskDelete(nullptr);
}

void RenderPipeline::output_task() {
  LogicalFrame logical;
  PhysicalFrame physical;
  esp_task_wdt_add(nullptr);

  for (;;) {
    if (xQueueReceive(ready_frames_, &logical, portMAX_DELAY) == pdPASS) {
      mapper_.map(logical, physical);
      if (output_) {
        output_->commit(physical);
      }
      Metrics::markLastSequence(logical.sequence);
      
      if (auto* health = lg::output_health()) {
        health->mark_frame_committed(logical.timestamp_ms);
      }
    }
    esp_task_wdt_reset();
  }
}

}  // namespace lg
