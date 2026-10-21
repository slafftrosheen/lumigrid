> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-HW-004 — Electrical Boot Safety + Power-Step Control + Reset Forensics

## 1. Executive Summary

LG-HW-004 resolves electrical transients, boot-time output glitches, and post-reset recovery below the normal driver layer.

Prior to LG-HW-004, the controller presented several electrical and forensic risks:
1. **Boot Glitches & Floating Pins**: Between MCU power-on and driver initialization, the PCA9685 `/OE` line could float or be driven low by pull-downs, and addressable LED data lines (A1–A8) floated, resulting in flash bursts or random color artifacts on connected LED fixtures.
2. **First-Frame Output Transients**: Output drivers could transmit normal lighting frames before zero/black output was established on the wire.
3. **Reset-Loop Misclassification**: Warm reboots (OTA updates, operator software restarts) were lumped into a single boot counter, causing false trips into Safe Mode.
4. **Brownout & Watchdog Forensics**: Brownout events and watchdog resets were not tracked with RTC memory forensics or early serial banners.
5. **Sudden Inrush Power Steps**: Large lighting steps (e.g., cold boot directly into 100% white, or blackout exit) caused severe current spikes on the DC power bus, potentially triggering power supply collapse or brownout resets during Wi-Fi association transients.
6. **Unsafe Post-Abnormal Reboot Restoration**: After a panic, brownout, or watchdog crash, the system could immediately slam the power rails with heavy load before power supplies and networking stabilized.

HW-004 resolves all of these failure modes while strictly maintaining:
- 8 × Addressable LED lanes (WS2812/SK6812)
- 8 × PCA9685 PWM channels (100 kHz I2C, address 0x55)
- 8 × 64 RMT symbol architecture with CPU0/CPU1 core affinity split
- Unchanged API surfaces and binary footprint bounds

---

## 2. Early Electrical Output Safety (`BoardOutputSafety`)

### 2.1 Earliest Application Entry Point
At the very beginning of `app_main()`, before any other subsystem (NVS, Wi-Fi, logging, heap allocation, I2C, or RMT), `lg::board::force_outputs_safe_early()` is executed:

```cpp
esp_err_t force_outputs_safe_early() {
    // 1. Immediately drive PCA /OE HIGH (disable PWM channels)
    gpio_reset_pin(kPcaOe);
    gpio_set_direction(kPcaOe, GPIO_MODE_OUTPUT);
    gpio_set_level(kPcaOe, 1);
    s_diag.pca_oe_high_at_bootstrap = (gpio_get_level(kPcaOe) == 1);

    // 2. Drive all 8 ALED pins LOW so they do not float before RMT takes ownership
    s_diag.aled_low_mask = 0;
    for (uint8_t i = 0; i < kAledCount; ++i) {
        gpio_num_t pin = kAledPins[i];
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
        if (gpio_get_level(pin) == 0) {
            s_diag.aled_low_mask |= static_cast<uint8_t>(1u << i);
        }
    }

    s_diag.early_safety_applied = true;
    return ESP_OK;
}
```

### 2.2 Electrical State Guarantees
- **PCA9685 `/OE`**: Pin `GPIO_NUM_25` is immediately driven **HIGH** (logic 1), forcing all PCA9685 output MOSFETs/drivers into high-impedance / disabled state.
- **ALED Pins (A1–A8)**: `GPIO_NUM_16, 4, 17, 18, 19, 23, 26, 27` are driven **LOW** (logic 0). WS2812/SK6812 protocol requires data low at idle (>280 µs reset pulse). Driving them LOW eliminates floating high-impedance states that cause first-pixel false triggers.
- **No `gpio_hold_en`**: Normal GPIO output mode without RTC hold registers prevents latching pins into un-reconfigurable states during peripheral driver initialization.

---

## 3. First-Black & First-Zero Output Confirmation Gates

