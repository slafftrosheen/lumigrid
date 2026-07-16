#include "network/ApiServer.h"
#include "api/api_server.hpp"

namespace network {

void ApiServer::begin(const void* a, const void* b, const void* c, const void* d, const void* e) {
    // delegate into new api skeleton; actual binding happens in App.cpp.
}
void ApiServer::loop() {
    api.loop();
}

ApiServer api;

}
