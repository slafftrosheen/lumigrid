#include "MqttService.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"

namespace lg {

static const char* TAG = "mqtt-service";

MqttService::MqttService() {}

MqttService::~MqttService() {
    stop();
    if (telemetry_timer_) {
        xTimerDelete(telemetry_timer_, 0);
        telemetry_timer_ = nullptr;
    }
}

esp_err_t MqttService::init(const char* broker_uri, const char* client_id, const char* topic_prefix, LightingRuntime* signage) {
    if (!broker_uri || strlen(broker_uri) == 0) return ESP_ERR_INVALID_ARG;

    // Clean up any previous client from a prior init() call
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }

    signage_ = signage;

    topic_prefix_ = topic_prefix;
    telemetry_topic_ = topic_prefix_ + "/telemetry";
    log_topic_ = topic_prefix_ + "/logs";
    command_topic_ = topic_prefix_ + "/command";
    availability_topic_ = topic_prefix_ + "/availability";

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = broker_uri;
    mqtt_cfg.credentials.client_id = client_id;
    
    mqtt_cfg.session.last_will.topic = availability_topic_.c_str();
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.msg_len = 7;
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 1;

    // We can add auth here in the future if requested

    client_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_) return ESP_FAIL;

    esp_mqtt_client_register_event(client_, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, this);

    telemetry_timer_ = xTimerCreate("mqtt_tel", pdMS_TO_TICKS(60000), pdTRUE, this, telemetry_timer_cb);
    return ESP_OK;
}

esp_err_t MqttService::start() {
    if (!client_) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_start(client_);
}

void MqttService::stop() {
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    if (telemetry_timer_) {
        xTimerStop(telemetry_timer_, 0);
    }
    connected_ = false;
}

void MqttService::publish_telemetry(const char* payload) {
    if (connected_ && client_) {
        esp_mqtt_client_publish(client_, telemetry_topic_.c_str(), payload, 0, 0, 0);
    }
}

void MqttService::publish_log(const char* log_msg, size_t len) {
    if (connected_ && client_) {
        // Publish log chunks QoS 0 to avoid blocking
        esp_mqtt_client_publish(client_, log_topic_.c_str(), log_msg, len, 0, 0);
    }
}

void MqttService::telemetry_timer_cb(TimerHandle_t xTimer) {
    MqttService* svc = static_cast<MqttService*>(pvTimerGetTimerID(xTimer));
    if (!svc->connected_) return;

    wifi_ap_record_t ap_info;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    float fps = 0.0f;
    uint16_t active_preset = 0;
    if (svc->signage_) {
        fps = svc->signage_->get_fps();
        active_preset = svc->signage_->active_scene_index();
    }

    char body[256];
    snprintf(body, sizeof(body),
        "{\"uptime_s\":%lu,\"free_heap\":%lu,\"reset_reason\":%d,\"wifi_rssi\":%d,\"fps\":%.1f,\"active_preset\":%u}",
        (unsigned long)(esp_timer_get_time() / 1000000ULL),
        (unsigned long)esp_get_free_heap_size(),
        (int)esp_reset_reason(),
        rssi,
        fps,
        active_preset);

    svc->publish_telemetry(body);
}

void MqttService::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    MqttService* svc = static_cast<MqttService*>(handler_args);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            svc->connected_ = true;
            esp_mqtt_client_publish(svc->client_, svc->availability_topic_.c_str(), "online", 6, 1, 1);
            esp_mqtt_client_subscribe(svc->client_, svc->command_topic_.c_str(), 1);
            if (svc->telemetry_timer_) {
                xTimerStart(svc->telemetry_timer_, 0);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected");
            svc->connected_ = false;
            if (svc->telemetry_timer_) {
                xTimerStop(svc->telemetry_timer_, 0);
            }
            break;
        case MQTT_EVENT_DATA:
            if (event->topic_len > 0 && strncmp(event->topic, svc->command_topic_.c_str(), event->topic_len) == 0) {
                if (svc->command_cb_) {
                    svc->command_cb_(event->data, event->data_len);
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
        default:
            break;
    }
}

} // namespace lg
