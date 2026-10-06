// components/auth/include/AuthTypes.hpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_rom_crc.h"
#include "mbedtls/sha256.h"

namespace lg::auth {

enum class AuthRole : uint8_t {
    Operator = 0,
    Installer = 1,
    Admin = 2,
};

enum class CommissioningState : uint8_t {
    None = 0,
    FactoryFirstBoot = 1,
    PhysicalRecovery = 2,
};

enum class AuthMethod : uint8_t {
    None = 0,
    Session = 1,
    ApiKey = 2,
};

struct CredentialRecord {
    bool enabled = false;
    uint8_t salt[16] = {};
    uint8_t digest[32] = {};
};

struct AuthConfig {
    uint16_t schema_version = 1;
    bool provisioned = false;

    CredentialRecord admin{};
    CredentialRecord installer{};
    CredentialRecord op{};

    uint32_t checksum = 0;
};

struct AuthSession {
    bool active = false;
    bool factory_setup = false;
    AuthRole role = AuthRole::Operator;
    uint8_t token[32] = {};
    char token_hex[65] = {};
    uint64_t created_us = 0;
    uint64_t last_seen_us = 0;
};

struct WsTicket {
    bool active = false;
    AuthRole role = AuthRole::Operator;
    char ticket_hex[33] = {};
    uint64_t created_us = 0;
};

inline uint32_t calc_auth_config_crc(const AuthConfig& c) {
    uint32_t crc = 0;
    crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&c.schema_version), sizeof(c.schema_version));
    const uint8_t prov = c.provisioned ? 1 : 0;
    crc = esp_rom_crc32_le(crc, &prov, sizeof(prov));

    const uint8_t a_en = c.admin.enabled ? 1 : 0;
    crc = esp_rom_crc32_le(crc, &a_en, sizeof(a_en));
    crc = esp_rom_crc32_le(crc, c.admin.salt, sizeof(c.admin.salt));
    crc = esp_rom_crc32_le(crc, c.admin.digest, sizeof(c.admin.digest));

    const uint8_t i_en = c.installer.enabled ? 1 : 0;
    crc = esp_rom_crc32_le(crc, &i_en, sizeof(i_en));
    crc = esp_rom_crc32_le(crc, c.installer.salt, sizeof(c.installer.salt));
    crc = esp_rom_crc32_le(crc, c.installer.digest, sizeof(c.installer.digest));

    const uint8_t o_en = c.op.enabled ? 1 : 0;
    crc = esp_rom_crc32_le(crc, &o_en, sizeof(o_en));
    crc = esp_rom_crc32_le(crc, c.op.salt, sizeof(c.op.salt));
    crc = esp_rom_crc32_le(crc, c.op.digest, sizeof(c.op.digest));

    return crc;
}

inline bool constant_time_memcmp(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (a[i] ^ b[i]);
    }
    return diff == 0;
}

inline void compute_credential_digest(const char* token, const uint8_t salt[16], uint8_t digest_out[32]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
    mbedtls_sha256_update(&ctx, salt, 16);
    if (token) {
        mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(token), std::strlen(token));
    }
    mbedtls_sha256_finish(&ctx, digest_out);
    mbedtls_sha256_free(&ctx);
}

} // namespace lg::auth
