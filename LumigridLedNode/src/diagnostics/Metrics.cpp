#include "diagnostics/Metrics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "diagnostics/FaultModel.h"
#include "diagnostics/OutputHealth.h"
#include "board/BoardSafety.h"

namespace lg {

namespace {

struct MetricsState {
  uint32_t uptime_ms = 0;
  uint32_t dropped_frames = 0;
  uint32_t last_sequence = 0;
  uint32_t last_frame_ms = 0;
  uint32_t pca_faults = 0;
  uint32_t pixel_faults = 0;
  OutputHealth* health = nullptr;
  Fault latched_fault;
};

static MetricsState s_state;

}

void Metrics::init(OutputHealth* health) {
  s_state.health = health;
  tick();
}

void Metrics::tick() {
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  s_state.uptime_ms += (now - s_state.uptime_ms); // Simple update since we just want it roughly correct

  if (s_state.health && s_state.health->frame_deadline_missed(now)) {
    if (s_state.latched_fault.code != FaultCode::OutputDeadlineMissed) {
      s_state.latched_fault.code = FaultCode::OutputDeadlineMissed;
      s_state.latched_fault.occurred_at_ms = now;
      s_state.latched_fault.count++;
      s_state.latched_fault.latched = true;

      board::safeOff();
      s_state.health->set_outputs_enabled(false);
    }
  }
}

void Metrics::recordPcaFault() {
  s_state.pca_faults++;
  s_state.latched_fault.code = FaultCode::Pca9685NotDetected;
  s_state.latched_fault.occurred_at_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  s_state.latched_fault.latched = true;
}

void Metrics::recordPixelFault() {
  s_state.pixel_faults++;
  s_state.latched_fault.code = FaultCode::PixelTransportError;
  s_state.latched_fault.occurred_at_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  s_state.latched_fault.latched = true;
}
void Metrics::incrementDropped() { s_state.dropped_frames++; }
void Metrics::markLastSequence(uint32_t sequence) { s_state.last_sequence = sequence; }

OutputHealth* output_health() { return s_state.health; }

MetricsSnapshot metrics_snapshot() {
  MetricsSnapshot snapshot = {};
  snapshot.uptime_ms = s_state.uptime_ms;
  snapshot.dropped_frames = s_state.dropped_frames;
  snapshot.last_sequence = s_state.last_sequence;
  snapshot.last_frame_ms = s_state.last_frame_ms;
  snapshot.pca_faulted = s_state.pca_faults > 0;
  snapshot.pixel_faulted = s_state.pixel_faults > 0;
  return snapshot;
}

Fault get_latched_fault() {
  return s_state.latched_fault;
}

void clear_fault() {
  s_state.latched_fault = Fault{};
}

}
