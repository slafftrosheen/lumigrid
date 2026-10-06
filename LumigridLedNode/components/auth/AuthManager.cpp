// components/auth/AuthManager.cpp
#include "AuthManager.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lg::auth {
namespace {

static const char* TAG = "auth_manager";

void bytes_to_hex(const uint8_t* src, size_t len, char* dst) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        dst[i * 2]     = hex_chars[(src[i] >> 4) & 0x0F];
        dst[i * 2 + 1] = hex_chars[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

} // namespace

AuthManager::AuthManager(AuthStore* store)
    : store_(store ? store : &default_store_) {
    mutex_ = xSemaphoreCreateMutex();
}

AuthManager::~AuthManager() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

esp_err_t AuthManager::init() {
    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
    }
    return store_->init();
}

bool AuthManager::is_provisioned() const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    bool prov = store_->is_provisioned();
    if (mutex_) xSemaphoreGive(mutex_);
    return prov;
}

bool AuthManager::verify_factory_setup_credential_locked(const char* credential) const {
    return credential && !store_->is_provisioned() &&
        commissioning_state_ == CommissioningState::FactoryFirstBoot &&
        std::strcmp(credential, kFactorySetupCredential) == 0;
}

bool AuthManager::verify_factory_setup_credential(const char* credential) const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    bool valid = verify_factory_setup_credential_locked(credential);
    if (mutex_) xSemaphoreGive(mutex_);
    return valid;
}

