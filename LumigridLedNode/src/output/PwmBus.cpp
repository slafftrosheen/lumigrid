#include "output/PwmBus.h"
#include "esp_com.h"

namespace output {

bool PwmBus::probe() { return true; }
void PwmBus::init(const lg::NodeConfig&) {}
void PwmBus::blackout() {}
void PwmBus::showDirty() {}

}
