# LG-HW-003 — PCA Bus Recovery + RMT Transmission Integrity

## 1. Executive Summary

LG-HW-003 transitions LumiGrid's physical output subsystems from architectural coexistence into active peripheral fault resilience, diagnosis, and transport integrity verification.

Following the stabilization of dual-core memory and lifecycle coexistence in LG-HW-002, HW-003 addresses:
1. **PCA9685 / I2C Bus Recovery**: Eliminating unbounded/100 ms I2C stalls in the 20 ms render path, classifying transport faults via GPIO line inspection, executing standards-based I2C bus clearing (up to 9 SCL pulses with open-drain recovery), verifying chip state registers post-recovery, and strictly upholding the `/OE` safety contract.
2. **RMT Transmission Integrity**: Registering IRAM-safe TX-done event callbacks backed by lock-free internal SRAM ledgers, validating generated RMT symbol counts against wire payload formulas, enforcing submit-to-completion parity, tracking scheduling latency margins against deadline near misses, and classifying overall RMT health.
3. **Qualification Framework**: Providing diagnostic telemetry in `/api/v1/diagnostics/output`, extending the coexistence soak harness with delta counters and qualification classification (`PASS` / `MARGINAL` / `FAIL`), and establishing an empirical decision gate for retaining the 8-lane RMT architecture versus prototyping an I2S-parallel DMA transport.

> [!IMPORTANT]
> **Transport Integrity vs. Physical Signal Verification**:
> RMT TX-done callbacks and symbol count validation prove that the ESP-IDF/RMT driver and peripheral generated the exact intended symbol stream. They do **not** physically prove that remote LED chips correctly decoded the electrical pulses. True physical signal integrity requires bench oscilloscope/logic analyzer measurement or external loopback.

---

## 2. PCA9685 / I2C Fault Classification & Bus Recovery

### 2.1 Bounded 10 ms Transaction Timeout
The legacy I2C implementation waited up to `pdMS_TO_TICKS(100)` (100 ms) per transaction. In a 20 ms (50 FPS) or 25 ms (40 FPS) lighting schedule, a 100 ms timeout caused catastrophic frame deadline misses and scheduling queue stalls.
At 100 kHz I2C clock speed, a 33-byte PCA9685 multi-byte PWM frame requires ~3.3 ms to transfer.
HW-003 establishes:
```cpp
constexpr uint32_t kPcaTransactionTimeoutMs = 10;
```
This is strictly enforced across register read, register write, multi-byte PWM frame commit, and recovery verification transactions.

### 2.2 Physical Line Telemetry & Fault Classification
Rather than treating every I2C error generically, the driver samples the physical SDA and SCL GPIO levels upon any transaction error:
```cpp
struct PcaBusState {
    bool sda_high;
    bool scl_high;
};
```
Fault conditions are explicitly classified into `PcaFaultKind`:
- `None`: Healthy operation.
- `TransportError`: Generic I2C driver error.
- `Timeout`: Transaction timed out; lines are idle (SCL=HIGH, SDA=HIGH).
- `DeviceNotResponding`: NACK from slave address or registers.
- `SdaStuckLow`: SCL=HIGH, SDA=LOW. A slave device (or PCA9685) was interrupted during a read and holds the SDA line low. Candidate for standard clock-pulsing bus recovery.
- `SclStuckLow`: SCL line is physically pulled low (SCL=LOW). Clock line is held low by external fault or hardware contention. Software must **not** pulse SCL when SCL is stuck low.
- `RegisterVerificationFailed`: PCA registers (`MODE1`, `MODE2`, `PRE_SCALE`) readback does not match expected configuration.
- `RecoveryFailed`: Software bus-clear or chip reconfiguration failed.

### 2.3 Standards-Based SDA-Stuck Bus Clear Algorithm
When `recover_bus()` is invoked:
1. `/OE` remains strictly **HIGH** (outputs disabled).
2. The legacy ESP-IDF I2C driver is safely uninstalled (`i2c_driver_delete(kI2cPort)`).
3. SCL and SDA GPIOs are configured as open-drain outputs with pull-ups enabled. Both lines are released HIGH.
4. Line levels are sampled:
   - If **SCL is LOW**: The bus cannot be cleared via software clock pulses. The driver records `PcaFaultKind::SclStuckLow`, restores I2C driver, leaves `/OE` HIGH, and returns `ESP_ERR_TIMEOUT`.
   - If **SCL is HIGH and SDA is LOW**: The driver sends up to 9 SCL pulses (5 µs low, 5 µs high open-drain). After each pulse, SDA is sampled. As soon as SDA releases HIGH, pulsing terminates immediately.
