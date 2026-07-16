#pragma once
#include "frame/lg_frame.hpp"

namespace lg {

class OutputManager {
 public:
  virtual ~OutputManager() = default;
  virtual void commit(const PhysicalFrame& frame) = 0;
  virtual void safe_off() = 0;
};

}  // namespace lg
