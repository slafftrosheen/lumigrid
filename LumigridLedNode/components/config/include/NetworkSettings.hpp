// components/config/include/NetworkSettings.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include "esp_rom_crc.h"
#include "esp_random.h"

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

struct NetworkSettingsV1 {
    uint16_t schema_version = 1;
    NetworkMode mode = NetworkMode::ApOnly;
    char sta_ssid[33] = {};
    char sta_password[65] = {};
    char ap_name[33] = {};
    uint8_t ap_channel = 1;
    uint32_t checksum = 0;
};

inline uint32_t calc_network_settings_v1_crc(const NetworkSettingsV1& s) {
    uint32_t crc = 0;
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&s.schema_version), sizeof(s.schema_version));
    const uint8_t mode = static_cast<uint8_t>(s.mode);
    crc = esp_rom_crc32_le(crc, &mode, sizeof(mode));
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.sta_ssid), sizeof(s.sta_ssid));
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.sta_password), sizeof(s.sta_password));
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(s.ap_name), sizeof(s.ap_name));
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&s.ap_channel), sizeof(s.ap_channel));
    return crc;
}

struct NetworkSettings {
    uint16_t schema_version = 2;

    NetworkMode mode = NetworkMode::ApOnly;

    char sta_ssid[33] = {};
    char sta_password[65] = {};

    char ap_name[33] = {};
    char ap_password[65] = {};
    uint8_t ap_channel = 1;

    uint32_t checksum = 0;

    bool is_ap_enabled() const {
        return mode != NetworkMode::StaOnly;
    }
};

/**
 * Deterministic CRC32 computation over explicit fields in a defined order.
 * Does not include compiler padding or the checksum field itself.
 */
inline uint32_t calc_network_settings_crc(const NetworkSettings& s) {
    uint32_t crc = 0;

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(&s.schema_version),
        sizeof(s.schema_version)
    );

    const uint8_t mode = static_cast<uint8_t>(s.mode);
    crc = esp_rom_crc32_le(crc, &mode, sizeof(mode));

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(s.sta_ssid),
        sizeof(s.sta_ssid)
    );

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(s.sta_password),
        sizeof(s.sta_password)
    );

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(s.ap_name),
        sizeof(s.ap_name)
    );

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(s.ap_password),
        sizeof(s.ap_password)
    );

    crc = esp_rom_crc32_le(
        crc,
        reinterpret_cast<const uint8_t*>(&s.ap_channel),
        sizeof(s.ap_channel)
    );

    return crc;
}

/**
 * Generates a cryptographically strong random printable URL-safe SoftAP password.
 * Never derived from MAC, serial, node ID, or chip ID.
 */
inline void generate_random_ap_password(char* out, size_t max_len) {
    if (!out || max_len < 17) return;
    static constexpr char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr size_t charset_len = sizeof(charset) - 1;
    constexpr size_t key_len = 20; // 20 printable characters
    size_t target_len = (max_len - 1 < key_len) ? (max_len - 1) : key_len;
    uint8_t rand_buf[32];
    esp_fill_random(rand_buf, sizeof(rand_buf));
    for (size_t i = 0; i < target_len; ++i) {
        out[i] = charset[rand_buf[i] % charset_len];
    }
    out[target_len] = '\0';
}

enum class NetworkApplyState : uint8_t {
    Idle = 0,
    Applying,
    WaitingForNetwork,
    RollingBack,
    Failed
};

inline const char* network_apply_state_to_string(NetworkApplyState s) {
    switch (s) {
        case NetworkApplyState::Idle: return "idle";
        case NetworkApplyState::Applying: return "applying";
        case NetworkApplyState::WaitingForNetwork: return "waiting_for_network";
        case NetworkApplyState::RollingBack: return "rolling_back";
        case NetworkApplyState::Failed: return "failed";
        default: return "idle";
    }
}

struct NetworkApplyRequest {
    NetworkSettings previous{};
    NetworkSettings desired{};
    uint32_t command_id = 0;
};

} // namespace lg
