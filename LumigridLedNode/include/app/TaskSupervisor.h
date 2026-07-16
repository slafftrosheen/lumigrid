#pragma once
#include <stdint.h>
#include "protocols/protocol_router.hpp"
#include "config/lg_config.hpp"

namespace app {
namespace output { class PixelBus; class PwmBus; }
namespace render { class Renderer; }

class TaskSupervisor {
 public:
  void spawnOutput(output::PixelBus&, output::PwmBus&, render::Renderer&);
  void start(lg::ProtocolRouter& router);
  void loop();
};
}
