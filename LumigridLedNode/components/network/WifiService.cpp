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
#include "NvsRuntime.hpp"

namespace lg {
namespace {
constexpr char kTag[] = "wifi";
constexpr uint32_t kBackoffDelaysSec[] = { 5, 10, 30, 60 };
ESP_EVENT_DEFINE_BASE(kWifiServiceEvent);
constexpr int32_t kRetryStation = 1;
constexpr int32_t kExitFallback = 2;
constexpr int32_t kLifecycleBarrier = 3;
constexpr uint64_t kEventRetryDelayUs = 1000000ULL;
constexpr int64_t kQuiesceTimeoutUs = 1000000;

class LifecycleGuard {
public:
    explicit LifecycleGuard(std::atomic<bool>& busy) : busy_(busy) {
        bool expected = false;
        acquired_ = busy_.compare_exchange_strong(expected, true);
    }
    ~LifecycleGuard() { if (acquired_) busy_.store(false); }
    explicit operator bool() const { return acquired_; }
private:
    std::atomic<bool>& busy_;
    bool acquired_ = false;
};
}

WifiService::Activity::Activity(const WifiService& service) : service_(service) {
    // Sequential consistency pairs admission with the closing gate: a caller
    // either observes the gate or is counted before quiesce observes zero.
    service_.active_operations_.fetch_add(1);
    admitted_ = !service_.intentional_disconnect_.load();
    if (!admitted_) service_.active_operations_.fetch_sub(1);
}

WifiService::Activity::~Activity() {
    if (admitted_) service_.active_operations_.fetch_sub(1);
}

bool WifiService::in_callback() const {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    // Waiting for a barrier from the default event task would wait on itself,
    // including calls made by other handlers registered on that event loop.
    return callback_task_.load() == current || event_task_.load() == current;
}

esp_err_t WifiService::quiesce() {
    intentional_disconnect_.store(true);
    const int64_t deadline = esp_timer_get_time() + kQuiesceTimeoutUs;
    while (active_operations_.load() != 0) {
        if (esp_timer_get_time() >= deadline) {
            wifi_diagnostics_.last_error = ESP_ERR_TIMEOUT;
            ESP_LOGE(kTag, "Timed out waiting for active Wi-Fi work; retaining callback ownership");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    }
    // An entered timer callback may have rearmed itself after admission closed.
    // Its handle and callback are no longer in use, so cancel only after drain.
    cancel_fallback_timer();
    cancel_fallback_exit_timer();
    return ESP_OK;
}

esp_err_t WifiService::drain_event_queue() {
    if (!service_event_instance_) return ESP_OK;
    uint32_t sequence = ++barrier_sequence_;
    if (sequence == 0) sequence = ++barrier_sequence_;
    const int64_t deadline = esp_timer_get_time() + kQuiesceTimeoutUs;
    const esp_err_t err = esp_event_post(kWifiServiceEvent, kLifecycleBarrier,
                                        &sequence, sizeof(sequence), 0);
    if (err != ESP_OK) {
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "Unable to queue Wi-Fi lifecycle barrier: %s", esp_err_to_name(err));
        return err;
    }
    while (barrier_acknowledged_.load() != sequence) {
        if (esp_timer_get_time() >= deadline) {
            wifi_diagnostics_.last_error = ESP_ERR_TIMEOUT;
            ESP_LOGE(kTag, "Timed out draining old Wi-Fi events; admission remains closed");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    }
    return ESP_OK;
}

void WifiService::notify_state(WifiState state) {
    if (!callback_) return;
    const TaskHandle_t previous = callback_task_.exchange(xTaskGetCurrentTaskHandle());
    callback_(state);
    callback_task_.store(previous);
}

WifiDiagnostics WifiService::AtomicDiagnostics::snapshot() const {
    WifiDiagnostics diag{};
    diag.start_attempts = start_attempts.load();
    diag.start_failures = start_failures.load();
    diag.reconnect_attempts = reconnect_attempts.load();
    diag.reconnect_failures = reconnect_failures.load();
    diag.fallback_activations = fallback_activations.load();
    diag.fallback_exit_attempts = fallback_exit_attempts.load();
    diag.fallback_exit_failures = fallback_exit_failures.load();
    diag.disconnect_events = disconnect_events.load();
    diag.last_disconnect_reason = last_disconnect_reason.load();
    diag.last_error = last_error.load();
    return diag;
}

void WifiService::on_fallback_timer(void* arg) {
    auto* self = static_cast<WifiService*>(arg);
    if (!self) return;
    Activity activity(*self);
    if (!activity ||
        !self->is_fallback_ap_.load(std::memory_order_acquire)) {
        return;
    }
    // Timer and Wi-Fi events run on different tasks. Keep state transitions on
    // the default event loop so a disconnect cannot race AP shutdown/retry.
    const esp_err_t err = esp_event_post(kWifiServiceEvent, kRetryStation, nullptr, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "STA retry event queue full: %s", esp_err_to_name(err));
        esp_timer_start_once(self->fallback_timer_, kEventRetryDelayUs);
    }
}

void WifiService::on_fallback_exit_timer(void* arg) {
    auto* self = static_cast<WifiService*>(arg);
    if (!self) return;
    Activity activity(*self);
    if (!activity ||
        !self->fallback_exit_pending_.load(std::memory_order_acquire)) {
        return;
    }
    const esp_err_t err = esp_event_post(kWifiServiceEvent, kExitFallback, nullptr, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Fallback exit event queue full: %s", esp_err_to_name(err));
        esp_timer_start_once(self->fallback_exit_timer_, kEventRetryDelayUs);
    }
}

esp_err_t WifiService::ensure_fallback_timers() {
    // Allocate while admission is closed, before publishing a running service.
    // Recovery must not silently depend on allocating a timer after link loss.
    if (!fallback_timer_) {
        esp_timer_create_args_t timer_args = {
            .callback = &WifiService::on_fallback_timer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_fb_timer",
            .skip_unhandled_events = true
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &fallback_timer_), kTag, "STA retry timer");
    }
    if (!fallback_exit_timer_) {
        esp_timer_create_args_t timer_args = {
            .callback = &WifiService::on_fallback_exit_timer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_fb_exit",
            .skip_unhandled_events = true
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &fallback_exit_timer_), kTag, "fallback exit timer");
    }
    return ESP_OK;
}

