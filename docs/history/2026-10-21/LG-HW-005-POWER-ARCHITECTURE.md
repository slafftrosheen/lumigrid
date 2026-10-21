> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-HW-005 — Production Power Model & Multi-Supply Integrity

## 1. Executive Summary

Batch **LG-HW-005** upgrades LumiGrid from a legacy 16-bit / 60,000 mW single-rail power estimator to a production-grade **32-bit milliwatt electrical power architecture with multi-supply domain integrity and authoritative hardware commit guarding**.

Prior to LG-HW-005, the system suffered from several key electrical and architectural limitations:
1. **16-Bit Power Ceiling**: Global and per-channel power limits were stored as `uint16_t` milliwatts, artificially capping configuration and calculations at 60,000 mW (60 W). For high-density architectural installations (e.g., 8 channels × 256 WS2812/SK6812 pixels exceeding 1,800 W), the 16-bit type experienced integer overflow, causing erratic clamping or silent bypass.
2. **Single Global Rail Assumption**: Installations frequently use multiple physical power supplies (e.g., separate 5 V supplies for addressable runs and 12 V / 24 V supplies for high-power PWM floodlights). The legacy system had no domain partitioning, meaning an overload on one rail throttled all channels indiscriminately.
3. **PWM Silent Zero-Power Defect**: Unconfigured PWM channels defaulted to 0 mW full-load rating. When driven to 100% duty, the power budget treated them as consuming zero power, concealing severe electrical overloads.
4. **Estimated vs Actual Clamped Power Discrepancy**: When power scaling was applied, diagnostics reported the ceiling limit rather than recalculating the true electrical power from the actual scaled channel values.
5. **Bypassable Power Guard**: Factory test effects, burn-in routines, and direct manual API commands bypassed the software `PowerBudget` engine in `LightingRuntime`, allowing unthrottled full-white frames to reach physical drivers.
6. **Telemetry Misleading**: Systems often conflated modeled software estimates with physical measurements, misleading operators into believing physical current shunt sensors were present on board.

LG-HW-005 resolves all of these issues while maintaining:
- 8 × Addressable LED lanes (WS2812 / SK6812) via parallel RMT
- 8 × High-power PWM channels via PCA9685 (100 kHz I2C, address 0x55)
- Full backward-compatible lossless migration (V1 -> V2 -> V3 -> V4) with zero NVS erasures
- Zero heap allocations during frame commit power enforcement

---

## 2. Electrical Specifications & Connector Matrix

### 2.1 Hardware Capabilities vs Data Model Ceilings
It is critical to distinguish between **software data-model ceilings** and **physically qualified board electrical ratings**:
- **Software Ceiling (`PowerConfigLimits`)**:
  - `kMaxGlobalPowerMw = 2,000,000 mW` (2,000 W)
  - `kMaxChannelPowerMw = 1,000,000 mW` (1,000 W per channel)
  - `kMinSupplyVoltageMv = 3,000 mV` (3 V)
  - `kMaxSupplyVoltageMv = 60,000 mV` (60 V)
  - `kMaxSupplyCurrentMa = 200,000 mA` (200 A)
- **Physical Board Qualification**:
  - The LumiGrid PCB contains logic buffers, the ESP32-WROOM-32E MCU, and PCA9685 I2C driver.
  - **High-current LED power is NOT routed through the ESP32 mainboard traces**.
  - Power injection must be wired directly from qualified external power supplies to LED strips and fixtures, with shared DC ground return.

### 2.2 Connector Qualification Matrix

