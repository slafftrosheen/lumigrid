#pragma once
#include <frame/lg_frame.hpp>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

#include "output/lg_pixel_mapper.hpp"
#include "output/OutputManager.hpp"

namespace lg {

class RenderPipeline {
 public:
  RenderPipeline(const NodeConfig& config, OutputManager* output);
  esp_err_t start();
  void submit_latest(const LogicalFrame& frame);
  bool is_started() const { return started_; }

 private:
  static void output_task_entry(void* argument);
  void output_task();

  bool started_ = false;
  QueueHandle_t ready_frames_ = nullptr;
  PixelMapper mapper_;
  OutputManager* output_ = nullptr;
};

}  // namespace lg
