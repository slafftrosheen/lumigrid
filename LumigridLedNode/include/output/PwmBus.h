#pragma once
#include <config/lg_config.hpp>
#include <stdint.h>
#include <stddef.h>

namespace output {

class PwmBus {
 public:
  bool probe();
  void init(const lg::NodeConfig& cfg);
  void blackout();
  void showDirty();
};

}
