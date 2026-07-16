#pragma once
#include <cstdint>

namespace lg {

class OutputHealth {
 public:
  void mark_frame_committed(uint32_t now_ms);
  bool frame_deadline_missed(uint32_t now_ms) const;
  void set_outputs_enabled(bool enabled);
  bool outputs_enabled() const { return outputs_enabled_; }

 private:
  uint32_t last_frame_ms_ = 0;
  bool outputs_enabled_ = false;
};

}
