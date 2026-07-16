#pragma once

namespace network {

class ApiServer {
 public:
  void begin(const void*, const void*, const void*, const void*, const void*);
  void loop();
};

extern ApiServer api;

}
