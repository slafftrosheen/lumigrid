#pragma once
#include <stdint.h>

namespace config {

constexpr uint16_t kSchemaVersion = 1;
constexpr uint8_t kNodeIdSize = 24;

struct NodeConfig {
  uint16_t schemaVersion{kSchemaVersion};
  char nodeId[kNodeIdSize]{"lg-node"};
  char group[kNodeIdSize]{"default"};
  uint16_t frameRate{60};
  uint16_t pwmFrequency{1000};
  uint16_t endpoints[16]{};
};

class ConfigStore {
 public:
  void load();
  const NodeConfig& get() const;
};

}