| Port / Header | Logical Channels | Physical Driver | Voltage Rating | Max Board Trace Current | External Wiring & Fusing Requirement |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **A1 – A8** | Ch 0 – Ch 7 | 74HCT245 Level Shifter (5V CMOS) driven by ESP32 RMT | 5.0 V DC Logic Data Out | Data signals only (< 25 mA per pin) | External 5V or 12V/24V (for strips with internal buck) power injection directly to strips. Fuse each branch according to strip wire gauge (max 5A per injection tap). |
| **P1 – P8** | Ch 8 – Ch 15 | PCA9685 Open-Drain / Push-Pull PWM Gates | 3.3V / 5V Gate Drive (V_load up to 30V DC) | Logic gate drive only | External MOSFET / Driver stage. Load current returns through power ground plane. Fused externally per load circuit. |
| **PCA `/OE`** | Hardware Master | Active-LOW Output Enable (Pin 25) | 3.3 V Logic | Logic level | Pulled HIGH by external 10k pull-up resistor on board to guarantee disabled outputs during power transitions. |
| **Main DC In** | Logic Rail | On-board DC-DC Buck (TPS54302 or LDO) | 7.0 V – 28.0 V DC (Nominal 12V/24V) | 1.5 A (Logic + Buffer only) | Fused on board (2A fast-acting). Does NOT power LED fixtures directly. |

---

## 3. Data Architecture: DeviceConfig Schema V4

### 3.1 32-Bit Types
All power and electrical quantities are explicitly typed using 32-bit unsigned integers:
```cpp
using PowerMilliwatts = uint32_t;
using VoltageMillivolts = uint32_t;
using CurrentMilliamps = uint32_t;
```
All intermediate power calculations (idle + active LED power, PWM duty scaling, supply domain aggregations) are executed using **`uint64_t` arithmetic** to guarantee that arithmetic overflow is mathematically impossible even under worst-case 2,000 W configurations.

### 3.2 Channel Configuration (`ChannelConfig`)
```cpp
struct ChannelConfig {
    char name[24] = "";
    bool enabled = false;

    // Addressable parameters
    AddressableType addressable_type = AddressableType::WS2812;
    PixelOrder pixel_order = PixelOrder::GRB;
    uint16_t pixel_count = 0;
    uint8_t gpio_pin = 0;

    // DMX / Network mapping
    uint16_t dmx_universe = 1;
    uint16_t dmx_start_channel = 1;

    // Electrical Power Model (LG-HW-005)
    PowerMilliwatts rated_full_power_mw = 0; // Rated power at 100% output (PWM load modeling)
    PowerMilliwatts channel_limit_mw = 0;    // Local channel power ceiling (0 = no channel limit)
    uint32_t max_milliwatts = 0;             // Preserved for backward-compatibility
};
```

### 3.3 Multi-Supply Domain Configuration (`PowerSupplyConfig`)
LumiGrid supports up to 4 independent external logical power-supply domains (`kMaxPowerSupplies = 4`):
```cpp
struct PowerSupplyConfig {
    bool enabled = false;
    char name[24] = {};

    VoltageMillivolts voltage_mv = 0;        // e.g., 24000 for 24V
    CurrentMilliamps rated_current_ma = 0;   // e.g., 10000 for 10A
    uint16_t usable_capacity_permille = 800; // Default 80% continuous planning reserve
    uint16_t channel_mask = 0;               // Bitmask: bits 0..7 = A1..A8, bits 8..15 = P1..P8

    uint64_t rated_power_mw() const {
        return (uint64_t(voltage_mv) * rated_current_ma) / 1000ULL;
    }

    uint64_t usable_power_mw() const {
        return (rated_power_mw() * usable_capacity_permille) / 1000ULL;
    }
};
```

### 3.4 Migration Integrity (V1 -> V2 -> V3 -> V4)
`ConfigStore` provides a non-destructive migration chain:
- **`migrate_v3_to_v4`**:
  - `schema_version` is updated from 3 to 4.
  - `v4.max_power_mw = static_cast<PowerMilliwatts>(v3.max_power_mw)`.
  - For each channel:
    - `channel_limit_mw = v3.channels[i].max_milliwatts`.
    - If channel is PWM (i >= 8) and `max_milliwatts > 0`, `rated_full_power_mw = v3.channels[i].max_milliwatts`.
    - `max_milliwatts` field is maintained as a 32-bit alias to prevent breaking existing integrations.
  - Default power supply 0 is initialized (24V, 5A, 80% usable, covering all 8 addressable channels).