### 3.1 ParallelRmtDriver First-Black Frame Contract
To eliminate transient color flashes upon driver bring-up, `ParallelRmtDriver` enforces a first-black gate:
1. `first_black_frame_confirmed_` is initialized to `false`.
2. Any `commit()` call containing non-zero pixel wire data before a verified all-zero frame has completed is rejected with `ESP_ERR_INVALID_STATE`.
3. When an all-zero (black) frame successfully completes across all configured lanes, `first_black_frame_confirmed_` transitions to `true` and records telemetry via `lg::board::record_first_addressable_black()`.
4. The gate resets to `false` on any driver reconfiguration, release, or fault clearing.

### 3.2 Pca9685Driver Zero-First Output Enable Contract
The PCA9685 hardware output enable (`/OE` active low) is governed by a strict zero-first rule:
1. `enable_outputs(true)` asserts `zero_confirmed_`. If a zero frame has not yet been committed to chip registers, `/OE` is kept HIGH.
2. In both `init()` and `recover()`, `ALL_LED_OFF = 4096` is committed over I2C to write zero duty to all channels, and `lg::board::record_first_pwm_zero()` is called.
3. `/OE` is lowered only after the software output controller verifies valid frame generation.

---

## 4. RTC Reset Forensics & Safe Mode Classification

### 4.1 RTC-Backed Boot State
A dedicated struct resides in RTC slow memory:
```cpp
struct RtcBootState {
    uint32_t magic;                       // 0x4C474253 ('LGBS')
    uint32_t total_boots;
    uint32_t consecutive_abnormal_boots;
    uint32_t last_boot_sequence;
    bool     previous_boot_reached_ready;
};
```
If `magic != kRtcBootMagic` (power loss, cold boot), the state is cleanly initialized.

### 4.2 Abnormal vs Normal Reset Classification
Reset reasons are classified according to root cause:
- **Abnormal Resets**:
  - `ESP_RST_PANIC` (Software crash / assertion / abort)
  - `ESP_RST_INT_WDT` (Interrupt watchdog timeout)
  - `ESP_RST_TASK_WDT` (Task watchdog timeout)
  - `ESP_RST_WDT` (Other watchdog resets)
  - `ESP_RST_BROWNOUT` (VDD droop below brownout detector threshold)
  - Increments `consecutive_abnormal_boots`.
- **Normal Resets**:
  - `ESP_RST_POWERON` (Cold power switch on)
  - `ESP_RST_SW` (Clean software reboot / OTA restart / operator reboot)
  - `ESP_RST_DEEPSLEEP`
  - Does **not** increment `consecutive_abnormal_boots`.

### 4.3 Safe Mode Criterion & Healthy Boot Confirmation
- **Safe Mode Trigger**: Mandated **only** when `consecutive_abnormal_boots >= 3`. Normal reboots can never trigger Safe Mode.
- **Healthy Boot Dwell**: After reaching `StartupState::Ready`, the runtime task dwells for 30,000 ms. Once 30 seconds of stable operation elapse, `lg::board::confirm_healthy_boot()` is called, resetting `consecutive_abnormal_boots = 0` and recording `previous_boot_reached_ready = true`.

### 4.4 Early Serial Banner & Brownout Detector
Before logging or Wi-Fi start, a structured serial banner is output:
```text
BOOT_RESET reason=power_on abnormal=0 consecutive=0 total=1 previousReady=0 session=0x4C470001
```
`sdkconfig.defaults` explicitly mandates:
```text
CONFIG_ESP_BROWNOUT_DET=y
```
A compile-time `#error` check in `ResetForensics.hpp` prevents building firmware with brownout protection disabled.

---

## 5. Bounded Power-Step Control (`PowerBudget`)

### 5.1 Per-Frame Power Step Limitation
To protect power supplies and prevent ground bounce / voltage droop, `PowerBudget` enforces a ramp rate limit:
```cpp
max_step_mw = max_power_mw * frame_period_ms / ramp_ms;
```
- Default `ramp_ms = 750 ms`.
- For a 100 W budget at 25 ms frame period, maximum allowable increase per frame is:
  $$\Delta P_{\max} = 100{,}000 \times \frac{25}{750} \approx 3{,}333\text{ mW}$$
