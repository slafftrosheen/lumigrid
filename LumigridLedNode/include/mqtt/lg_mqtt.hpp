#pragma once

#include <functional>
#include <string>
#include "config/lg_config.hpp"

namespace lg {

struct MqttStatus {
    bool connected = false;
    std::string client_id;
};

using MqttCommandHandler = std::function<void(const std::string& topic, const std::string& payload)>;

class MqttClient {
public:
    MqttClient() = default;
    ~MqttClient();

    bool begin(const MqttConfig& cfg, const std::string& node_id);
    void stop();
    void loop();

    void set_command_handler(MqttCommandHandler handler);
    MqttStatus status() const { return status_; }

private:
    bool connect_();
    void publish_availability_(bool online);
    void on_connected_(bool connected);

    MqttConfig cfg_;
    std::string node_id_;
    std::string base_topic_;
    std::string client_id_;
    MqttCommandHandler command_handler_;
    MqttStatus status_;
    bool connected_ = false;
};

} // namespace lg
