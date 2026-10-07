# LG-HW-001 — Deterministic Hardware Coexistence Architecture

**Status**: Implemented & Hardened  
**Scope**: Classic ESP32 (Dual Core Xtensa LX6 @ 240MHz, 8MB SPI Flash)  
**Document Owner**: LumiGrid Embedded Firmware Architecture Team  

---

## 1. Architectural Domain Separation (CPU0 vs CPU1)

On the classic ESP32 dual-core SoC, shared interrupt handlers and task contention between networking (Wi-Fi/LWIP/HTTPD) and hard real-time lighting output (ESP-IDF RMT TX, I2C/PCA9685) produce severe jitter, channel desynchronization, and silent hardware deadlock if core affinity is uncoordinated.

LG-HW-001 establishes an immutable domain boundary:

```
┌─────────────────────────────────────────┐  ┌─────────────────────────────────────────┐
│                 CPU 0                   │  │                 CPU 1                   │
│         Network & Control Domain        │  │       Hardware & Lighting Domain        │
├─────────────────────────────────────────┤  ├─────────────────────────────────────────┤
│ • esp_netif / LWIP TCP/IP stack         │  │ • LightingRuntime render loop (50 Hz)   │
│ • Wi-Fi driver task (pinned to CPU0)    │  │ • OutputController & ParallelRmtDriver  │
│ • HTTPD web server & REST API handlers  │  │ • PCA9685 synchronous PWM driver        │
│ • Main boot task (returns after boot)   │  │ • OutputCoreExecutor (output_hw worker) │
│ • Fleet discovery & NVS background tasks│  │ • RMT TX interrupts & ISR callbacks     │
└─────────────────────────────────────────┘  └─────────────────────────────────────────┘
                                       │    ▲
                  Synchronous Marshalled Calls (Queue + Binary Semaphore)
                                       └───►│
```

### Core Configuration Invariants (`sdkconfig.defaults` & `sdkconfig.factory`)
1. `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y` — Prevents Wi-Fi stack ISRs and event handling tasks from preempting time-critical lighting tasks on CPU1.
2. `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y` — Keeps boot initialization on CPU0, returning cleanly so its 10 KB stack is reclaimed.
3. `CONFIG_RMT_ISR_IRAM_SAFE=y` — Ensures all RMT interrupt service routines execute from IRAM even when the flash cache is disabled during Wi-Fi NVS/flash writes.

---

## 2. RMT Core Ownership & Lifecycle Marshalling

### Problem
In ESP-IDF 5.x, calling `rmt_new_tx_channel()` allocates an interrupt vector on whichever CPU core is currently executing the caller. When `OutputController::init()` or `reconfigure()` is called from the main boot task or an incoming HTTP POST request on CPU0, the RMT interrupt vector becomes registered on CPU0. When `LightingRuntime` transmits from CPU1, cross-core interrupt notifications induce scheduling latencies, missed latch timings, and FreeRTOS assert crashes.

### Solution: `OutputCoreExecutor`
The `OutputCoreExecutor` runs a dedicated FreeRTOS worker (`output_hw`) pinned permanently to CPU1 (`xTaskCreatePinnedToCore(..., 1)`):
- Priority: 5 (Dedicated hardware lifecycle context).
- Stack: 6144 bytes.
- Zero per-frame heap allocation: Commands use static/stack descriptors with binary completion semaphores.
- Core 1 Bypass: If `OutputController::init()` or `reconfigure()` is invoked by a thread already on CPU1 (e.g. `LightingRuntime`), execution proceeds directly without queuing to prevent deadlock.
- All hardware lifecycle functions (`init_on_output_core`, `reconfigure_on_output_core`) enforce `assert(xPortGetCoreID() == 1)` in debug and test environments.

---

## 3. RMT Memory Constraints (8 Lanes × 64 Symbols)

The classic ESP32 RMT peripheral features 8 channels and a shared RAM of 512 32-bit symbol words.

$$\text{Total Memory} = 512 \text{ words} = 8 \text{ channels} \times 64 \text{ words/channel}$$

