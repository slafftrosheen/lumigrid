// components/network/WifiService.cpp
#include "WifiService.hpp"

#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace lg {
namespace {
constexpr char kTag[] = "wifi";
}

esp_err_t WifiService::start(const WifiConfig& config,
                             WifiStateCallback callback) {
    config_ = config;
    callback_ = std::move(callback);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, kTag, "nvs init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), kTag, "event loop");

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), kTag, "wifi init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &WifiService::event_handler,
            this,
            nullptr
        ),
        kTag,
        "wifi event"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &WifiService::event_handler,
            this,
            nullptr
        ),
        kTag,
        "ip event"
    );

    switch (config.mode) {
        case WifiMode::StaOnly:
        case WifiMode::StaWithApFallback:
            return start_sta();
        case WifiMode::ApOnly:
            return start_ap();
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t WifiService::start_sta() {
    wifi_config_t wifi_cfg = {};
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
                 config_.sta_ssid.c_str(),
                 sizeof(wifi_cfg.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
                 config_.sta_password.c_str(),
                 sizeof(wifi_cfg.sta.password) - 1);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set sta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), kTag, "sta cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), kTag, "wifi connect");

    state_ = WifiState::Connecting;
    if (callback_) callback_(state_);
    return ESP_OK;
}

esp_err_t WifiService::start_ap() {
    wifi_config_t ap_cfg = {};
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid),
                 config_.ap_ssid.c_str(),
                 sizeof(ap_cfg.ap.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.password),
                 config_.ap_password.c_str(),
                 sizeof(ap_cfg.ap.password) - 1);

    ap_cfg.ap.ssid_len = config_.ap_ssid.size();
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = config_.ap_password.empty()
        ? WIFI_AUTH_OPEN
        : WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "set ap mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), kTag, "ap cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi ap start");

    state_ = WifiState::ApActive;
    has_ip_ = true;
    if (callback_) callback_(state_);
    return ESP_OK;
}

void WifiService::event_handler(void* arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data) {
    auto* self = static_cast<WifiService*>(arg);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        self->has_ip_ = false;

        if (self->retries_ < self->config_.max_retry) {
            ++self->retries_;
            esp_wifi_connect();
            self->state_ = WifiState::Connecting;
        } else if (self->config_.mode == WifiMode::StaWithApFallback) {
            esp_wifi_stop();
            self->start_ap();
            return;
        } else {
            self->state_ = WifiState::Failed;
        }

        if (self->callback_) self->callback_(self->state_);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        self->retries_ = 0;
        self->has_ip_ = true;
        self->state_ = WifiState::Connected;
        if (self->callback_) self->callback_(self->state_);
    }
}

WifiState WifiService::state() const {
    return state_;
}

bool WifiService::has_ip() const {
    return has_ip_;
}

} // namespace lg
