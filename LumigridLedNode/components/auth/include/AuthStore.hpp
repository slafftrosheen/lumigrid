// components/auth/include/AuthStore.hpp
#pragma once

#include "AuthTypes.hpp"
#include "esp_err.h"

namespace lg::auth {

class AuthStore {
public:
    AuthStore() = default;

    esp_err_t init();
    
    // Load persisted AuthConfig. Returns ESP_OK if valid CRC and schema.
    esp_err_t load(AuthConfig& out_cfg);

    // Save AuthConfig to NVS with updated CRC.
    esp_err_t save(const AuthConfig& cfg);

    // Erase auth NVS (e.g. on factory reset)
    esp_err_t erase();

    bool is_provisioned() const { return config_.provisioned; }
    const AuthConfig& config() const { return config_; }

private:
    AuthConfig config_{};
    bool initialized_ = false;
};

} // namespace lg::auth
