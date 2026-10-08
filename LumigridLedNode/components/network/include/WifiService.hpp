// components/network/include/WifiService.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <atomic>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_types.h"
#include "esp_timer.h"

#include "esp_wifi_types.h"

namespace lg {

enum class WifiMode : uint8_t {
    StaOnly,
    ApOnly,
    StaWithApFallback
};

struct WifiConfig {
    WifiMode mode = WifiMode::StaWithApFallback;
    std::string sta_ssid;
    std::string sta_password;
    std::string ap_ssid = "LumiGrid-Factory";
    std::string ap_password; // Configured at runtime, non-universal
    uint8_t ap_channel = 1;
    uint8_t max_retry = 3;
};

enum class WifiState : uint8_t {
    Idle,
    Connecting,
    Connected,
    ApActive,
    FallbackAp,
    Failed
};

struct WifiDiagnostics {
    uint32_t start_attempts = 0;
    uint32_t start_failures = 0;

    uint32_t reconnect_attempts = 0;
    uint32_t reconnect_failures = 0;

    uint32_t fallback_activations = 0;
    uint32_t fallback_exit_attempts = 0;
    uint32_t fallback_exit_failures = 0;

    uint32_t disconnect_events = 0;

    uint8_t last_disconnect_reason = 0;
    esp_err_t last_error = ESP_OK;

    wifi_mode_t actual_mode = WIFI_MODE_NULL;
};

struct WifiRuntimeStatus {
    WifiState state = WifiState::Idle;
    WifiMode mode = WifiMode::ApOnly;

    bool sta_connected = false;
    bool ap_active = false;
    bool is_fallback_ap = false;

    char sta_ssid[33] = {};
    int8_t sta_rssi = 0;

    esp_ip4_addr_t sta_ip{};
    esp_ip4_addr_t ap_ip{};

    uint8_t retry_count = 0;
    uint8_t last_disconnect_reason = 0;
};

using WifiStateCallback = std::function<void(WifiState)>;

class WifiService {
public:
    esp_err_t start(const WifiConfig& config, WifiStateCallback callback);
    esp_err_t stop();
    WifiState state() const;
    bool has_ip() const;
    WifiRuntimeStatus status() const;
    WifiDiagnostics diagnostics() const;

    void schedule_exit_fallback_ap();
    void perform_exit_fallback_ap();

    void set_intentional_disconnect(bool val) {
        intentional_disconnect_.store(val, std::memory_order_relaxed);
    }
    bool is_intentional_disconnect() const {
        return intentional_disconnect_.load(std::memory_order_relaxed);
    }
    uint8_t last_disconnect_reason() const {
        return last_disconnect_reason_.load(std::memory_order_relaxed);
    }
    bool is_fallback_ap() const {
        return is_fallback_ap_.load(std::memory_order_relaxed);
    }

private:
    static void event_handler(
        void* arg,
        esp_event_base_t event_base,
        int32_t event_id,
        void* event_data
    );

    static void on_fallback_timer(void* arg);
    static void on_fallback_exit_timer(void* arg);

    esp_err_t start_sta();
    esp_err_t start_ap();
    esp_err_t start_fallback_ap();
    void schedule_fallback_retry();
    void cancel_fallback_timer();
    void cancel_fallback_exit_timer();

    WifiConfig config_{};
    WifiStateCallback callback_{};
    std::atomic<WifiState> state_{WifiState::Idle};
    uint8_t retries_ = 0;
    std::atomic<bool> has_ip_{false};
    std::atomic<bool> intentional_disconnect_{false};
    std::atomic<uint8_t> last_disconnect_reason_{0};
    std::atomic<bool> is_fallback_ap_{false};
    std::atomic<bool> fallback_exit_pending_{false};

    WifiDiagnostics wifi_diagnostics_{};

    esp_timer_handle_t fallback_timer_ = nullptr;
    esp_timer_handle_t fallback_exit_timer_ = nullptr;
    uint8_t fallback_backoff_index_ = 0;

    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
    bool wifi_init_done_ = false;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
};

} // namespace lg
