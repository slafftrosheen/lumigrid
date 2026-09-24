// components/network/WifiService.cpp
#include "WifiService.hpp"

#include <algorithm>
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
constexpr uint32_t kBackoffDelaysSec[] = { 5, 10, 30, 60 };
}

void WifiService::on_fallback_timer(void* arg) {
    auto* self = static_cast<WifiService*>(arg);
    if (!self || self->intentional_disconnect_.load(std::memory_order_relaxed)) {
        return;
    }
    if (self->is_fallback_ap_.load(std::memory_order_relaxed)) {
        ESP_LOGI(kTag, "Fallback AP: periodic background attempt to rejoin STA...");
        esp_wifi_connect();
    }
}

void WifiService::schedule_fallback_retry() {
    cancel_fallback_timer();
    if (intentional_disconnect_.load(std::memory_order_relaxed)) return;

    size_t idx = std::min<size_t>(fallback_backoff_index_, 3);
    uint32_t delay_sec = kBackoffDelaysSec[idx];
    if (fallback_backoff_index_ < 3) {
        fallback_backoff_index_++;
    }

    ESP_LOGI(kTag, "Scheduling STA rejoin retry in %" PRIu32 "s (backoff step %u)", delay_sec, fallback_backoff_index_);

    if (!fallback_timer_) {
        esp_timer_create_args_t timer_args = {
            .callback = &WifiService::on_fallback_timer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_fb_timer",
            .skip_unhandled_events = true
        };
        esp_timer_create(&timer_args, &fallback_timer_);
    }

    if (fallback_timer_) {
        esp_timer_start_once(fallback_timer_, static_cast<uint64_t>(delay_sec) * 1000000ULL);
    }
}

void WifiService::cancel_fallback_timer() {
    if (fallback_timer_) {
        esp_timer_stop(fallback_timer_);
    }
}

esp_err_t WifiService::start(const WifiConfig& config,
                             WifiStateCallback callback) {
    config_ = config;
    callback_ = std::move(callback);
    intentional_disconnect_.store(false, std::memory_order_relaxed);
    is_fallback_ap_.store(false, std::memory_order_relaxed);
    fallback_backoff_index_ = 0;
    retries_ = 0;
    last_disconnect_reason_.store(0, std::memory_order_relaxed);

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
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "wifi ap start");
    return ESP_OK;
}

esp_err_t WifiService::start_fallback_ap() {
    ESP_LOGW(kTag, "STA connection failed after %u attempts. Activating recovery SoftAP (AP+STA mode)...", retries_);

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

    // Use APSTA mode so SoftAP serves operator while STA continues background reconnect
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    is_fallback_ap_.store(true, std::memory_order_relaxed);
    state_ = WifiState::FallbackAp;
    has_ip_.store(true, std::memory_order_release); // Recovery AP interface is usable

    if (callback_) {
        callback_(WifiState::FallbackAp);
    }

    schedule_fallback_retry();
    return ESP_OK;
}

void WifiService::event_handler(void* arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data) {
    auto* self = static_cast<WifiService*>(arg);

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (!self->intentional_disconnect_.load(std::memory_order_relaxed)) {
                esp_wifi_connect();
            }
            return;
        }

        if (event_id == WIFI_EVENT_AP_START) {
            ESP_LOGI(kTag, "SoftAP started successfully (SSID: %s, Channel: %d)",
                     self->config_.ap_ssid.c_str(), self->config_.ap_channel);
            if (!self->is_fallback_ap_.load(std::memory_order_relaxed)) {
                self->state_ = WifiState::ApActive;
                self->has_ip_ = true;
                if (self->callback_) self->callback_(self->state_);
            }
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
            auto* dis_event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
            if (dis_event) {
                self->last_disconnect_reason_.store(dis_event->reason, std::memory_order_relaxed);
            }

            // Check if this was an administrative disconnect (e.g. reconfigure / stop)
            if (self->intentional_disconnect_.load(std::memory_order_relaxed)) {
                ESP_LOGI(kTag, "Intentional disconnect; skipping automatic reconnect policy");
                self->has_ip_ = false;
                self->state_ = WifiState::Idle;
                return;
            }

            // If we are in fallback AP mode, don't spin in retry loops; use bounded backoff timer
            if (self->is_fallback_ap_.load(std::memory_order_relaxed)) {
                self->schedule_fallback_retry();
                return;
            }

            self->has_ip_ = false;

            if (self->retries_ < self->config_.max_retry) {
                ++self->retries_;
                ESP_LOGI(kTag, "STA disconnected (reason: %u). Retrying (%u/%u)...",
                         self->last_disconnect_reason_.load(), self->retries_, self->config_.max_retry);
                esp_wifi_connect();
                self->state_ = WifiState::Connecting;
            } else if (self->config_.mode == WifiMode::StaWithApFallback) {
                self->start_fallback_ap();
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
        self->cancel_fallback_timer();
        self->fallback_backoff_index_ = 0;
        self->is_fallback_ap_.store(false, std::memory_order_relaxed);
        self->has_ip_ = true;
        self->state_ = WifiState::Connected;
        ESP_LOGI(kTag, "STA connected and acquired IP address");
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
    intentional_disconnect_.store(true, std::memory_order_relaxed);
    cancel_fallback_timer();
    esp_wifi_stop();
    state_ = WifiState::Idle;
    has_ip_ = false;
    callback_ = nullptr;
    retries_ = 0;
    is_fallback_ap_.store(false, std::memory_order_relaxed);
    return ESP_OK;
}

WifiRuntimeStatus WifiService::status() const {
    WifiRuntimeStatus st{};
    st.state = state_;
    st.mode = config_.mode;
    st.retry_count = retries_;
    st.last_disconnect_reason = last_disconnect_reason_.load(std::memory_order_relaxed);
    st.is_fallback_ap = is_fallback_ap_.load(std::memory_order_relaxed);
    st.sta_connected = (state_ == WifiState::Connected);
    st.ap_active = (state_ == WifiState::ApActive || st.is_fallback_ap);

    std::strncpy(st.sta_ssid, config_.sta_ssid.c_str(), sizeof(st.sta_ssid) - 1);
    st.sta_ssid[sizeof(st.sta_ssid) - 1] = '\0';

    if (st.sta_connected) {
        wifi_ap_record_t ap_info{};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            st.sta_rssi = ap_info.rssi;
        }
    }

    if (sta_netif_) {
        esp_netif_ip_info_t ip_info{};
        if (esp_netif_get_ip_info(sta_netif_, &ip_info) == ESP_OK) {
            st.sta_ip = ip_info.ip;
        }
    }

    if (ap_netif_) {
        esp_netif_ip_info_t ip_info{};
        if (esp_netif_get_ip_info(ap_netif_, &ip_info) == ESP_OK) {
            st.ap_ip = ip_info.ip;
        }
    }

    return st;
}

} // namespace lg