esp_err_t AuthManager::bootstrap(const char* admin_token, char* out_session_hex, uint32_t* out_expires_in) {
    if (!admin_token) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = std::strlen(admin_token);
    if (len < 16 || len > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    if (store_->is_provisioned() ||
        (commissioning_state_ != CommissioningState::FactoryFirstBoot &&
         commissioning_state_ != CommissioningState::PhysicalRecovery)) {
        if (mutex_) xSemaphoreGive(mutex_);
        ESP_LOGW(TAG, "auth_bootstrap_rejected: node already provisioned");
        return ESP_ERR_INVALID_STATE;
    }

    AuthConfig cfg = store_->config();
    cfg.schema_version = 1;
    cfg.provisioned = true;

    cfg.admin.enabled = true;
    esp_fill_random(cfg.admin.salt, sizeof(cfg.admin.salt));
    compute_credential_digest(admin_token, cfg.admin.salt, cfg.admin.digest);

    cfg.installer.enabled = false;
    cfg.op.enabled = false;

    esp_err_t err = store_->save(cfg);
    if (err == ESP_OK) {
        commissioning_state_ = CommissioningState::None;
        // Revoke every temporary session before issuing permanent Admin authority.
        for (auto& session : sessions_) session.active = false;
        for (auto& ticket : ws_tickets_) ticket.active = false;
        failed_attempts_ = 0;
        lockout_until_us_ = 0;
        if (out_session_hex && out_expires_in) {
            auto& session = sessions_[0];
            session = AuthSession{};
            session.active = true;
            session.role = AuthRole::Admin;
            esp_fill_random(session.token, sizeof(session.token));
            bytes_to_hex(session.token, sizeof(session.token), session.token_hex);
            session.created_us = session.last_seen_us = esp_timer_get_time();
            std::strcpy(out_session_hex, session.token_hex);
            *out_expires_in = kIdleTimeoutUs / 1000000ULL;
        }
        ESP_LOGI(TAG, "auth_bootstrap_completed: Admin credential provisioned successfully");
    } else {
        ESP_LOGE(TAG, "auth_bootstrap_failed: unable to save auth config: %s", esp_err_to_name(err));
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return err;
}

bool AuthManager::verify_credential_locked(const char* credential, AuthRole* out_role) {
    if (!credential || credential[0] == '\0' || !store_->is_provisioned() ||
        std::strcmp(credential, kFactorySetupCredential) == 0) {
        return false;
    }

    const AuthConfig& cfg = store_->config();
    uint8_t cand_digest[32];

    // 1. Check Admin
    if (cfg.admin.enabled) {
        compute_credential_digest(credential, cfg.admin.salt, cand_digest);
        if (constant_time_memcmp(cand_digest, cfg.admin.digest, sizeof(cand_digest))) {
            if (out_role) *out_role = AuthRole::Admin;
            return true;
        }
    }

    // 2. Check Installer
    if (cfg.installer.enabled) {
        compute_credential_digest(credential, cfg.installer.salt, cand_digest);
        if (constant_time_memcmp(cand_digest, cfg.installer.digest, sizeof(cand_digest))) {
            if (out_role) *out_role = AuthRole::Installer;
            return true;
        }
    }

    // 3. Check Operator
    if (cfg.op.enabled) {
        compute_credential_digest(credential, cfg.op.salt, cand_digest);
        if (constant_time_memcmp(cand_digest, cfg.op.digest, sizeof(cand_digest))) {
            if (out_role) *out_role = AuthRole::Operator;
            return true;
        }
    }

    return false;
}

bool AuthManager::is_rate_limited() const {
    if (lockout_until_us_ == 0) return false;
    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    return now_us < lockout_until_us_;
}

void AuthManager::record_failed_attempt() {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    failed_attempts_++;
    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (failed_attempts_ >= 5) {
        lockout_until_us_ = now_us + 30ULL * 1000000ULL; // 30 second lockout
        ESP_LOGW(TAG, "Rate limiting activated: 5 failed attempts reached, locking for 30s");
    }
    if (mutex_) xSemaphoreGive(mutex_);
}

void AuthManager::record_successful_attempt() {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    failed_attempts_ = 0;
    lockout_until_us_ = 0;
    if (mutex_) xSemaphoreGive(mutex_);
}

void AuthManager::cleanup_expired_sessions_locked(uint64_t now_us) {
    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (!sessions_[i].active) continue;
        if ((now_us - sessions_[i].created_us > kAbsoluteTimeoutUs) ||
            (now_us - sessions_[i].last_seen_us > kIdleTimeoutUs)) {
            sessions_[i].active = false;
            ESP_LOGI(TAG, "auth_session_expired for role=%d", static_cast<int>(sessions_[i].role));
        }
    }
}

void AuthManager::cleanup_expired_tickets_locked(uint64_t now_us) {
    for (size_t i = 0; i < kMaxWsTickets; ++i) {
        if (!ws_tickets_[i].active) continue;
        if (now_us - ws_tickets_[i].created_us > kWsTicketTimeoutUs) {
            ws_tickets_[i].active = false;
        }
    }
}

esp_err_t AuthManager::create_session(const char* credential, char out_session_hex[65], AuthRole* out_role, uint32_t* out_expires_in) {
    if (!credential || !out_session_hex || !out_role || !out_expires_in) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    if (now_us < lockout_until_us_) {
        if (mutex_) xSemaphoreGive(mutex_);
        return ESP_ERR_TIMEOUT;
    }

    cleanup_expired_sessions_locked(now_us);

    AuthRole derived_role = AuthRole::Operator;
    bool factory_setup = verify_factory_setup_credential_locked(credential);
    if (factory_setup) derived_role = AuthRole::Admin;
    if (!factory_setup && !verify_credential_locked(credential, &derived_role)) {
        if (mutex_) xSemaphoreGive(mutex_);
        ESP_LOGW(TAG, "auth_login_failed");
        return ESP_ERR_NOT_FOUND;
    }

    // Find slot (free or oldest active)
    int target_idx = -1;
    uint64_t oldest_seen = UINT64_MAX;
    int oldest_idx = -1;

    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (!sessions_[i].active) {
            target_idx = static_cast<int>(i);
            break;
        }
        if (sessions_[i].last_seen_us < oldest_seen) {
            oldest_seen = sessions_[i].last_seen_us;
            oldest_idx = static_cast<int>(i);
        }
    }

    if (target_idx < 0) {
        target_idx = oldest_idx; // Evict oldest session if table full
    }

    AuthSession& s = sessions_[target_idx];
    s.active = true;
    s.factory_setup = factory_setup;
    s.role = derived_role;
    esp_fill_random(s.token, sizeof(s.token));
    bytes_to_hex(s.token, sizeof(s.token), s.token_hex);
    s.created_us = now_us;
    s.last_seen_us = now_us;

    std::strncpy(out_session_hex, s.token_hex, 65);
    *out_role = derived_role;
    *out_expires_in = static_cast<uint32_t>(kIdleTimeoutUs / 1000000ULL);

    ESP_LOGI(TAG, "auth_login_success: new session created for role=%d", static_cast<int>(derived_role));

    if (mutex_) xSemaphoreGive(mutex_);
    return ESP_OK;
}

