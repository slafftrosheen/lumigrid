#include "output/PixelBus.h"

namespace output {

bool PixelBus::probe() { return true; }
void PixelBus::init(const lg::NodeConfig&) {}
void PixelBus::show() {}
void PixelBus::blackout() {}

}
