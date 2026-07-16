#include "diagnostics/OutputHealth.h"

namespace lg {

void OutputHealth::mark_frame_committed(uint32_t now_ms) {
  last_frame_ms_ = now_ms;
}

bool OutputHealth::frame_deadline_missed(uint32_t now_ms) const {
  return outputs_enabled_ && (now_ms - last_frame_ms_ > 250);
}

void OutputHealth::set_outputs_enabled(bool enabled) {
  outputs_enabled_ = enabled;
}

}
