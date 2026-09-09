// components/protocols/include/SacnReceiver.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <array>
#include <mutex>

#include "esp_err.h"

namespace lg {

struct SacnDmxPacket {
    uint16_t universe = 0;
    uint16_t length = 0;
    uint8_t sequence = 0;
    uint8_t priority = 0;
    uint32_t cid_hash = 0;
    uint8_t cid[16] = {};
    uint8_t data[512] = {};
};

using SacnDmxCallback = std::function<void(const SacnDmxPacket&)>;

class SacnReceiver {
public:
    esp_err_t start(SacnDmxCallback callback);

    /// Configure active universes for multicast group membership (IGMP join/leave).
    void set_subscribed_universes(const uint16_t* universes, size_t count);

private:
    static void task_entry(void* arg);
    void task();
    bool handle_packet(const uint8_t* packet, size_t length);
    void join_multicast_groups(int sock);
    void leave_multicast_groups(int sock);

    SacnDmxCallback callback_{};
    int sock_ = -1;
    std::mutex mtx_;
    std::array<uint16_t, 16> subscribed_universes_{};
    size_t subscribed_count_ = 0;
};

} // namespace lg