void WifiService::schedule_fallback_retry() {
    cancel_fallback_timer();
    if (intentional_disconnect_.load()) return;

    size_t idx = std::min<size_t>(fallback_backoff_index_, 3);
    uint32_t delay_sec = kBackoffDelaysSec[idx];
    if (fallback_backoff_index_ < 3) {
        fallback_backoff_index_++;
    }

    ESP_LOGI(kTag, "Scheduling STA rejoin retry in %" PRIu32 "s (backoff step %u)", delay_sec, fallback_backoff_index_);
    if (fallback_timer_) {
        esp_timer_start_once(fallback_timer_, static_cast<uint64_t>(delay_sec) * 1000000ULL);
    }
}

void WifiService::cancel_fallback_timer() {
    if (fallback_timer_) {
        esp_timer_stop(fallback_timer_);
    }
}

void WifiService::schedule_exit_fallback_ap() {
    Activity activity(*this);
    if (!activity ||
        !is_fallback_ap_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) != WifiState::Connected) {
        return;
    }
    cancel_fallback_exit_timer();
    fallback_exit_pending_.store(true, std::memory_order_release);

    if (fallback_exit_timer_) {
        // Deferred 50ms to allow IP_EVENT handling to cleanly return
        esp_timer_start_once(fallback_exit_timer_, 50000ULL);
    }
}

void WifiService::cancel_fallback_exit_timer() {
    fallback_exit_pending_.store(false, std::memory_order_release);
    if (fallback_exit_timer_) {
        esp_timer_stop(fallback_exit_timer_);
    }
}

