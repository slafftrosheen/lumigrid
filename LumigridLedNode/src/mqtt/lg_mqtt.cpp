#include "mqtt/lg_mqtt.hpp"
#include "esp_log.h"
#include <cstring>

namespace lg {

static const char* TAG = "lg_mqtt";

MqttClient::~MqttClient() {
    stop();
}

bool MqttClient::begin(const MqttConfig& cfg, const std::string& node_id) {
    if (!cfg.enabled) return false;

    cfg_ = cfg;
    node_id_ = node_id;
    base_topic_ = "lumigrid/" + node_id_;
    client_id_ = "lumigrid-" + node_id_;

    return connect_();
}

void MqttClient::stop() {
    connected_ = false;
    publish_availability_(false);
}

void MqttClient::set_command_handler(MqttCommandHandler handler) {
    command_handler_ = std::move(handler);
}

bool MqttClient::connect_() {
    // Production: connect via ESP-MQTT with TLS if cfg_.uri starts with mqtts://.
    // Stub: mark connected and publish availability.
    connected_ = true;
    status_.connected = true;
    status_.client_id = client_id_;
    publish_availability_(true);
    ESP_LOGI(TAG, "MQTT stub connected to %s", cfg_.uri.c_str());
    return true;
}

void MqttClient::publish_availability_(bool online) {
    // Topic: <base_topic>/availability
    // Payload: "online" or "offline", retained.
}

void MqttClient::on_connected_(bool connected) {
    connected_ = connected;
    status_.connected = connected;
    if (connected) {
        publish_availability_(true);
    } else {
        publish_availability_(false);
    }
}

void MqttClient::loop() {
    if (!connected_ || !cfg_.enabled) return;
    // Production: pump ESP-MQTT event loop, process incoming commands.
}

} // namespace lg