- **Validation Rules**:
  - `max_power_mw <= 2,000,000`
  - Per-channel `channel_limit_mw <= 1,000,000` and `rated_full_power_mw <= 1,000,000`
  - Enabled power supplies must have `voltage_mv` in [3,000, 60,000], `rated_current_ma <= 200,000`, `usable_capacity_permille <= 1000`
  - **Overlapping channel masks across enabled power supplies are strictly rejected** with an explicit error to prevent rail contention.

---

## 4. Authoritative Last-Stage Hardware Power Guard

### 4.1 Architecture
In earlier designs, power budgeting was performed solely inside `LightingRuntime::render_frame()`. This created a critical safety gap:
- Direct factory effect generators
- Burn-in stress tests
- Direct manual API commands (`POST /api/v1/control/manual`)
- Hardware bring-up test injections

could stage unbudgeted output directly to drivers, bypassing power limits.

LG-HW-005 introduces the **Authoritative Last-Stage Power Guard** inside `OutputController::try_commit_locked()`. Because every physical output commit path routes through `try_commit_locked()`, no subsystem can bypass power policy.

```
+-------------------------------------------------------------+
| Control Sources: Live DMX / Scene / Factory / Manual / Test |
+-------------------------------------------------------------+
                               |
                               v
               [ OutputTransaction / stage ]
                               |
                               v
+-------------------------------------------------------------+
|            OutputController::try_commit_locked()            |
|                                                             |
| 1. Check Safety Gates (E-Stop / Output Enabled / Topology)  |
| 2. enforce_power_guard_locked()                             |
|    - Calculate actual staged load across A1..A8 and P1..P8  |
|    - Enforce local channel limits                           |
|    - Enforce supply domain usable capacities (isolated)     |
|    - Enforce global system limit                            |
|    - In-place scaling of pixels and PWM wire registers      |
| 3. Physical Commit (RMT DMA start & PCA9685 I2C commit)     |
+-------------------------------------------------------------+
```

### 4.2 In-Place Wire Buffer Scaling
`enforce_power_guard_locked()` calculates power directly from staged pixel wire buffers and PWM duty registers:
- **No Heap Allocations**: Scaling modifies the existing RMT wire buffers and PCA registers in-place.
- **No Double-Scaling**: If a frame was already clamped in `LightingRuntime`, the staged power is already `<= limit`, yielding a scale factor of `256/256` (exact unity, zero modification).
- **Blackout & E-Stop Exemption**: Blackout and E-Stop routines bypass `try_commit_locked()` and write zeros directly, ensuring immediate emergency shutdown without power-limiting interference.

---

## 5. Modeling State & Power Recalculation

### 5.1 Power Estimation State (`PowerEstimateState`)
To prevent unconfigured loads from silently consuming zero watts, the estimator reports its confidence level:
- **`Complete`**: All active channels have complete electrical models (addressable pixel models or configured PWM rated wattage).
- **`Partial`**: One or more active PWM channels have duty > 0 but `rated_full_power_mw == 0`. Telemetry flags these in `unmodeled_channel_mask` and increments `unknown_load_warnings`.
- **`Unmodeled`**: Active channels are driven without any electrical power models.
- **`Unknown`**: Configuration or hardware driver state is undefined.

### 5.2 Post-Scaling Actual Power Recalculation
Rather than setting `limited_power_mw = limit_mw`, `PowerBudget` recalculates actual power directly from the scaled pixel components ($R, G, B, W$) and scaled PWM duty cycles:
$$P_{\text{pixel}} = P_{\text{idle}} + \frac{(R + G + B) \cdot 295\,\text{mW} + W \cdot 380\,\text{mW}}{255}$$
$$P_{\text{pwm}} = \frac{\text{duty} \cdot P_{\text{rated}}}{4095}$$
This ensures telemetry truthfully reflects actual expected power draw rather than theoretical ceilings.

