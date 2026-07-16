#pragma once
#include "diagnostics/FaultModel.h"
namespace lg {

class OutputHealth;

namespace Metrics {

void init(OutputHealth* health = nullptr);
void tick();
void recordPcaFault();
void recordPixelFault();
void incrementDropped();
void markLastSequence(uint32_t sequence);

}

struct MetricsSnapshot {
  uint32_t uptime_ms;
  uint32_t dropped_frames;
  uint32_t last_sequence;
  uint32_t last_frame_ms;
  bool pca_faulted;
  bool pixel_faulted;
};

MetricsSnapshot metrics_snapshot();
OutputHealth* output_health();
Fault get_latched_fault();
void clear_fault();

}
