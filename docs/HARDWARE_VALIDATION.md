# LumiGrid Hardware Validation Matrix & Physical Bench Status

> **Baseline Commit**: `ea586e39aca19df0d051e2534cf336c73316f582`
> **Date**: October 2026
> **Scope**: Physical hardware validation for LumiGrid LED Node v1.0 (A1–A8, P1–P8, power, thermal, sync, and recovery).

---

## 1. Physical Hardware Validation Policy

> [!IMPORTANT]
> Software simulation and unit tests (e.g. `RuntimeHardeningTests`, `FactoryRunner` unit verification) certify only **software lifecycle and algorithmic correctness**.
> Physical hardware validation milestones (`[PASS]`) MUST NOT be claimed without real physical hardware bench measurement on connected physical fixtures, power analyzers, oscilloscopes, or multi-node test networks.

All physical milestones remain explicitly tracked below as `[PENDING BENCH]` until physical test execution is completed.

---

## 2. Validation Matrix

| Test ID | Domain | Test Scenario | Hardware Setup | Verification Criteria | Status |
| :--- | :--- | :--- | :--- | :--- | :---: |
| **HW-BOOT-01** | Power / Boot | Power-on transient and strapping protection | Physical ESP32 on calibrated bench PSU | No false strapping pin latches on boot, GPIO0 stabilizes within 3s grace period | `[PENDING BENCH]` |
| **HW-A1A8-01** | Addressable | A1–A8 WS2812 / SK6812 parallel RMT signal integrity | 8x LED strips (up to 256 px/lane) attached to RMT output pins | 800 kHz bit timing, clean rise/fall times (<50ns), zero flicker or frame drop at 40 FPS under true 8-lane parallel submission | `[PENDING BENCH]` |
| **HW-P1P8-01** | PWM | P1–P8 PCA9685 I2C output drive | 8x low-voltage LED loads / MOSFET gates | ~1017 Hz PWM waveform (prescale=5), linear duty cycle response (0–100%), 100 kHz I2C bus stability, no bus lockup | `[PENDING BENCH]` |
| **HW-ESTOP-01** | Safety | Physical E-Stop / blackout trip | External emergency stop switch on GPIO | PCA9685 /OE asserted HIGH immediately (<1 µs); addressable blackout protocol-bound (~7.68 ms for 256 px WS2812 / ~10.24 ms SK6812); latched state prevents re-enable. Physical <5 ms addressable cutoff requires future hardware load-switch / power-gate | `[PENDING BENCH]` |
| **HW-PWR-01** | Safety / Power | Dynamic power limiter | DC lab power supply with current shunt | Total system power draw clamped to configured `max_power_mw` without reboot | `[PENDING BENCH]` |
| **HW-THERM-01**| Thermal | 24-hour continuous burn-in at full load | Thermal chamber or sealed enclosure | Board temperature remains within safe operating envelope; no thermal throttling crashes | `[PENDING BENCH]` |
| **HW-SYNC-01** | Fleet Sync | Multi-node optical synchronization | 3 physical nodes with photodiodes on oscilloscope | Frame start optical alignment within <15 ms across nodes running UDP NodeSync | `[PENDING BENCH]` |
| **HW-RESET-01**| Recovery | Physical GPIO0 factory reset | Physical button hold for 5 seconds | Blackout, full NVS erase, wait for button release, clean reboot into unprovisioned AP | `[PENDING BENCH]` |
| **HW-OTA-01**  | Recovery / OTA | Bad firmware image automatic rollback | Physical node flashed with fault-inducing candidate | Bootloader detects unconfirmed candidate failure and rolls back to previous partition | `[PENDING BENCH]` |

---

## 3. LG-035, LG-037 & LG-038 Output Engine Stabilization Status

The software architecture and endpoints for factory validation, physical output engine stabilization, and traceable hardware certification are implemented:
- **True 8-lane parallel RMT driver**: submits all active channels simultaneously before waiting, bounded wait timeouts (`addressable_wire_time_us + 1200us`), drains all channels on timeout, zero per-frame heap allocation.
- **Canonical Addressable limit**: exactly 256 pixels max per lane (replaces outdated 144 / 300 references).
- **PCA9685 safety**: 100 kHz standard mode I2C, init register readback verification (MODE1, MODE2, PRE_SCALE=5 / ~1017 Hz), fast fail-safe asserting `/OE HIGH` directly via GPIO without I2C transaction.
- **No catch-up bursts**: lighting scheduler detects frame deadline misses and rebases without burst cycles.
- **Microsecond commit telemetry**: `GET /api/v1/diagnostics/output` and WebSocket live streaming of addressable, PWM, total commit duration, and frame deadline misses.
- **Factory test stages**: deterministic sequenced stages (Red, Green, Blue, White, Chase, Blackout for addressable; 0%, 25%, 50%, 75%, 100%, 0% for PWM).
- **Split E-Stop certification**: `SAFE-ESTOP-LOGIC` (Automatic), `SAFE-ESTOP-PWM` (Instrument), `SAFE-ESTOP-ADDRESSABLE` (Operator). 38 tests total in Plan v1.
- `FactoryRunner` state machine (`Idle`, `Running`, `Completed`, `Aborted`, `Failed`) with time-bounded burn-in job execution.
- Dual-slot NVS persistence (`hw_cert` namespace: `cert_a` / `cert_b`) with CRC32 protection.
- Versioned certification plans (Plan v1) with `Development`, `ProductionUnit`, and `ReleaseCandidate` profiles.
- Operator console at `/dashboard/certification` and embedded summary in `dashboard.html`.

See [`docs/HARDWARE_CERTIFICATION.md`](file:///c:/Users/slavd/lumigrid/docs/HARDWARE_CERTIFICATION.md) for full certification catalog, evidence rules, and bench execution plan. Physical certification remains dependent on executing the plan on real reference hardware with oscilloscopes and meters.

