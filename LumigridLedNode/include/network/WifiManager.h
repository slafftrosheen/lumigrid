#pragma once
#include <stdint.h>

namespace network {
class WifiManager {
 public:
  void begin(const char* nodeId);
  void loop();
};
class ApiServer;
class WsHub;
}
