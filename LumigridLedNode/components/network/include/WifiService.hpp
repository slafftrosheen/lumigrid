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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    // Lifecycle calls run outside the default event loop. On timeout, retain
    // anything captured by the callback until a later stop() succeeds.
    esp_err_t start(const WifiConfig& config, WifiStateCallback callback);
    esp_err_t stop();
    WifiState state() const;
    bool has_ip() const;
    WifiRuntimeStatus status() const;
    WifiDiagnostics diagnostics() const;

    void schedule_exit_fallback_ap();
    void perform_exit_fallback_ap();

    void set_intentional_disconnect(bool val) {
        intentional_disconnect_.store(val);
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
    // The runtime owns this service for the lifetime of the default event loop.
    // An Activity pins callback/configuration storage while stop/start closes
    // admission and waits for existing work. No lock spans an IDF call.
    class Activity {
    public:
        explicit Activity(const WifiService& service);
        ~Activity();
        explicit operator bool() const { return admitted_; }
        Activity(const Activity&) = delete;
        Activity& operator=(const Activity&) = delete;
    private:
        const WifiService& service_;
        bool admitted_ = false;
    };

    struct AtomicDiagnostics {
        std::atomic<uint32_t> start_attempts{0}, start_failures{0};
        std::atomic<uint32_t> reconnect_attempts{0}, reconnect_failures{0};
        std::atomic<uint32_t> fallback_activations{0};
        std::atomic<uint32_t> fallback_exit_attempts{0}, fallback_exit_failures{0};
        std::atomic<uint32_t> disconnect_events{0};
        std::atomic<uint8_t> last_disconnect_reason{0};
        std::atomic<esp_err_t> last_error{ESP_OK};
        WifiDiagnostics snapshot() const;
    };

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
    esp_err_t ensure_fallback_timers();
    esp_err_t quiesce();
    esp_err_t drain_event_queue();
    bool in_callback() const;
    void notify_state(WifiState state);
    void handle_connect_failure();
    void schedule_fallback_retry();
    void cancel_fallback_timer();
    void cancel_fallback_exit_timer();

    WifiConfig config_{};
    WifiStateCallback callback_{};
    std::atomic<WifiState> state_{WifiState::Idle};
    std::atomic<WifiMode> configured_mode_{WifiMode::StaWithApFallback};
    std::atomic<uint8_t> retries_{0};
    std::atomic<bool> has_ip_{false};
    std::atomic<bool> intentional_disconnect_{true};
    std::atomic<bool> lifecycle_busy_{false};
    mutable std::atomic<uint32_t> active_operations_{0};
    std::atomic<TaskHandle_t> callback_task_{nullptr};
    std::atomic<TaskHandle_t> event_task_{nullptr};
    uint32_t barrier_sequence_ = 0; // Serialized by lifecycle_busy_.
    std::atomic<uint32_t> barrier_acknowledged_{0};
    std::atomic<uint8_t> last_disconnect_reason_{0};
    std::atomic<bool> is_fallback_ap_{false};
    std::atomic<bool> fallback_exit_pending_{false};

    AtomicDiagnostics wifi_diagnostics_{};

    esp_timer_handle_t fallback_timer_ = nullptr;
    esp_timer_handle_t fallback_exit_timer_ = nullptr;
    uint8_t fallback_backoff_index_ = 0;

    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
    std::atomic<bool> wifi_init_done_{false};
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    esp_event_handler_instance_t service_event_instance_ = nullptr;
};

} // namespace lg
