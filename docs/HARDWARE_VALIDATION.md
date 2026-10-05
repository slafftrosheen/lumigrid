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
| **HW-A1A8-01** | Addressable | A1–A8 WS2812 / SK6812 signal integrity | 8x LED strips attached to RMT output pins | 800 kHz bit timing, clean rise/fall times (<50ns), zero flicker or frame drop at 40 FPS | `[PENDING BENCH]` |
| **HW-P1P8-01** | PWM | P1–P8 PCA9685 I2C output drive | 8x low-voltage LED loads / MOSFET gates | 1 kHz PWM waveform, linear duty cycle response (0–100%), no I2C bus lockup | `[PENDING BENCH]` |
| **HW-ESTOP-01** | Safety | Physical E-Stop / blackout trip | External emergency stop switch on GPIO | Outputs blackout within <5 ms of E-Stop pin trip; latched state prevents re-enable | `[PENDING BENCH]` |
| **HW-PWR-01** | Safety / Power | Dynamic power limiter | DC lab power supply with current shunt | Total system power draw clamped to configured `max_power_mw` without reboot | `[PENDING BENCH]` |
| **HW-THERM-01**| Thermal | 24-hour continuous burn-in at full load | Thermal chamber or sealed enclosure | Board temperature remains within safe operating envelope; no thermal throttling crashes | `[PENDING BENCH]` |
| **HW-SYNC-01** | Fleet Sync | Multi-node optical synchronization | 3 physical nodes with photodiodes on oscilloscope | Frame start optical alignment within <15 ms across nodes running UDP NodeSync | `[PENDING BENCH]` |
| **HW-RESET-01**| Recovery | Physical GPIO0 factory reset | Physical button hold for 5 seconds | Blackout, full NVS erase, wait for button release, clean reboot into unprovisioned AP | `[PENDING BENCH]` |
| **HW-OTA-01**  | Recovery / OTA | Bad firmware image automatic rollback | Physical node flashed with fault-inducing candidate | Bootloader detects unconfirmed candidate failure and rolls back to previous partition | `[PENDING BENCH]` |

---

## 3. LG-035 & LG-037 Baseline Status

The software architecture and endpoints for factory validation and traceable hardware certification are implemented:
- `FactoryRunner` state machine (`Idle`, `Running`, `Completed`, `Aborted`, `Failed`)
- Addressable and PWM lane test patterns (solid, chase, cycle, full)
- Time-bounded burn-in job execution
- `CertificationService` dedicated domain for traceable evidence collection (LG-037)
- Dual-slot NVS persistence (`hw_cert` namespace: `cert_a` / `cert_b`)
- Versioned certification plans (Plan v1) with `Development`, `ProductionUnit`, and `ReleaseCandidate` profiles
- Three-tier evidence requirements: `AUTOMATIC`, `OPERATOR OBSERVATION`, and `INSTRUMENT REQUIRED`
- Anti-spoofing policy: browser cannot forge physical test passes; factory safety caps enforced
- Operator console at `/dashboard/certification` and embedded summary in `dashboard.html`

See [`docs/HARDWARE_CERTIFICATION.md`](file:///c:/Users/slavd/lumigrid/docs/HARDWARE_CERTIFICATION.md) for full certification catalog, evidence rules, and bench execution plan. Physical certification remains dependent on executing the plan on real reference hardware.
