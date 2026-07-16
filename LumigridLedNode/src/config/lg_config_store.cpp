#include "config/lg_config_store.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include <cstring>

namespace lg {

static const char* TAG = "lg_config";

esp_err_t ConfigStore::init() {
    // NVS partition initialization would go here in a production build.
    return ESP_OK;
}

esp_err_t ConfigStore::load(NodeConfig& out) {
    // Attempt to read a persisted blob from NVS. For now, default to factory.
    NodeConfig factory{};
    esp_err_t err = reset_to_factory(factory);
    if (err != ESP_OK) {
        return err;
    }
    out = factory;
    return ESP_OK;
}

ConfigError ConfigStore::validate(const NodeConfig& cfg) const {
    if (cfg.schema_version != 1) {
        return ConfigError::InvalidSchema;
    }

    if (cfg.lanes.empty()) {
        return ConfigError::InvalidLane;
    }

    for (size_t i = 0; i < cfg.lanes.size(); ++i) {
        const PixelLaneConfig& lane = cfg.lanes[i];
        if (!lane.enabled) {
            continue;
        }
        if (lane.gpio == 0) {
            return ConfigError::InvalidLane;
        }
        if (lane.pixel_count == 0 || lane.pixel_count > kMaxPixelsPerLane) {
            return ConfigError::InvalidLane;
        }
        if (lane.logical_start + lane.pixel_count >
            static_cast<uint16_t>(kPixelLanes * kMaxPixelsPerLane)) {
            return ConfigError::InvalidLane;
        }
    }

    bool used_pwm_ch[kPwmChannels] = {};

    for (const PwmGroupConfig& group : cfg.pwm_groups) {
        if (!group.enabled) {
            continue;
        }
        if (group.channel_count == 0) {
            return ConfigError::InvalidPwmGroup;
        }
        if (group.first_channel + group.channel_count > kPwmChannels) {
            return ConfigError::InvalidPwmGroup;
        }
        if (group.logical_start + group.channel_count >
            static_cast<uint16_t>(kPwmChannels * 4095)) {
            return ConfigError::InvalidPwmGroup;
        }
        for (uint8_t ch = group.first_channel;
             ch < group.first_channel + group.channel_count;
             ++ch) {
            if (used_pwm_ch[ch]) {
                return ConfigError::PwmOverlap;
            }
            used_pwm_ch[ch] = true;
        }
    }

    if (cfg.artnet.enabled) {
        if (cfg.artnet.first_universe + cfg.artnet.universe_count >
            static_cast<uint32_t>(kPixelLanes * 16)) {
            return ConfigError::InvalidNetwork;
        }
    }

    if (cfg.mqtt.enabled) {
        if (cfg.mqtt.uri.empty() || cfg.mqtt.base_topic.empty()) {
            return ConfigError::InvalidNetwork;
        }
    }

    return ConfigError::None;
}

esp_err_t ConfigStore::save(const NodeConfig& cfg) {
    ConfigError err = validate(cfg);
    if (err != ConfigError::None) {
        ESP_LOGW(TAG, "Rejecting config save: validation error %d", static_cast<int>(err));
        return ESP_ERR_INVALID_ARG;
    }
    // NVS write would go here.
    return ESP_OK;
}

esp_err_t ConfigStore::reset_to_factory(NodeConfig& out) {
    out = NodeConfig{};
    out.schema_version = 1;
    out.node_id = "lumigrid-node";
    out.group_id = "default";
    out.target_fps = 60;
    out.pwm_frequency_hz = 1000;
    out.max_pixel_ma = 0;
    out.artnet.enabled = true;
    out.artnet.first_universe = 0;
    out.artnet.universe_count = 16;
    out.artnet.source_timeout_ms = 3000;
    out.artnet.accept_broadcast = true;
    out.mqtt.enabled = false;
    out.mqtt.tls_verify = true;
    for (size_t i = 0; i < kPixelLanes; ++i) {
        out.lanes[i].enabled = false;
        out.lanes[i].brightness = 255;
    }
    for (size_t i = 0; i < kPwmChannels; ++i) {
        out.pwm_groups[i].enabled = false;
    }
    return ESP_OK;
}

} // namespace lg