---

## 6. Truthful Diagnostics & API Contract

### 6.1 `GET /api/v1/diagnostics/output`
The output diagnostics endpoint provides full transparency into the electrical subsystem:

```json
{
  "power": {
    "modelState": "complete",
    "requestedMw": 125400,
    "limitedMw": 48000,
    "measuredMw": null,
    "limitMw": 50000,
    "unmodeledChannelMask": 0,
    "unknownLoadWarnings": 0,
    "channelThrottleEvents": 142,
    "overflowRejectionAttempts": 0,
    "supplyDomains": [
      {
        "id": 0,
        "name": "Main 24V Strip Rail",
        "enabled": true,
        "voltageMv": 24000,
        "ratedCurrentMa": 5000,
        "usableCapacityMw": 96000,
        "estimatedLoadMw": 48000,
        "channelMask": 255,
        "throttleEvents": 0
      }
    ]
  }
}
```

> [!IMPORTANT]
> **Measured vs Modeled Truthfulness**:
> `measuredMw` is explicitly serialized as `null` (never `0` and never cloned from modeled power). The physical hardware does not contain on-board shunt resistors or ADC telemetry. Claiming measured current without physical sensors violates certification truthfulness.

---

## 7. Bench Power Verification Procedure & Qualification Blockers

### 7.1 Required Equipment
1. Multi-channel DC Power Supply (0–30V, 0–20A) with precision current readback.
2. Digital Multimeter (true RMS) and calibrated DC current clamp (e.g., Fluke 376 FC).
3. 2-channel Oscilloscope (100 MHz+) for observing VDD rail droop and PWM rise times.
4. Active LED fixtures: 8 × 256 WS2812B strips + 8 × high-power LED arrays.

### 7.2 Verification Steps
1. **Migration Verification**:
   - Flash firmware onto an existing node containing V3 configuration with `max_power_mw = 50000`.
   - Verify non-destructive upgrade to Schema V4. Confirm all channel names, universes, and addresses are intact.
2. **High-Power 32-Bit Range Check**:
   - Configure node for `max_power_mw = 250000` (250 W).
   - Stage 8 × 256 full-white pixels.
   - Verify no 16-bit integer wrapping occurs and power is clamped at exactly 250 W.
3. **Multi-Supply Rail Isolation Check**:
   - Assign Channel 0 to Supply 0 (usable capacity = 20 W).
   - Assign Channel 1 to Supply 1 (usable capacity = 100 W).
   - Drive both channels to full white.
   - Verify Channel 0 is throttled to 20 W, while Channel 1 runs at 100% unthrottled brightness.
4. **Authoritative Last-Stage Bypass Test**:
   - Set global power limit to 10 W (`max_power_mw = 10000`).
   - Trigger a factory burn-in test effect via `POST /api/v1/factory/effects`.
   - Measure physical current draw on DC rail. Confirm current clamp confirms total draw does not exceed 10 W.
5. **Unmodeled Load Safety Alert**:
   - Drive PWM Channel 8 with `rated_full_power_mw = 0`.
   - Verify `GET /api/v1/diagnostics/output` reports `"modelState": "partial"`, `unmodeledChannelMask` with bit 8 set, and incremented `unknownLoadWarnings`.

### 7.3 Qualification Blockers
Any of the following occurrences constitutes an immediate qualification failure:
- Any reset attributed to `ESP_RST_BROWNOUT` during 0% to 100% full-load step transients.
- Any DC voltage droop on the 3.3V logic rail exceeding 150 mV ($< 3.15\text{ V}$).
- Any physical power draw exceeding configured `max_power_mw` or domain capacity by more than 5%.
- Any NVS configuration loss or factory reset triggered by configuration schema upgrades.