SessionValidationResult AuthManager::validate_session(const char* session_hex) {
    SessionValidationResult res{};
    if (!session_hex || std::strlen(session_hex) != 64) {
        return res;
    }

    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    cleanup_expired_sessions_locked(now_us);

    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (sessions_[i].active && std::strcmp(sessions_[i].token_hex, session_hex) == 0) {
            if (sessions_[i].factory_setup && !verify_factory_setup_credential_locked(kFactorySetupCredential)) {
                sessions_[i].active = false;
                break;
            }
            if ((now_us - sessions_[i].created_us > kAbsoluteTimeoutUs) ||
                (now_us - sessions_[i].last_seen_us > kIdleTimeoutUs)) {
                sessions_[i].active = false;
                res.expired = true;
                res.valid = false;
                break;
            }

            // Refresh last seen
            sessions_[i].last_seen_us = now_us;
            res.valid = true;
            res.factory_setup = sessions_[i].factory_setup;
            res.role = sessions_[i].role;

            uint64_t idle_rem = (kIdleTimeoutUs > (now_us - sessions_[i].last_seen_us))
                                ? (kIdleTimeoutUs - (now_us - sessions_[i].last_seen_us)) : 0;
            uint64_t abs_rem  = (kAbsoluteTimeoutUs > (now_us - sessions_[i].created_us))
                                ? (kAbsoluteTimeoutUs - (now_us - sessions_[i].created_us)) : 0;
            res.remaining_s = static_cast<uint32_t>(std::min(idle_rem, abs_rem) / 1000000ULL);
            break;
        }
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return res;
}

SessionValidationResult AuthManager::inspect_session(const char* session_hex) {
    SessionValidationResult res{};
    if (!session_hex || std::strlen(session_hex) != 64) {
        return res;
    }

    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (sessions_[i].active && std::strcmp(sessions_[i].token_hex, session_hex) == 0) {
            if (sessions_[i].factory_setup && !verify_factory_setup_credential_locked(kFactorySetupCredential)) break;
            if ((now_us - sessions_[i].created_us > kAbsoluteTimeoutUs) ||
                (now_us - sessions_[i].last_seen_us > kIdleTimeoutUs)) {
                res.expired = true;
                res.valid = false;
            } else {
                res.valid = true;
                res.factory_setup = sessions_[i].factory_setup;
                res.role = sessions_[i].role;
                uint64_t idle_rem = (kIdleTimeoutUs > (now_us - sessions_[i].last_seen_us))
                                    ? (kIdleTimeoutUs - (now_us - sessions_[i].last_seen_us)) : 0;
                uint64_t abs_rem  = (kAbsoluteTimeoutUs > (now_us - sessions_[i].created_us))
                                    ? (kAbsoluteTimeoutUs - (now_us - sessions_[i].created_us)) : 0;
                res.remaining_s = static_cast<uint32_t>(std::min(idle_rem, abs_rem) / 1000000ULL);
            }
            break;
        }
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return res;
}

esp_err_t AuthManager::delete_session(const char* session_hex) {
    if (!session_hex) return ESP_ERR_INVALID_ARG;

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    bool found = false;
    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (sessions_[i].active && std::strcmp(sessions_[i].token_hex, session_hex) == 0) {
            sessions_[i].active = false;
            found = true;
            ESP_LOGI(TAG, "auth_logout: session invalidated for role=%d", static_cast<int>(sessions_[i].role));
            break;
        }
    }
    if (mutex_) xSemaphoreGive(mutex_);

    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void AuthManager::invalidate_sessions(AuthRole role) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (!sessions_[i].active) continue;
        if (role == AuthRole::Admin || sessions_[i].role == role) {
            sessions_[i].active = false;
        }
    }
    if (mutex_) xSemaphoreGive(mutex_);
}