5. If SDA is successfully released HIGH, a standard I2C STOP condition is generated:
   - Drive SDA LOW
   - Release SCL HIGH (wait 5 µs)
   - Release SDA HIGH (wait 5 µs)
6. The ESP-IDF legacy I2C master driver is cleanly reinstalled (`i2c_param_config` + `i2c_driver_install`).

### 2.4 /OE Safety Contract & Chip Reinitialization
- **Safety Contract**: Physical output enable (`/OE`, active low) must **never** be driven LOW (enabled) during recovery.
- **State Recovery**: Bus recovery alone does not guarantee the PCA retained its PWM oscillator state or registers.
- Recovery sequence strictly verifies:
  1. `MODE1`: `SLEEP = 0`, `AI = 1` (Auto-increment enabled)
  2. `MODE2`: `OUTDRV = 1` (Totem pole output drive)
  3. `PRE_SCALE`: `0x05` (~1017 Hz PWM at 25 MHz oscillator)
  If any register diverges or verification fails, the complete chip initialization sequence is re-executed (Sleep -> Prescale -> Mode2 -> Wake -> 500 µs oscillator wait -> Restart -> Zero frame).
- A zero frame (`ALL_LED_OFF = 4096`) is committed to ensure no glitch upon subsequent output un-gating.
- `/OE` remains strictly HIGH. Only the output engine (`OutputController`) may lower `/OE` after a subsequent valid rendered frame is committed.

### 2.5 CPU1 Output Core Executor Integration
PCA bus recovery operates under the existing `OutputCoreExecutor` pinned exclusively to CPU1:
```cpp
esp_err_t execute_pca_recovery(OutputController* controller);
```
Recovery cannot execute concurrently with output commits or topology reconfigurations. When `LightingRuntime::clear_output_fault()` detects `OutputFaultDomain::PcaI2c`, it schedules PCA recovery on CPU1 and re-arms output only upon verified completion.

---

## 3. RMT Transmission Integrity Architecture

### 3.1 IRAM-Safe TX-Done Callbacks
In ESP-IDF 5.2, RMT channel completions can trigger registered callbacks:
```cpp
rmt_tx_register_event_callbacks(chan, &callbacks, &user_data);
```
HW-003 establishes an atomic ledger residing permanently in internal SRAM (`DRAM_ATTR static RmtIsrLedger s_ledgers[8]`):
```cpp
struct RmtIsrLedger {
    std::atomic<uint32_t> completions{0};
    std::atomic<uint32_t> last_num_symbols{0};
};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
```
The callback is attributed with `IRAM_ATTR`:
```cpp
static bool IRAM_ATTR on_rmt_tx_done(
    rmt_channel_handle_t channel,
    const rmt_tx_done_event_data_t* event,
    void* user_data
) {
    if (user_data && event) {
        auto* ledger = static_cast<RmtIsrLedger*>(user_data);
        ledger->completions.fetch_add(1, std::memory_order_relaxed);
        ledger->last_num_symbols.store(static_cast<uint32_t>(event->num_symbols), std::memory_order_relaxed);
    }
    return false;
}
```
**Strict ISR Safety Rules**:
- Zero heap allocations (`malloc`, `free`, `new`).
- Zero logging calls (`ESP_LOG*`).
- Zero blocking operations or FreeRTOS queues.
- Execution time < 1 µs.

### 3.2 Symbol Count Formula Validation
Each WS2812/SK6812 frame generated by LumiGrid's custom encoder consists of:
- 1 symbol per bit of pixel wire data (`wire_len * 8`)
- 1 symbol for the reset/latch code
- 1 EOF symbol appended by the ESP-IDF driver
```cpp
expected_symbols = wire_len * 8 + 2;
```
For 256 pixels:
- **RGB (WS2812)**: 768 wire bytes $\to$ $768 \times 8 + 2 = 6,146$ symbols.
- **RGBW (SK6812)**: 1024 wire bytes $\to$ $1024 \times 8 + 2 = 8,194$ symbols.

If `actual_symbols != expected_symbols`, the driver marks the lane and frame as `Faulted` and increments `symbol_count_mismatches`. The frame is not accepted merely because the driver wait returned `ESP_OK`.

### 3.3 Submit-to-Completion Parity
Each lane maintains `submitted_generation` (incremented on each successful RMT submit) and compares it against the ISR-updated `completions` counter after bounded wait:
```cpp
if (completed_gen != L.submitted_generation) {
    ldiag.completion_mismatches++;
    L.state = LaneState::Faulted;
}
```
Any discrepancy indicates missed interrupts or buffer state drift, immediately faulting the lane.

