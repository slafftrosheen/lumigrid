#pragma once
#include <protocols/lg_artnet.hpp>
#include "lg_config.hpp"

namespace lg {

class ProtocolRouter {
public:
    ProtocolRouter();
    ~ProtocolRouter();
    bool begin(const NodeConfig& cfg);
    void loop();
    void publish_frame();

    void set_dmx_callback(DmxCallback cb) {
        artnet_.start(cb, [this]() { this->on_sync_(); });
    }

private:
    void on_dmx_(const DmxPacket& pkt);
    void on_sync_();

    lg::ArtnetReceiver artnet_;
    lg::NodeConfig config_;

    struct FrameBuffer {
        static constexpr size_t kCapacity = 8U * 512U;
        uint8_t data[kCapacity]{};
        size_t length = 0;
        uint32_t updated_ms = 0;

        void clear() {
            length = 0;
            std::memset(data, 0, sizeof(data));
        }
        bool write(size_t offset, const uint8_t* src, size_t src_len) {
            if (offset >= kCapacity) return false;
            size_t copy = src_len;
            if (offset + copy > kCapacity) copy = kCapacity - offset;
            std::memcpy(data + offset, src, copy);
            if (offset + copy > length) length = offset + copy;
            return true;
        }
    };

    FrameBuffer artnet_staging_;
    FrameBuffer live_frame_;
    portMUX_TYPE frame_mux_;
    bool dirty_ = false;
    uint32_t last_dmx_ms_ = 0;
};

}
