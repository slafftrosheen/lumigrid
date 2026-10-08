#include "ResetForensics.hpp"
#include <cstdio>
#include "esp_random.h"
#include "esp_timer.h"

namespace lg::board {

RTC_DATA_ATTR static RtcBootState s_rtc_state{};
static uint32_t s_boot_session_id = 0;
static esp_reset_reason_t s_current_reset_reason = ESP_RST_UNKNOWN;
static bool s_current_reset_abnormal = false;
static bool s_current_boot_confirmed = false;
static bool s_initialized = false;

const char* reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "power_on";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "interrupt_watchdog";
        case ESP_RST_TASK_WDT:  return "task_watchdog";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        case ESP_RST_EXT:       return "external";
        default:                return "unknown";
    }
}

bool is_abnormal_reset(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

void init_reset_forensics() {
    if (s_initialized) {
        return;
    }
    s_initialized = true;

    s_current_reset_reason = esp_reset_reason();
    s_current_reset_abnormal = is_abnormal_reset(s_current_reset_reason);

    // Validate RTC memory signature
    if (s_rtc_state.magic != kRtcBootMagic) {
        s_rtc_state.magic = kRtcBootMagic;
        s_rtc_state.total_boots = 0;
        s_rtc_state.consecutive_abnormal_boots = 0;
        s_rtc_state.last_boot_sequence = 0;
        s_rtc_state.previous_boot_reached_ready = false;
    }

    s_rtc_state.total_boots++;
    s_rtc_state.last_boot_sequence++;

    if (s_current_reset_abnormal) {
        s_rtc_state.consecutive_abnormal_boots++;
    }

    // Generate unique session ID for this boot
    s_boot_session_id = esp_random();
    if (s_boot_session_id == 0) {
        s_boot_session_id = 1;
    }

    // Early concise serial banner (Section 16)
    printf("BOOT_RESET reason=%s abnormal=%d consecutive=%lu total=%lu previousReady=%d\n",
           reset_reason_to_str(s_current_reset_reason),
           s_current_reset_abnormal ? 1 : 0,
           static_cast<unsigned long>(s_rtc_state.consecutive_abnormal_boots),
           static_cast<unsigned long>(s_rtc_state.total_boots),
           s_rtc_state.previous_boot_reached_ready ? 1 : 0);
}

bool is_safe_mode_mandated() {
    return s_rtc_state.consecutive_abnormal_boots >= kMaxConsecutiveAbnormalBootsForSafeMode;
}

bool is_abnormal_reset_quarantine_active(uint32_t uptime_ms) {
    if (!s_current_reset_abnormal) {
        return false;
    }
    return uptime_ms < kAbnormalResetOutputQuarantineMs;
}

void confirm_healthy_boot() {
    s_rtc_state.consecutive_abnormal_boots = 0;
    s_rtc_state.previous_boot_reached_ready = true;
    s_current_boot_confirmed = true;
}

ResetDiagnostics get_reset_diagnostics() {
    ResetDiagnostics diag{};
    diag.current_reason = s_current_reset_reason;
    diag.reason_str = reset_reason_to_str(s_current_reset_reason);
    diag.total_boots = s_rtc_state.total_boots;
    diag.consecutive_abnormal_boots = s_rtc_state.consecutive_abnormal_boots;
    diag.previous_boot_reached_ready = s_rtc_state.previous_boot_reached_ready;
    diag.current_boot_confirmed = s_current_boot_confirmed;
    diag.last_reset_abnormal = s_current_reset_abnormal;
    diag.boot_session_id = s_boot_session_id;
    return diag;
}

uint32_t get_boot_session_id() {
    return s_boot_session_id;
}

} // namespace lg::board
