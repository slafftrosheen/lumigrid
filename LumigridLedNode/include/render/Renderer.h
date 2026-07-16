#pragma once
#include <config/lg_config.hpp>
#include "protocols/protocol_router.hpp"

namespace render {
class Renderer {
 public:
  void init(const config::NodeConfig& cfg);
  void loop();
};
}
