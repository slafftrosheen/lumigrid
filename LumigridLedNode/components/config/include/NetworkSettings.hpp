// components/config/include/NetworkSettings.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "esp_rom_crc.h"

namespace lg {

enum class NetworkMode : uint8_t {
    ApOnly = 0,
    StaOnly = 1,
    StaWithApFallback = 2,
};

inline const char* network_mode_to_string(NetworkMode mode) {
    switch (mode) {
        case NetworkMode::ApOnly: return "ap_only";
        case NetworkMode::StaOnly: return "sta_only";
        case NetworkMode::StaWithApFallback: return "sta_with_ap_fallback";
        default: return "ap_only";
    }
}

inline NetworkMode string_to_network_mode(const char* str) {
    if (!str) return NetworkMode::ApOnly;
    if (std::strcmp(str, "sta_only") == 0) return NetworkMode::StaOnly;
    if (std::strcmp(str, "sta_with_ap_fallback") == 0) return NetworkMode::StaWithApFallback;
    return NetworkMode::ApOnly;
}

inline bool parse_network_mode(const char* str, NetworkMode& out_mode) {
    if (!str) return false;
    if (std::strcmp(str, "ap_only") == 0) { out_mode = NetworkMode::ApOnly; return true; }
    if (std::strcmp(str, "sta_only") == 0) { out_mode = NetworkMode::StaOnly; return true; }
    if (std::strcmp(str, "sta_with_ap_fallback") == 0) { out_mode = NetworkMode::StaWithApFallback; return true; }
    return false;
}

struct NetworkSettings {
    uint16_t schema_version = 1;

    NetworkMode mode = NetworkMode::ApOnly;

    char sta_ssid[33] = {};
    char sta_password[65] = {};

    bool ap_enabled = true;
    char ap_name[33] = {};
    uint8_t ap_channel = 1;

    uint32_t checksum = 0;
};

inline uint32_t calc_network_settings_crc(const NetworkSettings& settings) {
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&settings),
                            sizeof(NetworkSettings) - sizeof(uint32_t));
}

} // namespace lg