bool AuthManager::verify_api_key(const char* api_key, AuthRole* out_role) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    bool ok = verify_credential_locked(api_key, out_role);
    if (mutex_) xSemaphoreGive(mutex_);
    return ok;
}

esp_err_t AuthManager::create_ws_ticket(AuthRole role, char out_ticket_hex[33], uint32_t* out_expires_in_ms) {
    if (!out_ticket_hex || !out_expires_in_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    cleanup_expired_tickets_locked(now_us);

    int target_idx = -1;
    for (size_t i = 0; i < kMaxWsTickets; ++i) {
        if (!ws_tickets_[i].active) {
            target_idx = static_cast<int>(i);
            break;
        }
    }
    if (target_idx < 0) {
        target_idx = 0; // Evict oldest slot
    }

    uint8_t rand_bytes[16];
    esp_fill_random(rand_bytes, sizeof(rand_bytes));

    WsTicket& t = ws_tickets_[target_idx];
    t.active = true;
    t.role = role;
    bytes_to_hex(rand_bytes, sizeof(rand_bytes), t.ticket_hex);
    t.created_us = now_us;

    std::strncpy(out_ticket_hex, t.ticket_hex, 33);
    *out_expires_in_ms = static_cast<uint32_t>(kWsTicketTimeoutUs / 1000ULL);

    if (mutex_) xSemaphoreGive(mutex_);
    return ESP_OK;
}

bool AuthManager::consume_ws_ticket(const char* ticket_hex, AuthRole* out_role) {
    if (!ticket_hex || std::strlen(ticket_hex) != 32) {
        return false;
    }

    uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    cleanup_expired_tickets_locked(now_us);

    bool ok = false;
    for (size_t i = 0; i < kMaxWsTickets; ++i) {
        if (ws_tickets_[i].active && std::strcmp(ws_tickets_[i].ticket_hex, ticket_hex) == 0) {
            if (now_us - ws_tickets_[i].created_us <= kWsTicketTimeoutUs) {
                ok = true;
                if (out_role) *out_role = ws_tickets_[i].role;
            }
            // Ticket is single-use; consume immediately regardless of expiry
            ws_tickets_[i].active = false;
            break;
        }
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return ok;
}

esp_err_t AuthManager::rotate_credential(AuthRole role, const char* new_token) {
    if (!new_token) return ESP_ERR_INVALID_ARG;
    size_t len = std::strlen(new_token);
    if (len < 16 || len > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    if (!store_->is_provisioned()) {
        if (mutex_) xSemaphoreGive(mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    AuthConfig cfg = store_->config();
    CredentialRecord* target_cred = nullptr;

    switch (role) {
        case AuthRole::Admin: target_cred = &cfg.admin; break;
        case AuthRole::Installer: target_cred = &cfg.installer; break;
        case AuthRole::Operator: target_cred = &cfg.op; break;
    }

    target_cred->enabled = true;
    esp_fill_random(target_cred->salt, sizeof(target_cred->salt));
    compute_credential_digest(new_token, target_cred->salt, target_cred->digest);

    esp_err_t err = store_->save(cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "auth_key_rotated for role=%d", static_cast<int>(role));
    }

    if (mutex_) xSemaphoreGive(mutex_);

    if (err == ESP_OK) {
        invalidate_sessions(role);
    }

    return err;
}

esp_err_t AuthManager::disable_credential(AuthRole role) {
    if (role == AuthRole::Admin) {
        ESP_LOGW(TAG, "Cannot disable Admin credential");
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    AuthConfig cfg = store_->config();
    if (role == AuthRole::Installer) {
        cfg.installer.enabled = false;
    } else if (role == AuthRole::Operator) {
        cfg.op.enabled = false;
    }

    esp_err_t err = store_->save(cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "auth_role_disabled for role=%d", static_cast<int>(role));
    }

    if (mutex_) xSemaphoreGive(mutex_);

    if (err == ESP_OK) {
        invalidate_sessions(role);
    }

    return err;
}

esp_err_t AuthManager::migrate_legacy_tokens(const char* legacy_admin, const char* legacy_installer, const char* legacy_op) {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);

    if (store_->is_provisioned()) {
        if (mutex_) xSemaphoreGive(mutex_);
        return ESP_OK;
    }

    AuthConfig cfg = store_->config();
    if (cfg.provisioned) {
        if (mutex_) xSemaphoreGive(mutex_);
        return ESP_OK;
    }

    // Check Case A: legacy config contains universal defaults
    bool is_universal_admin = legacy_admin &&
        (std::strcmp(legacy_admin, "lumigrid_admin") == 0 || std::strcmp(legacy_admin, kFactorySetupCredential) == 0);
    bool is_universal_installer = legacy_installer &&
        (std::strcmp(legacy_installer, "lumigrid_install") == 0 || std::strcmp(legacy_installer, kFactorySetupCredential) == 0);
    bool is_universal_op = legacy_op &&
        (std::strcmp(legacy_op, "lumigrid_op") == 0 || std::strcmp(legacy_op, kFactorySetupCredential) == 0);

    if (is_universal_admin && is_universal_installer && is_universal_op) {
        // Case A: treat as unprovisioned!
        cfg.provisioned = false;
        cfg.admin.enabled = false;
        cfg.installer.enabled = false;
        cfg.op.enabled = false;
        store_->save(cfg);
        ESP_LOGI(TAG, "Migration: legacy universal credentials detected. Node initialized as UNPROVISIONED.");
        if (mutex_) xSemaphoreGive(mutex_);
        return ESP_OK;
    }

    // Case B: genuinely customized credentials!
    bool any_custom = false;
    if (legacy_admin && legacy_admin[0] != '\0' && !is_universal_admin) {
        esp_fill_random(cfg.admin.salt, sizeof(cfg.admin.salt));
        compute_credential_digest(legacy_admin, cfg.admin.salt, cfg.admin.digest);
        cfg.admin.enabled = true;
        any_custom = true;
    }
    if (legacy_installer && legacy_installer[0] != '\0' && !is_universal_installer) {
        esp_fill_random(cfg.installer.salt, sizeof(cfg.installer.salt));
        compute_credential_digest(legacy_installer, cfg.installer.salt, cfg.installer.digest);
        cfg.installer.enabled = true;
        any_custom = true;
    }
    if (legacy_op && legacy_op[0] != '\0' && !is_universal_op) {
        esp_fill_random(cfg.op.salt, sizeof(cfg.op.salt));
        compute_credential_digest(legacy_op, cfg.op.salt, cfg.op.digest);
        cfg.op.enabled = true;
        any_custom = true;
    }

    if (any_custom) {
        cfg.provisioned = true;
        store_->save(cfg);
        ESP_LOGI(TAG, "Migration: customized legacy credentials migrated to AuthStore.");
    } else {
        cfg.provisioned = false;
        store_->save(cfg);
    }

    if (mutex_) xSemaphoreGive(mutex_);
    return ESP_OK;
}

void AuthManager::get_credential_status(bool& admin_enabled, bool& installer_enabled, bool& op_enabled) const {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    const AuthConfig& cfg = store_->config();
    admin_enabled = cfg.admin.enabled;
    installer_enabled = cfg.installer.enabled;
    op_enabled = cfg.op.enabled;
    if (mutex_) xSemaphoreGive(mutex_);
}

esp_err_t AuthManager::factory_reset() {
    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    for (size_t i = 0; i < kMaxSessions; ++i) {
        sessions_[i].active = false;
    }
    for (size_t i = 0; i < kMaxWsTickets; ++i) {
        ws_tickets_[i].active = false;
    }
    failed_attempts_ = 0;
    lockout_until_us_ = 0;
    esp_err_t err = store_->erase();
    if (err == ESP_OK) commissioning_state_ = CommissioningState::FactoryFirstBoot;
    if (mutex_) xSemaphoreGive(mutex_);
    return err;
}

} // namespace lg::auth
