#include "app/TaskSupervisor.h"

namespace app {

void TaskSupervisor::spawnOutput(output::PixelBus&, output::PwmBus&, render::Renderer&) {}
void TaskSupervisor::start(lg::ProtocolRouter&) {}
void TaskSupervisor::loop() {}

} // namespace app

