// components/auth/include/AuthManager.hpp
#pragma once

#include "AuthTypes.hpp"
#include "AuthStore.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <cstddef>

namespace lg::auth {

struct SessionValidationResult {
    bool valid = false;
    bool expired = false;
    AuthRole role = AuthRole::Operator;
    uint32_t remaining_s = 0;
};

class AuthManager {
public:
    static constexpr size_t kMaxSessions = 8;
    static constexpr size_t kMaxWsTickets = 8;
    static constexpr uint64_t kIdleTimeoutUs = 30ULL * 60ULL * 1000000ULL;       // 30 mins
    static constexpr uint64_t kAbsoluteTimeoutUs = 8ULL * 3600ULL * 1000000ULL;    // 8 hours
    static constexpr uint64_t kWsTicketTimeoutUs = 15ULL * 1000000ULL;           // 15 seconds

    explicit AuthManager(AuthStore* store = nullptr);
    ~AuthManager();

    esp_err_t init();

    // Check if initial admin has been provisioned
    bool is_provisioned() const;

    // Commissioning state ownership (Section 0)
    void set_commissioning_state(CommissioningState state) { commissioning_state_ = state; }
    CommissioningState commissioning_state() const { return commissioning_state_; }
    bool is_bootstrap_allowed() const {
        return !is_provisioned() && (commissioning_state_ == CommissioningState::FactoryFirstBoot || commissioning_state_ == CommissioningState::PhysicalRecovery);
    }

    // Bootstrap initial admin credential (allowed only once when !provisioned && is_bootstrap_allowed())
    esp_err_t bootstrap(const char* admin_token);

    // Authenticate a long-lived credential and open a new session
    // Returns ESP_OK on success, populating out_session_hex, out_role, out_expires_in
    esp_err_t create_session(const char* credential, char out_session_hex[65], AuthRole* out_role, uint32_t* out_expires_in);

    // Validate a session token; refreshes last_seen_us if valid
    SessionValidationResult validate_session(const char* session_hex);

    // Get current session info without altering last_seen
    SessionValidationResult inspect_session(const char* session_hex);

    // Logout: invalidate a session token
    esp_err_t delete_session(const char* session_hex);

    // Invalidate sessions by role (or all if role == Admin)
    void invalidate_sessions(AuthRole role);

    // Verify long-lived API key (for machine/automation X-API-Key)
    bool verify_api_key(const char* api_key, AuthRole* out_role);

    // Issue a short-lived single-use WebSocket ticket
    esp_err_t create_ws_ticket(AuthRole role, char out_ticket_hex[33], uint32_t* out_expires_in_ms);

    // Validate and consume a WebSocket ticket (single-use)
    bool consume_ws_ticket(const char* ticket_hex, AuthRole* out_role);

    // Credential management (Admin only)
    esp_err_t rotate_credential(AuthRole role, const char* new_token);
    esp_err_t disable_credential(AuthRole role);

    // Migration helper: check legacy tokens from DeviceConfig on first boot
    esp_err_t migrate_legacy_tokens(const char* legacy_admin, const char* legacy_installer, const char* legacy_op);

    // Get credential configuration statuses (never exposes secrets)
    void get_credential_status(bool& admin_enabled, bool& installer_enabled, bool& op_enabled) const;

    // Factory reset
    esp_err_t factory_reset();

    // Bounded rate-limiting for failed attempts
    bool is_rate_limited() const;
    void record_failed_attempt();
    void record_successful_attempt();

private:
    AuthStore* store_ = nullptr;
    AuthStore default_store_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;

    AuthSession sessions_[kMaxSessions] = {};
    WsTicket ws_tickets_[kMaxWsTickets] = {};
    CommissioningState commissioning_state_{CommissioningState::None};

    // Rate limiter state
    uint32_t failed_attempts_ = 0;
    uint64_t lockout_until_us_ = 0;

    void cleanup_expired_sessions_locked(uint64_t now_us);
    void cleanup_expired_tickets_locked(uint64_t now_us);
    bool verify_credential_locked(const char* credential, AuthRole* out_role);
};

} // namespace lg::auth
