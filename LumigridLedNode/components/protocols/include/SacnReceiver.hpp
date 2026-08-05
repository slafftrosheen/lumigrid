#pragma once
#include <cstdint>
#include <functional>

#include "esp_err.h"

namespace lg {

struct SacnDmxPacket {
    uint16_t universe = 0;
    uint16_t length = 0;
    uint8_t sequence = 0;
    uint8_t priority = 0;
    uint8_t data[512] = {};
};

using SacnDmxCallback = std::function<void(const SacnDmxPacket&)>;

class SacnReceiver {
public:
    esp_err_t start(SacnDmxCallback callback);

private:
    static void task_entry(void* arg);
    void task();
    bool handle_packet(const uint8_t* packet, size_t length);

    SacnDmxCallback callback_{};
};

} // namespace lg