1. **Exact Allocation**: Every lane is configured with `mem_block_symbols = 64`. Increasing any single lane to 128 or 192 symbols steals RAM blocks from adjacent channels, reducing the total available addressable outputs below the 8 required by LumiGrid.
2. **Interrupt Priority**: TX channels are registered with `cc.intr_priority = 3`, ensuring RMT buffer reload ISRs preempt standard tasks while remaining below FreeRTOS critical masking levels.
3. **Internal DRAM Buffers**: Because `CONFIG_RMT_ISR_IRAM_SAFE=y` is active, any buffer touched in interrupt context or encoder callback must reside in internal SRAM. `ParallelRmtDriver` allocates wire buffers and `StripEncoder` memory strictly via `heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Allocation from external PSRAM or standard general heap is prohibited.

---

## 4. PCA9685 Synchronous I2C & Fail-Safe Architecture

The 8-channel PWM subsystem is driven by an NXP PCA9685 controller at I2C address `0x55` on `I2C_NUM_0` (SDA: GPIO 21, SCL: GPIO 22, 100 kHz).

### Invariants:
1. **Legacy Synchronous Driver**: Preserves `#include "driver/i2c.h"` synchronous master mode with 100 ms timeouts. Prevents asynchronous queue allocations, race conditions with HTTP workers, and dynamic heap churn.
2. **Output Enable Fail-Safe (`/OE` on GPIO 25)**:
   - Configured as active-low GPIO output.
   - Initialized to logic `HIGH` (disabled/high-impedance) before I2C initialization begins.
   - Only asserted `LOW` after successful PCA9685 register configuration and clean all-off channel initialization.
   - If I2C communication fails or errors exceed thresholds, `/OE` is immediately driven `HIGH`, cutting power/duty to external MOSFET drivers.
3. **Core Pinning**: Initialized exclusively on CPU1 via `OutputCoreExecutor`.

---

## 5. Factory Hardware Coexistence Soak Mode

To qualify deterministic coexistence under maximum stress, firmware provides the `FactoryEffect::HardwareCoexistence` pattern (ID: `hardware_coexistence` or `coexistence`):

- **6 Continuous Addressable Patterns** (cycling every 1000 ms across all 8 lanes × 256 pixels = 2048 LEDs):
  1. *Moving Pixel (Sparse)*: Single pixel traveling across the entire strip to verify WS2812 latch reset margins.
  2. *Alternating*: High-frequency spatial toggles.
  3. *Checkerboard*: Alternating multi-pixel segments.
  4. *Full White / Saturated Color*: Maximum current demand and maximum high-bit RMT symbols.
  5. *All Black*: Complete latch timing verification.
  6. *Deterministic PRNG Pattern*: Pseudo-random data testing symbol decoding variation.
- **Simultaneous 8-Channel PCA9685 Modulation**: Sweeps all 8 PWM channels with phase-offset sine duty cycles.
- **Zero Allocations**: Pre-allocated frame buffers; zero `malloc`/`new` in the 50 Hz render loop.

---

## 6. Telemetry & Diagnostics API

Telemetry is exposed via the authenticated `/api/v1/diagnostics/output` endpoint:

```json
{
  "rmt": {
    "allocation_core": 1,
    "interrupt_priority": 3,
    "iram_safe": true,
    "active_lanes": 8,
    "total_frames": 18000,
    "failed_frames": 0,
    "lanes": [
      {
        "lane": 0,
        "submissions": 18000,
        "completed": 18000,
        "submit_failures": 0,
        "wait_timeouts": 0,
        "other_failures": 0,
        "max_submit_us": 42,
        "max_wait_us": 8200,
        "last_error": "ESP_OK"
      }
    ]
  },
  "pwm": {
    "init_core": 1,
    "transactions": 144000,
    "transaction_failures": 0,
    "consecutive_failures": 0,
    "last_error": "ESP_OK"
  }
}
```

---

## 7. Soak Stress Test Script

The automated stress tool `scripts/hardware_coexistence_soak.py` bombards the node with concurrent network activity while the hardware soak mode runs:

```bash
python scripts/hardware_coexistence_soak.py \
  --host 192.168.4.1 \
  --admin-key <ADMIN_TOKEN> \
  --duration 300 \
  --workers 4
```

### Pass Criteria:
- Duration $\ge 300$ seconds.
- `rmt.failed_frames == 0`
- `rmt.lanes[*].wait_timeouts == 0`
- `pwm.transaction_failures == 0`
- Zero watchdog panics or core resets.

---

## 8. Known Remaining Hardware Risks & Roadmap

1. **RMT Memory Limits on Classic ESP32**:
   - The 8-lane × 64-symbol limit is the physical ceiling for classic ESP32 RMT. Any future channel expansion (e.g., 16 addressable outputs) will require migrating addressable transport to I2S LCD parallel mode (GDMA).
2. **I2C Bus Contention**:
   - The PCA9685 shares `I2C_NUM_0`. Ensure no other high-rate sensors or peripherals are added to this bus to prevent frame render delays.
3. **Power Ground Jitter**:
   - Full-white pattern on 2048 LEDs pulls substantial current. External 5V/12V power distribution must maintain common ground reference to prevent GPIO signal edge degradation.