void WifiService::perform_exit_fallback_ap() {
    Activity activity(*this);
    if (!activity ||
        !is_fallback_ap_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) != WifiState::Connected ||
        !fallback_exit_pending_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    wifi_diagnostics_.fallback_exit_attempts++;
    ESP_LOGI(kTag, "Transitioning from Fallback AP (APSTA) to normal Station (STA) mode...");

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        wifi_diagnostics_.fallback_exit_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "Failed to switch mode to WIFI_MODE_STA (%s). Remaining in APSTA fallback mode.", esp_err_to_name(err));
        return;
    }

    wifi_mode_t cur_mode = WIFI_MODE_NULL;
    err = esp_wifi_get_mode(&cur_mode);
    if (err != ESP_OK || cur_mode != WIFI_MODE_STA) {
        wifi_diagnostics_.fallback_exit_failures++;
        wifi_diagnostics_.last_error = (err != ESP_OK) ? err : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Failed to confirm WIFI_MODE_STA after set_mode. Remaining degraded in APSTA.");
        return;
    }

    is_fallback_ap_.store(false, std::memory_order_release);
    fallback_exit_pending_.store(false, std::memory_order_release);
    ESP_LOGI(kTag, "Successfully exited fallback AP mode. Device restored to STA-only mode.");
    notify_state(state_);
}

esp_err_t WifiService::start(const WifiConfig& config,
                             WifiStateCallback callback) {
    if (in_callback()) return ESP_ERR_INVALID_STATE;
    LifecycleGuard lifecycle(lifecycle_busy_);
    if (!lifecycle) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(quiesce(), kTag, "Wi-Fi start quiescence");

    config_ = config;
    configured_mode_ = config.mode;
    callback_ = std::move(callback);
    is_fallback_ap_.store(false, std::memory_order_relaxed);
    fallback_exit_pending_.store(false, std::memory_order_relaxed);
    fallback_backoff_index_ = 0;
    retries_ = 0;
    state_.store(WifiState::Idle, std::memory_order_relaxed);
    has_ip_.store(false, std::memory_order_relaxed);
    last_disconnect_reason_.store(0, std::memory_order_relaxed);

    if (!wifi_init_done_) {
        if (!NvsRuntime::ready()) {
            esp_err_t nvs_err = NvsRuntime::initialise();
            if (nvs_err != ESP_OK) {
                ESP_LOGE(kTag, "NVS storage not ready for WiFi service: %s", esp_err_to_name(nvs_err));
                return nvs_err;
            }
        }

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
        if (!sta_netif_ || !ap_netif_) return ESP_ERR_NO_MEM;

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), kTag, "wifi init");
        ESP_LOGI("wifi", "WiFi/network domain CPU0");

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

        if (!service_event_instance_) {
            ESP_RETURN_ON_ERROR(
                esp_event_handler_instance_register(
                    kWifiServiceEvent,
                    ESP_EVENT_ANY_ID,
                    &WifiService::event_handler,
                    this,
                    &service_event_instance_
                ),
                kTag,
                "wifi service event"
            );
        }

        wifi_init_done_ = true;
    }

    if (config.mode == WifiMode::StaWithApFallback) {
        const esp_err_t err = ensure_fallback_timers();
        if (err != ESP_OK) {
            wifi_diagnostics_.start_attempts++;
            wifi_diagnostics_.start_failures++;
            wifi_diagnostics_.last_error = err;
            state_ = WifiState::Failed;
            return err;
        }
    }
    // Drop old STA/IP events before publishing the new session. Mode checks
    // alone cannot distinguish two consecutive STA configurations.
    ESP_RETURN_ON_ERROR(drain_event_queue(), kTag, "Wi-Fi start event barrier");
    intentional_disconnect_.store(false);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    switch (config.mode) {
        case WifiMode::StaOnly:
        case WifiMode::StaWithApFallback:
            err = start_sta();
            break;
        case WifiMode::ApOnly:
            err = start_ap();
            break;
    }
    if (err != ESP_OK) {
        const esp_err_t drain_err = quiesce();
        if (drain_err != ESP_OK) return drain_err;
        state_ = WifiState::Failed;
        has_ip_ = false;
    }
    return err;
}

