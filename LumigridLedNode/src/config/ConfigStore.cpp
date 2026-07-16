#include "config/ConfigStore.h"

namespace config {

void ConfigStore::load() {}
const NodeConfig& ConfigStore::get() const {
  static NodeConfig cfg;
  return cfg;
}

}
