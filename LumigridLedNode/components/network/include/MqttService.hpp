#pragma once

#include "mqtt_client.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string>
#include <functional>

#include "LightingRuntime.hpp"

namespace lg {

class MqttService {
public:
    using CommandCallback = std::function<void(const char* payload, size_t len)>;

    MqttService();
    ~MqttService();

    esp_err_t init(const char* broker_uri, const char* client_id, const char* topic_prefix, LightingRuntime* signage);
    esp_err_t start();
    void stop();

    void set_command_callback(CommandCallback cb) { command_cb_ = cb; }

    // Publish telemetry/health data
    void publish_telemetry(const char* payload);

    // Publish log stream data
    void publish_log(const char* log_msg, size_t len);

private:
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    static void telemetry_timer_cb(TimerHandle_t xTimer);

    esp_mqtt_client_handle_t client_ = nullptr;
    std::string topic_prefix_;
    std::string telemetry_topic_;
    std::string log_topic_;
    std::string command_topic_;
    std::string availability_topic_;
    
    CommandCallback command_cb_;
    TimerHandle_t telemetry_timer_ = nullptr;
    LightingRuntime* signage_ = nullptr;
    bool connected_ = false;
};

} // namespace lg