esp_err_t WifiService::start_sta() {
    wifi_diagnostics_.start_attempts++;
    wifi_config_t wifi_cfg = {};
    // IDF accepts a full 32-byte SSID / 64-byte hexadecimal PSK without a
    // terminator in these fixed-width fields. The struct is already zeroed.
    std::memcpy(wifi_cfg.sta.ssid, config_.sta_ssid.data(),
                std::min(config_.sta_ssid.size(), sizeof(wifi_cfg.sta.ssid)));
    std::memcpy(wifi_cfg.sta.password, config_.sta_password.data(),
                std::min(config_.sta_password.size(), sizeof(wifi_cfg.sta.password)));

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(err));
        return err;
    }
    // STA_START/GOT_IP may run before esp_wifi_start() returns.
    state_ = WifiState::Connecting;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        state_ = WifiState::Failed;
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_start(STA) failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t WifiService::start_ap() {
    wifi_diagnostics_.start_attempts++;
    wifi_config_t ap_cfg = {};
    std::memcpy(ap_cfg.ap.ssid, config_.ap_ssid.data(),
                std::min(config_.ap_ssid.size(), sizeof(ap_cfg.ap.ssid)));

    ap_cfg.ap.ssid_len = std::min(config_.ap_ssid.size(), sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.channel = (config_.ap_channel >= 1 && config_.ap_channel <= 13) ? config_.ap_channel : 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.ssid_hidden = 0;

    if (config_.ap_password.empty() || config_.ap_password.length() < 8) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.password[0] = '\0';
    } else {
        std::memcpy(ap_cfg.ap.password, config_.ap_password.data(),
                    std::min(config_.ap_password.size(), sizeof(ap_cfg.ap.password)));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_LOGI(kTag, "Configuring SoftAP: SSID='%.*s', channel=%u, authmode=%s",
             static_cast<int>(ap_cfg.ap.ssid_len),
             ap_cfg.ap.ssid,
             ap_cfg.ap.channel,
             (ap_cfg.ap.authmode == WIFI_AUTH_OPEN) ? "OPEN" : "WPA2_PSK");

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        wifi_diagnostics_.start_failures++;
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_start(AP) failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t WifiService::start_fallback_ap() {
    ESP_LOGW(kTag, "STA connection failed after %u attempts. Activating recovery SoftAP (AP+STA mode)...", retries_.load());

    wifi_config_t ap_cfg = {};
    std::memcpy(ap_cfg.ap.ssid, config_.ap_ssid.data(),
                std::min(config_.ap_ssid.size(), sizeof(ap_cfg.ap.ssid)));
    ap_cfg.ap.ssid_len = std::min(config_.ap_ssid.size(), sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.channel = (config_.ap_channel >= 1 && config_.ap_channel <= 13) ? config_.ap_channel : 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.ssid_hidden = 0;

    if (config_.ap_password.empty() || config_.ap_password.length() < 8) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ap_cfg.ap.password[0] = '\0';
    } else {
        std::memcpy(ap_cfg.ap.password, config_.ap_password.data(),
                    std::min(config_.ap_password.size(), sizeof(ap_cfg.ap.password)));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    // Use APSTA mode so SoftAP serves operator while STA continues background reconnect
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "Failed to set APSTA mode for fallback AP: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "Failed to set AP config for fallback AP: %s", esp_err_to_name(err));
        return err;
    }

    wifi_diagnostics_.fallback_activations++;
    is_fallback_ap_.store(true, std::memory_order_release);
    fallback_exit_pending_.store(false, std::memory_order_release);
    state_ = WifiState::FallbackAp;
    has_ip_.store(true, std::memory_order_release); // Recovery AP interface is usable

    notify_state(WifiState::FallbackAp);

    schedule_fallback_retry();
    return ESP_OK;
}

void WifiService::handle_connect_failure() {
    // A synchronous driver failure need not emit STA_DISCONNECTED. Enter the
    // configured recovery path now instead of remaining Connecting forever.
    if (config_.mode == WifiMode::StaWithApFallback && start_fallback_ap() == ESP_OK) {
        return;
    }
    state_ = WifiState::Failed;
    has_ip_ = false;
    notify_state(WifiState::Failed);
}

void WifiService::event_handler(void* arg,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void* event_data) {
    auto* self = static_cast<WifiService*>(arg);
    // The driver may already have queued AP_START/GOT_IP when stop() returns.
    // Do not revive stopped network state from those late events.
    if (!self) return;
    self->event_task_.store(xTaskGetCurrentTaskHandle());
    // The barrier must run while normal event admission is closed. FIFO event
    // delivery proves all events queued before it have passed the closed gate.
    if (event_base == kWifiServiceEvent && event_id == kLifecycleBarrier) {
        if (event_data) {
            uint32_t sequence = 0;
            std::memcpy(&sequence, event_data, sizeof(sequence));
            self->barrier_acknowledged_.store(sequence);
        }
        return;
    }
    Activity activity(*self);
    if (!activity) {
        return;
    }

    if (event_base == kWifiServiceEvent) {
        if (event_id == kExitFallback) {
            self->perform_exit_fallback_ap();
        } else if (event_id == kRetryStation &&
                   self->is_fallback_ap_.load(std::memory_order_acquire) &&
                   self->state_.load(std::memory_order_acquire) != WifiState::Connected) {
            ESP_LOGI(kTag, "Fallback AP: periodic background attempt to rejoin STA...");
            self->wifi_diagnostics_.reconnect_attempts++;
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                self->wifi_diagnostics_.reconnect_failures++;
                self->wifi_diagnostics_.last_error = err;
                ESP_LOGW(kTag, "Background STA reconnect attempt failed: %s", esp_err_to_name(err));
                // An immediate driver error need not produce DISCONNECTED.
                // Re-arm explicitly rather than losing the one-shot retry.
                self->schedule_fallback_retry();
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (self->config_.mode == WifiMode::ApOnly) return;
            if (!self->intentional_disconnect_.load(std::memory_order_relaxed)) {
                self->wifi_diagnostics_.reconnect_attempts++;
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    self->wifi_diagnostics_.reconnect_failures++;
                    self->wifi_diagnostics_.last_error = err;
                    ESP_LOGE(kTag, "esp_wifi_connect failed on STA_START: %s", esp_err_to_name(err));
                    self->handle_connect_failure();
                }
            }
            return;
        }

        if (event_id == WIFI_EVENT_AP_START) {
            if (self->config_.mode != WifiMode::ApOnly &&
                !self->is_fallback_ap_.load(std::memory_order_acquire)) return;
            ESP_LOGI(kTag, "SoftAP started successfully (SSID: %s, Channel: %d)",
                     self->config_.ap_ssid.c_str(), self->config_.ap_channel);
            if (!self->is_fallback_ap_.load(std::memory_order_relaxed)) {
                self->state_ = WifiState::ApActive;
                self->has_ip_ = true;
                self->notify_state(self->state_);
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
            // A queued STA event can outlive a stop/start reconfiguration to AP.
            if (self->config_.mode == WifiMode::ApOnly) return;
            auto* dis_event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
            self->wifi_diagnostics_.disconnect_events++;
            if (dis_event) {
                self->last_disconnect_reason_.store(dis_event->reason, std::memory_order_relaxed);
                self->wifi_diagnostics_.last_disconnect_reason = dis_event->reason;
            }
            self->cancel_fallback_exit_timer();

            // Check if this was an administrative disconnect (e.g. reconfigure / stop)
            if (self->intentional_disconnect_.load(std::memory_order_relaxed)) {
                ESP_LOGI(kTag, "Intentional disconnect; skipping automatic reconnect policy");
                self->has_ip_ = false;
                self->state_ = WifiState::Idle;
                return;
            }

            // If we are in fallback AP mode, don't spin in retry loops; use bounded backoff timer
            if (self->is_fallback_ap_.load(std::memory_order_relaxed)) {
                self->state_ = WifiState::FallbackAp;
                self->has_ip_ = true; // The recovery AP is still usable.
                self->notify_state(WifiState::FallbackAp);
                self->schedule_fallback_retry();
                return;
            }

            self->has_ip_ = false;

            if (self->retries_ < self->config_.max_retry) {
                ++self->retries_;
                ESP_LOGI(kTag, "STA disconnected (reason: %u). Retrying (%u/%u)...",
                         self->last_disconnect_reason_.load(), self->retries_.load(), self->config_.max_retry);
                self->wifi_diagnostics_.reconnect_attempts++;
                self->state_ = WifiState::Connecting;
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) {
                    self->wifi_diagnostics_.reconnect_failures++;
                    self->wifi_diagnostics_.last_error = err;
                    self->handle_connect_failure();
                    return;
                }
            } else if (self->config_.mode == WifiMode::StaWithApFallback) {
                self->handle_connect_failure();
                return;
            } else {
                self->state_ = WifiState::Failed;
            }

            self->notify_state(self->state_);
            return;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (self->config_.mode == WifiMode::ApOnly) return;
        self->retries_ = 0;
        self->cancel_fallback_timer();
        self->fallback_backoff_index_ = 0;
        self->has_ip_ = true;
        self->state_ = WifiState::Connected;
        ESP_LOGI(kTag, "STA connected and acquired IP address");

        if (self->is_fallback_ap_.load(std::memory_order_acquire)) {
            self->schedule_exit_fallback_ap();
        } else {
            self->is_fallback_ap_.store(false, std::memory_order_release);
        }

        self->notify_state(self->state_);
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
    if (in_callback()) return ESP_ERR_INVALID_STATE;
    LifecycleGuard lifecycle(lifecycle_busy_);
    if (!lifecycle) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(quiesce(), kTag, "Wi-Fi stop quiescence");
    esp_err_t err = wifi_init_done_ ? esp_wifi_stop() : ESP_OK;
    if (err != ESP_OK) {
        wifi_diagnostics_.last_error = err;
        ESP_LOGE(kTag, "esp_wifi_stop failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(drain_event_queue(), kTag, "Wi-Fi stop event barrier");
    state_ = WifiState::Idle;
    has_ip_ = false;
    callback_ = nullptr;
    retries_ = 0;
    is_fallback_ap_.store(false, std::memory_order_relaxed);
    fallback_exit_pending_.store(false, std::memory_order_relaxed);
    return err;
}

WifiDiagnostics WifiService::diagnostics() const {
    WifiDiagnostics diag = wifi_diagnostics_.snapshot();
    wifi_mode_t m = WIFI_MODE_NULL;
    if (wifi_init_done_ && esp_wifi_get_mode(&m) == ESP_OK) {
        diag.actual_mode = m;
    }
    return diag;
}

WifiRuntimeStatus WifiService::status() const {
    Activity activity(*this);
    WifiRuntimeStatus st{};
    st.state = state_;
    st.mode = configured_mode_;
    st.retry_count = retries_;
    st.last_disconnect_reason = last_disconnect_reason_.load(std::memory_order_relaxed);
    st.is_fallback_ap = is_fallback_ap_.load(std::memory_order_relaxed);
    st.sta_connected = (st.state == WifiState::Connected);
    st.ap_active = (st.state == WifiState::ApActive || st.is_fallback_ap);

    if (!activity) return st;

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
