#pragma once
#include <config/lg_config.hpp>
#include <stdint.h>

namespace output {

class PixelBus {
 public:
  bool probe();
  void init(const lg::NodeConfig& cfg);
  void show();
  void blackout();
};

}
