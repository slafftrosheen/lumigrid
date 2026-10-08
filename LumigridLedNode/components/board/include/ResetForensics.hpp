#pragma once

#include <cstdint>
#include "esp_system.h"
#include "sdkconfig.h"

#if defined(ESP_PLATFORM)
#if !CONFIG_ESP_BROWNOUT_DET
#error "LumiGrid hardware safety contract requires CONFIG_ESP_BROWNOUT_DET"
#endif
#endif

namespace lg::board {

constexpr uint32_t kRtcBootMagic = 0x4C474253; // 'LGBS'
constexpr uint32_t kHealthyBootConfirmMs = 30000;
constexpr uint32_t kAbnormalResetOutputQuarantineMs = 1000;
constexpr uint32_t kMaxConsecutiveAbnormalBootsForSafeMode = 3;

struct RtcBootState {
    uint32_t magic;
    uint32_t total_boots;
    uint32_t consecutive_abnormal_boots;
    uint32_t last_boot_sequence;
    bool     previous_boot_reached_ready;
};

struct ResetDiagnostics {
    esp_reset_reason_t current_reason;
    const char*        reason_str;

    uint32_t total_boots;
    uint32_t consecutive_abnormal_boots;

    bool previous_boot_reached_ready;
    bool current_boot_confirmed;

    bool last_reset_abnormal;
    uint32_t boot_session_id;
};

/// Initialize RTC reset forensics, evaluate reset reason, and print the early serial banner.
void init_reset_forensics();

/// Returns true if the reset reason was classified as abnormal (panic, wdt, brownout).
bool is_abnormal_reset(esp_reset_reason_t reason);

/// Human-readable reset reason string (e.g. "power_on", "brownout", "panic").
const char* reset_reason_to_str(esp_reset_reason_t reason);

/// Check if Safe Mode is mandated due to consecutive abnormal boots.
bool is_safe_mode_mandated();

/// Check if output quarantine is currently active following an abnormal reset.
bool is_abnormal_reset_quarantine_active(uint32_t uptime_ms);

/// Mark the boot as confirmed healthy after stability dwell.
void confirm_healthy_boot();

/// Retrieve current reset diagnostics.
ResetDiagnostics get_reset_diagnostics();

/// Direct query of boot session id.
uint32_t get_boot_session_id();

} // namespace lg::board
