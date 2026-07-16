#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "lg_config.hpp"

namespace lg {

constexpr uint16_t kArtnetPort = 6454;
constexpr uint16_t kOpDmx = 0x5000;
constexpr uint16_t kOpPoll = 0x2000;
constexpr uint16_t kOpSync = 0x5200;

struct DmxPacket {
    uint16_t universe;
    uint8_t sequence;
    const uint8_t* data;
    size_t length;
};

using DmxCallback = std::function<void(const DmxPacket&)>;

class ArtnetReceiver {
public:
    ArtnetReceiver();
    ~ArtnetReceiver();

    bool start(DmxCallback callback);
    void stop();
    void set_universe_range(uint16_t first, uint16_t count);
    void set_enabled(bool enabled);
    void loop(); /* idle hook; real work is in receiver task */

private:
    bool parse_packet(const uint8_t* packet, size_t length);
    void task_loop();

    void* task_handle_;
    int sock_;
    DmxCallback on_dmx_;
    uint16_t first_universe_;
    uint16_t universe_count_;
    bool enabled_;
};

} // namespace lg