### 3.4 Latency & Deadline Margin Instrumentation
- **Wire Overhead**: Tracks `overhead_us = wait_us - wire_time_us`, recording `max_wait_over_wire_us`.
- **Near-Deadline Near Misses**: A threshold margin of 3 ms:
  ```cpp
  constexpr uint32_t kRmtNearDeadlineMarginUs = 3000;
  ```
  If actual completion occurs within 3000 µs of the timeout expiration budget, `deadline_near_misses` is incremented. This provides an early warning indicator that interrupt refill or Wi-Fi CPU0/1 contention is encroaching on the 64-symbol hardware buffer limit.

### 3.5 RMT Health Classification
- `Faulted`: Any frame failure, timeout, completion mismatch, or symbol count mismatch.
- `Marginal`: Zero functional failures, but `deadline_near_misses > 0`.
- `Healthy`: Zero failures and full timing margin.

---

## 4. Diagnostics & API Schema

The `/api/v1/diagnostics/output` endpoint exposes the complete HW-003 telemetry model:
```json
{
  "output": {
    "faultDomain": "none",
    "topologyValid": true,
    "topologyGeneration": 1
  },
  "rmt": {
    "health": "healthy",
    "frames": 90000,
    "failedFrames": 0,
    "deadlineNearMisses": 0,
    "lanes": [
      {
        "lane": 0,
        "submissions": 90000,
        "completed": 90000,
        "txDoneCallbacks": 90000,
        "completionMismatches": 0,
        "symbolMismatches": 0,
        "lastExpectedSymbols": 6146,
        "lastActualSymbols": 6146,
        "maxSubmitUs": 142,
        "maxWaitUs": 8015,
        "maxWaitOverWireUs": 55,
        "deadlineNearMisses": 0,
        "lastError": 0
      }
    ]
  },
  "pwm": {
    "healthy": true,
    "recoveryRequired": false,
    "lastFaultKind": "none",
    "transactions": 90010,
    "transactionFailures": 0,
    "recoveryAttempts": 0,
    "recoverySuccesses": 0,
    "recoveryFailures": 0,
    "busClearAttempts": 0,
    "busClearSuccesses": 0,
    "busClearFailures": 0,
    "driverReinstalls": 0,
    "lastSdaHigh": true,
    "lastSclHigh": true,
    "lastTransactionUs": 3120,
    "maxTransactionUs": 3304
  }
}
```

---

## 5. Qualification Framework: PASS / MARGINAL / FAIL

The coexistence soak script (`scripts/hardware_coexistence_soak.py`) evaluates post-soak delta counters:

| Metric | PASS | MARGINAL | FAIL |
| :--- | :--- | :--- | :--- |
| **Device Resets** | 0 | 0 | $\ge 1$ |
| **RMT Failed Frames** | 0 | 0 | $\ge 1$ |
| **RMT Completion Mismatches** | 0 | 0 | $\ge 1$ |
| **RMT Symbol Mismatches** | 0 | 0 | $\ge 1$ |
| **RMT Deadline Near Misses** | 0 | $> 0$ | Any timeout |
| **PCA Failures / Recovery Failures** | 0 | 0 | $\ge 1$ |
| **Topology Drift / Validity** | 0 drift, valid | 0 drift, valid | Invalid or drifted |
| **Internal DRAM Reserve** | $\ge 12\text{ KB}$ | $\ge 12\text{ KB}$ | $< 12\text{ KB}$ |
| **Wi-Fi State Drift** | 0 | 0 | APSTA without fallback |

---

## 6. Decision Gate: 8-Lane RMT vs. I2S Parallel Prototype

LumiGrid uses all 8 classic ESP32 RMT blocks allocated as $8 \times 64$ symbols (512 symbols total).
Following a 1-hour physical soak run under maximum network and rendering load:

### Keep Current 8-Lane RMT Architecture If:
1. 0 RMT failed frames.
2. 0 symbol count mismatches.
3. 0 completion parity mismatches.
4. `deadlineNearMisses` rate is effectively zero.
5. Stable Wi-Fi STA throughput and frame rate without core stalls.

### Trigger I2S-Parallel Transport Migration If:
1. Reproducible symbol corruption or completion mismatches under Wi-Fi traffic.
2. `deadlineNearMisses` consistently accumulate, demonstrating that 64 symbols offer inadequate refill buffer time during flash/cache or radio activity.
3. Sustained 8-lane operation cannot coexist with HTTP and network load without dropping frames.
