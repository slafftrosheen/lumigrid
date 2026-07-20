// components/network/include/WifiService.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"
#include "esp_event.h"

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
    std::string ap_password = "lumigrid123";
    uint8_t max_retry = 10;
};

enum class WifiState : uint8_t {
    Idle,
    Connecting,
    Connected,
    ApActive,
    Failed
};

using WifiStateCallback = std::function<void(WifiState)>;

class WifiService {
public:
    esp_err_t start(const WifiConfig& config, WifiStateCallback callback);
    WifiState state() const;
    bool has_ip() const;

private:
    static void event_handler(
        void* arg,
        esp_event_base_t event_base,
        int32_t event_id,
        void* event_data
    );

    esp_err_t start_sta();
    esp_err_t start_ap();

    WifiConfig config_{};
    WifiStateCallback callback_{};
    WifiState state_ = WifiState::Idle;
    uint8_t retries_ = 0;
    bool has_ip_ = false;
};

} // namespace lg
