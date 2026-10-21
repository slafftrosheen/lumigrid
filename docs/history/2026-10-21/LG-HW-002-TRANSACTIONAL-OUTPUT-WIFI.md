> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-HW-002 — Transactional Output & Wi-Fi Fault Containment

**Status**: Implemented & Certified  
**Scope**: Classic ESP32 (Dual Core Xtensa LX6 @ 240MHz, 8MB SPI Flash)  
**Document Owner**: LumiGrid Embedded Firmware Architecture Team  
**Predecessor**: [LG-HW-001 — Deterministic Hardware Coexistence Foundation](file:///c:/Users/slavd/lumigrid/docs/LG-HW-001-RMT-WIFI-PCA-COEXISTENCE.md)

---

## 1. Architectural Mission

LG-HW-001 established fundamental domain separation (Wi-Fi pinned to CPU0, RMT/PCA9685 execution pinned to CPU1, IRAM-safe RMT ISR at priority 3, and legacy synchronous I2C). However, real-world deployment exposed two catastrophic failure modes:
1. **Destructive Output Reconfiguration**: When applying new device configurations or testing topology changes, failures in memory allocation, RMT channel creation, or I2C bus transactions would leave previous healthy output hardware deallocated, blacked out, or marked dead (`topology_valid_ = false`), creating unrecoverable dark states.
2. **Wi-Fi Fallback AP State Poisoning & Destructive NVS Erases**: Subsystem startup failures in `WifiService` or `ConfigStore` previously triggered automatic `nvs_flash_erase()` calls, wiping production calibration, Wi-Fi credentials, and device identities. Furthermore, fallback Access Point (`WIFI_MODE_APSTA`) states never properly transitioned back to pure Station (`WIFI_MODE_STA`) upon upstream router reconnection.

LG-HW-002 introduces **true transactional output lifecycle management**, **deterministic internal SRAM admission gates**, **centralized non-destructive NVS runtime ownership**, and **state-machine corrected Wi-Fi fallback AP containment**.

---

## 2. Memory Admission & Accounting Split

### 2.1 The Two-Pool Problem on Classic ESP32
The ESP32 possesses ~320 KB total SRAM, partitioned into internal data RAM (DRAM) and instruction RAM (IRAM). Due to `CONFIG_RMT_ISR_IRAM_SAFE=y`, RMT transmission buffers, interrupt descriptors, and `StripEncoder` instances **must reside exclusively in internal DRAM** (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`). If internal DRAM drops below ~10–12 KB, Wi-Fi driver packet buffers (`pbuf`), TCP sockets, or FreeRTOS ISR stacks fail immediately, crashing or halting the CPU.

Conversely, generic frame staging buffers, pixel color arrays (`ColorRgbw`), and gamma LUTs do not require internal IRAM safety and can utilize general 8-bit memory.

### 2.2 Memory Admission Formula
Before any output reconfiguration is attempted, `check_output_memory_admission()` calculates the deterministic requirements across all enabled lanes:

$$\text{internal\_bytes} = \sum_{\text{enabled lanes}} \left( \text{pixels}_l \times 3 + \text{kRmtStripEncoderOverhead} + 64 \times 4 \right) + \text{kRmtDriverInternalOverhead}$$

$$\text{generic\_bytes} = \sum_{\text{enabled lanes}} \left( \text{pixels}_l \times \text{sizeof(ColorRgbw)} + \text{sizeof(PixelOrderMapping)} \right) + \text{sizeof(AddressableDriver)}$$

Where:
- $\text{kRmtStripEncoderOverhead} = 400 \text{ bytes}$ per enabled lane (internal DRAM for `rmt_encoder_t` state machine).
- $\text{Wire symbols} = 64 \times 4 = 256 \text{ bytes}$ per lane internal channel descriptor.
- $\text{kPostTopologyInternalReserveBytes} = 12 \times 1024 = 12\,288 \text{ bytes}$ (mandatory reserved headroom for Wi-Fi/LWIP).
- $\text{kGenericReserveBytes} = 16 \times 1024 = 16\,384 \text{ bytes}$.

### 2.3 Strict Admission Gating
Reconfiguration is rejected **prior to touching any hardware or freeing any memory** if:
1. $(\text{free\_internal} - \text{internal\_required}) < \text{internal\_reserve}$
2. $\text{largest\_internal} < \max(\text{lane\_wire\_size})$
3. $(\text{free\_8bit} - \text{generic\_required}) < \text{generic\_reserve}$

Rejected requests return `ESP_ERR_NO_MEM`, log an explicit telemetry line (`OUTPUT_ADMISSION reject reason=...`), increment failure counters, and leave active outputs completely untouched.

---

## 3. 9-Step Transactional Reconfiguration Sequence

Output reconfiguration executes as an atomic 9-step state machine on CPU1:

```
[ Step 1: Validate Config ]
         │ (reject -> abort, keep old)
[ Step 2: Check Memory Admission ]
         │ (reject -> abort, keep old)
[ Step 3: Prepare Staged Candidate ]
         │ (channel create / alloc fail -> cancel candidate, keep old)
         ▼ (PRE-SWAP THRESHOLD: No changes to active outputs occurred yet)
[ Step 4: Gate PCA9685 /OE HIGH ] ────────── Disables external PWM loads
         ▼
[ Step 5: Blackout Old Active Output ] ───── Zeroes LEDs on old topology
         ▼
[ Step 6: Commit / Swap Candidate ] ──────── Swaps active pointer & channels
         │ (post-swap failure triggers rollback!)
[ Step 7: Blackout New Active Output ] ───── Zeroes LEDs on new topology
         │ (failure triggers rollback!)
[ Step 8: Clear PCA9685 Channels ] ───────── Writes all-zero duty to PWM registers
         │ (failure triggers rollback!)
         ▼
[ Step 9: Store Snapshot & Publish ] ─────── topology_valid = true, generation++
```

### 3.1 Pre-Swap Failure Safety
If failure occurs in Steps 1, 2, or 3:
- Candidate structures are destroyed via `cancel_candidate()`.
- Active outputs are **not** blacked out.
- Active output channels remain fully valid and enabled.
- `topology_valid_` remains `true`.
- `topology_generation_` does not increment.
- Reconfigure failure telemetry increments.

### 3.2 Post-Swap Best-Effort Rollback
If a failure occurs during Steps 6, 7, or 8 (after the swap boundary):
1. PCA9685 `/OE` is immediately forced `HIGH` (outputs disabled).
2. `rollback_to_last_good_topology()` is triggered.
3. The driver reads `last_known_good_snapshot_` and restores the previously working lane count, pixel lengths, and color orders.
4. If rollback succeeds:
   - Restored topology is blacked out.
   - PCA is cleared and `/OE` restored.
   - `topology_valid_` remains `true`.
   - `topology_rollbacks_` increments.
5. If rollback fails:
   - `/OE` is latched permanently `HIGH` (safe unpowered state).
   - `topology_valid_` is set to `false`.
   - `topology_rollback_failures_` increments.
   - An alarm is logged to telemetry.

---

## 4. Centralized Storage Ownership (`NvsRuntime`)

### 4.1 Elimination of Destructive NVS Erases
Previously, any initialization failure in `WifiService` (`ESP_ERR_NVS_NO_FREE_PAGES`) or `ConfigStore` triggered an immediate `nvs_flash_erase()`. Under weak power supplies or transient brownouts, devices would wipe their own configuration.

In LG-HW-002:
- `components/storage/NvsRuntime` is the single centralized authority for NVS initialization.
- Subsystems (`ConfigStore`, `WifiService`, `LumigridRuntime`) call `NvsRuntime::initialise()` idempotently.
- `nvs_flash_erase()` is completely removed from all automated startup sequences.
- If NVS is corrupted or lacks free pages, `NvsRuntime` sets `destructive_recovery_required_ = true` and logs:
  `STORAGE_NVS init_failed reason=... destructiveRecoveryRequired=1`.
- Destructive formatting requires explicit operator confirmation (such as a 5-second physical button press on boot).

---

## 5. Wi-Fi Fallback AP Lifecycle Correctness

### 5.1 Fallback Transition Flow
When station connection fails or credentials fail to connect:
1. `WifiService` enables the Fallback Access Point, switching from `WIFI_MODE_STA` to `WIFI_MODE_APSTA` (or `WIFI_MODE_AP`).
2. Station retry attempts continue in the background using bounded exponential backoff.
3. When station connection succeeds (`IP_EVENT_STA_GOT_IP`):
   - Rather than immediately calling blocking `esp_wifi_set_mode()` inside the FreeRTOS event callback context, `WifiService` arms an asynchronous one-shot timer (`schedule_exit_fallback_ap(500)`).
   - If STA remains connected when the timer fires, `perform_exit_fallback_ap()` calls `esp_wifi_set_mode(WIFI_MODE_STA)`.
   - `is_fallback_ap_` is cleared, and `fallback_exit_attempts_` is updated.
4. If STA disconnects while the exit timer is running, the timer is cancelled and the fallback AP remains alive.

### 5.2 Wi-Fi Diagnostics & Actual Radio Mode
`WifiDiagnostics` reports both logical and true hardware RF state:
- `actual_mode`: Read directly from `esp_wifi_get_mode()`, preventing UI/telemetry drift where firmware claims STA mode while the softAP radio is still transmitting.
- `start_attempts`, `start_failures`
- `reconnect_attempts`, `reconnect_failures`
- `fallback_activations`, `fallback_exit_attempts`, `fallback_exit_failures`
- `disconnect_events`, `last_disconnect_reason`, `last_error`

---

## 6. Output Lock Contention Telemetry

`OutputController::lock()` monitors mutex acquisition latency:
- If `xSemaphoreTakeRecursive(mutex_, 0)` succeeds immediately, no contention occurred.
- If acquisition blocks, high-resolution microsecond timers track wait time:
  - `output_lock_contentions_`: Total count of blocking lock attempts.
  - `output_lock_max_wait_us_`: Peak duration spent waiting for the output mutex.
- `lifecycle_max_us_`: Peak execution time of output reconfiguration or hardware lifecycle operations.

---

## 7. Soak Test Script Lifecycle Management

`scripts/hardware_coexistence_soak.py` implements end-to-end qualification:
1. **Target Topology Validation**: Verifies node is configured for 8 lanes × 256 pixels + 8 PWM channels.
2. **Baseline Telemetry Capture**: Records starting error counts and memory states.
3. **Automated Effect Management**:
   - Posts `POST /api/v1/factory/effect` with `effect: hardware_coexistence`.
   - Continuously polls `GET /api/v1/factory/status` verifying `activeEffect == "hardware_coexistence"`.
   - Always issues `POST /api/v1/factory/abort` in a Python `finally:` block so node is never left in an unmanaged test loop.
4. **Concurrent Load & Wi-Fi Cycling**:
   - Executes 20 Hz telemetry and status polling.
   - Optional `--exercise-wifi-reconnect` triggers station reconnect cycles during heavy rendering.
5. **Delta Validation**:
   - Zero RMT timeouts or dropped frames.
   - Zero PCA9685 I2C errors.
   - Zero reboots or task watchdogs.
   - Internal SRAM reserve $\ge 12 \text{ KB}$ maintained throughout test.