- Current granted power cannot exceed `previous_granted_power + max_step_mw`.

### 5.2 Global Proportional Scaling
When power is restricted by step limiting or the total budget:
- Both addressable color brightness ($R, G, B, W$) and PWM channel duty cycles ($0.0 \dots 1.0$) are scaled globally by a uniform factor $S$:
  $$S = \frac{P_{\text{granted}}}{P_{\text{requested}}}$$
- Relative color balance and channel-to-channel ratios are preserved exactly. Channels are **not** turned on sequentially or in a staircase pattern.

### 5.3 Soft-Start Triggers on High-Risk Transitions
`restart_soft_start()` is invoked to reset granted power to 0 mW and begin the 750 ms ramp on:
1. Boot un-quarantine / output enable.
2. Blackout exit.
3. Channel topology reconfiguration.
4. Scene / preset change.
5. Manual override engagement.
6. Fault recovery re-arming.
7. Wi-Fi connection association / reconnect transient events.

---

## 6. Abnormal Reset Output Quarantine

When an abnormal reset (`BROWNOUT`, `PANIC`, `WDT`) occurs:
- Physical lighting output is placed under a **1000 ms quarantine dwell**.
- Addressable LEDs remain black; PCA `/OE` remains HIGH.
- Wi-Fi, HTTP API, network discovery, and diagnostics boot normally with zero delay.
- Once uptime reaches 1000 ms, quarantine expires, and `LightingRuntime` automatically initiates soft-start ramping.

---

## 7. Diagnostics & Verification Endpoints

### 7.1 `/api/v1/diagnostics/output`
Extended with four diagnostic blocks:
- `reset`: `reason`, `reasonStr`, `abnormal`, `consecutiveAbnormalBoots`, `totalBoots`, `previousBootReady`, `currentBootConfirmed`, `safeModeMandated`, `quarantineActive`, `bootSessionId`, `brownoutDetectorEnabled`.
- `bootOutput`: `earlySafetyApplied`, `pcaOeHighAtBootstrap`, `aledLowMask`, `firstAddressableBlackConfirmed`, `firstPwmZeroConfirmed`, `firstOutputCommitMs`.
- `powerTransient`: `lastRequestedMw`, `lastGrantedMw`, `maxStepMw`, `powerRampActive`, `rampElapsedMs`, `rampTargetMs`, `stepClamps`, `wifiTransientClamps`, `quarantineClamps`.
- `activationTiming`: `quarantineDwellMs`, `quarantineExpired`, `softStartElapsedMs`, `outputsArmed`, `firstSafeFrameMs`.

### 7.2 `/api/v1/factory/boot-safe`
Returns bench verification state:
```json
{
  "oeLevel": 1,
  "oeHigh": true,
  "addressableMask": 0,
  "addressableAllLow": true,
  "resetReason": "power_on",
  "abnormalReset": false,
  "brownoutDetectorEnabled": true,
  "outputsArmed": true,
  "quarantineActive": false,
  "firstBlackConfirmed": true,
  "firstZeroConfirmed": true
}
```

---

## 8. Verification & Qualification Results

1. **Invariants Validation (`check_invariants.py`)**: Passed with 0 errors. Verified early safety call order, GPIO pin sets, brownout detector requirement, first-black frame gating, and absence of warm-boot safe mode.
2. **PlatformIO Test Environment (`pio run -e test`)**: Compiles and runs all runtime hardening test suites including `test_lghw004_boot_safety_power_and_forensics()`.
3. **Soak Test Lifecycle Harness (`scripts/hardware_coexistence_soak.py`)**: Enhanced with `bootSessionId` session tracking and classified reboot reporting (`FAIL: POWER/BROWNOUT`, `FAIL: PANIC`, `FAIL: TASK_WDT`, `FAIL: INT_WDT`).
