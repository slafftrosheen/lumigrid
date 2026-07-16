#include "protocols/lg_artnet.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "freertos/task.h"
#include <cstring>

namespace lg {

static const char* TAG = "lg_artnet";

ArtnetReceiver::ArtnetReceiver()
    : task_handle_(nullptr)
    , sock_(-1)
    , on_dmx_(nullptr)
    , first_universe_(0)
    , universe_count_(0)
    , enabled_(false) {}

ArtnetReceiver::~ArtnetReceiver() {
    stop();
}

bool ArtnetReceiver::parse_packet(const uint8_t* packet, size_t length) {
    if (!enabled_ || length < 18) return false;
    if (std::memcmp(packet, "Art-Net\0", 8) != 0) return false;

    const uint16_t opcode =
        static_cast<uint16_t>(packet[8]) |
        (static_cast<uint16_t>(packet[9]) << 8);

    if (opcode != kOpDmx) return false;

    const uint16_t universe =
        static_cast<uint16_t>(packet[14]) |
        (static_cast<uint16_t>(packet[15]) << 8);
    const uint16_t data_len =
        (static_cast<uint16_t>(packet[16]) << 8) |
        static_cast<uint16_t>(packet[17]);

    if (data_len == 0 || data_len > 512 ||
        length < static_cast<size_t>(18 + data_len)) {
        return false;
    }
    if (universe < first_universe_) return false;
    if (universe >= first_universe_ + universe_count_) return false;

    if (on_dmx_) {
        on_dmx_({
            .universe = universe,
            .sequence = packet[12],
            .data = packet + 18,
            .length = data_len
        });
    }
    return true;
}

void ArtnetReceiver::task_loop() {
    uint8_t buf[600];
    sockaddr_in src;
    socklen_t slen = sizeof(src);

    while (enabled_) {
        const ssize_t len = recvfrom(
            sock_, buf, sizeof(buf), 0,
            reinterpret_cast<sockaddr*>(&src), &slen
        );
        if (len <= 0) continue;
        parse_packet(buf, static_cast<size_t>(len));
    }
}

bool ArtnetReceiver::start(DmxCallback callback) {
    if (!enabled_) return false;
    on_dmx_ = std::move(callback);

    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "socket open failed");
        return false;
    }

    int opt = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kArtnetPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(sock_);
        sock_ = -1;
        return false;
    }

    if (xTaskCreate(
            [](void* arg) {
                static_cast<ArtnetReceiver*>(arg)->task_loop();
                vTaskDelete(nullptr);
            },
            "artnet",
            4096,
            this,
            4,
            &task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        close(sock_);
        sock_ = -1;
        return false;
    }

    return true;
}

void ArtnetReceiver::stop() {
    enabled_ = false;
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

void ArtnetReceiver::set_universe_range(
    uint16_t first, uint16_t count) {
    first_universe_ = first;
    universe_count_ = count;
}

void ArtnetReceiver::set_enabled(bool enabled) { enabled_ = enabled; }
void ArtnetReceiver::loop() {
    /* Non-task idle hook; parsing is driven from the UDP task. */
}

} // namespace lg
