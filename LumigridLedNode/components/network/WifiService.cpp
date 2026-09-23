// components/network/WifiService.cpp
#include "WifiService.hpp"

#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
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

    if (!wifi_init_done_) {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase");
            err = nvs_flash_init();
        }
        ESP_RETURN_ON_ERROR(err, kTag, "nvs init");

        esp_err_t netif_err = esp_netif_init();
        if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(netif_err, kTag, "netif");
        }
        
        esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(loop_err, kTag, "event loop");
        }

        if (!sta_netif_) {
            sta_netif_ = esp_netif_create_default_wifi_sta();
        }
        if (!ap_netif_) {
            ap_netif_ = esp_netif_create_default_wifi_ap();
        }

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), kTag, "wifi init");

        if (!wifi_event_instance_) {
            ESP_RETURN_ON_ERROR(
                esp_event_handler_instance_register(
                    WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &WifiService::event_handler,
                    this,
                    &wifi_event_instance_
                ),
                kTag,
                "wifi event"
            );
        }

        if (!ip_event_instance_) {
            ESP_RETURN_ON_ERROR(
                esp_event_handler_instance_register(
                    IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &WifiService::event_handler,
                    this,
                    &ip_event_instance_
                ),
                kTag,
                "ip event"
            );
        }

        wifi_init_done_ = true;
    }

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
    // esp_wifi_start() triggers WIFI_EVENT_STA_START, which calls esp_wifi_connect().
    // Do NOT call esp_wifi_connect() here — doing so causes a double-connect race.
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi start");

    state_ = WifiState::Connecting;
    return ESP_OK;
}

esp_err_t WifiService::start_ap() {
    wifi_config_t ap_cfg = {};
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid),
                 config_.ap_ssid.c_str(),
                 sizeof(ap_cfg.ap.ssid) - 1);

    ap_cfg.ap.ssid_len = config_.ap_ssid.size();
    ap_cfg.ap.channel = (config_.ap_channel >= 1 && config_.ap_channel <= 13) ? config_.ap_channel : 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.ssid_hidden = 0;

    if (config_.ap_password.empty() || config_.ap_password.length() < 8) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.password[0] = '\0';
    } else {
        std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.password),
                     config_.ap_password.c_str(),
                     sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_LOGI(kTag, "Configuring SoftAP: SSID='%s', channel=%u, authmode=%s",
             ap_cfg.ap.ssid,
             ap_cfg.ap.channel,
             (ap_cfg.ap.authmode == WIFI_AUTH_OPEN) ? "OPEN" : "WPA2_PSK");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "set ap mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), kTag, "ap cfg");
    // esp_wifi_start() triggers WIFI_EVENT_AP_START, which calls callback_(ApActive).
    // Do NOT call callback_ here — doing so is a double-give race on net_ready_sem_.
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi ap start");
    return ESP_OK;
}

void WifiService::event_handler(void* arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data) {
    auto* self = static_cast<WifiService*>(arg);

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
            return;
        }

        if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(kTag, "SoftAP started successfully (SSID: %s, Channel: %d)",
                     self->config_.ap_ssid.c_str(), self->config_.ap_channel);
            self->state_ = WifiState::ApActive;
            self->has_ip_ = true;
            if (self->callback_) self->callback_(self->state_);
            return;
        }

        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            auto* event = static_cast<wifi_event_ap_staconnected_t*>(event_data);
            ESP_LOGI(kTag, "Station joined SoftAP: MAC=" MACSTR ", AID=%d",
                     MAC2STR(event->mac), event->aid);
            return;
        }

        if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
            ESP_LOGI(kTag, "Station left SoftAP: MAC=" MACSTR ", AID=%d",
                     MAC2STR(event->mac), event->aid);
            return;
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
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
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        self->retries_ = 0;
        self->has_ip_ = true;
        self->state_ = WifiState::Connected;
        if (self->callback_) self->callback_(self->state_);
        return;
    }
}

WifiState WifiService::state() const {
    return state_;
}

bool WifiService::has_ip() const {
    return has_ip_;
}

esp_err_t WifiService::stop() {
    esp_wifi_stop();
    state_ = WifiState::Idle;
    has_ip_ = false;
    callback_ = nullptr;
    retries_ = 0;
    return ESP_OK;
}

} // namespace lg

