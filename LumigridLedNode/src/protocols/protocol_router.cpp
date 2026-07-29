#include "protocols/protocol_router.hpp"
#include "protocols/lg_artnet.hpp"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <Arduino.h>

namespace lg {

static const char* TAG = "lg_protocol_router";
static constexpr uint32_t kCoalesceMs = 10U;
static constexpr TickType_t kCoalesceTicks = pdMS_TO_TICKS(kCoalesceMs);

ProtocolRouter::ProtocolRouter()
    : artnet_staging_{}
    , live_frame_{}
    , frame_mux_(xSemaphoreCreateMutex())
    , dirty_(false)
    , last_dmx_ms_(0) {}

ProtocolRouter::~ProtocolRouter() {
    if (frame_mux_) {
        vSemaphoreDelete(frame_mux_);
        frame_mux_ = nullptr;
    }
}

bool ProtocolRouter::begin(const NodeConfig& cfg) {
    config_ = cfg;

    artnet_.set_universe_range(
        config_.artnet.first_universe,
        config_.artnet.universe_count
    );
    artnet_.set_enabled(config_.artnet.enabled);

    return artnet_.start([this](const DmxPacket& pkt) {
        this->on_dmx_(pkt);
    });
}

void ProtocolRouter::loop() {
    const uint32_t now = millis();
    if (!dirty_) return;
    if (!frame_mux_) return;

    if (now - last_dmx_ms_ >= kCoalesceMs &&
        xSemaphoreTake(frame_mux_, 0) == pdTRUE) {
        publish_frame();
    }
}

void ProtocolRouter::publish_frame() {
    if (!dirty_) return;

    std::swap(live_frame_, artnet_staging_);
    last_dmx_ms_ = millis();
    dirty_ = false;
    xSemaphoreGive(frame_mux_);
}

void ProtocolRouter::on_dmx_(const DmxPacket& pkt) {
    if (!config_.artnet.enabled) return;

    if (pkt.universe < config_.artnet.first_universe) return;
    if (pkt.universe >= config_.artnet.first_universe +
        config_.artnet.universe_count) {
        return;
    }

    const uint32_t offset =
        static_cast<uint32_t>(pkt.universe - config_.artnet.first_universe) * 512U;

    if (xSemaphoreTake(frame_mux_, kCoalesceTicks) != pdTRUE) {
        return;
    }
    if (artnet_staging_.write(offset, pkt.data, pkt.length)) {
        last_dmx_ms_ = millis();
        dirty_ = true;
    }
    xSemaphoreGive(frame_mux_);
}

void ProtocolRouter::on_sync_() {
    loop();
}

}