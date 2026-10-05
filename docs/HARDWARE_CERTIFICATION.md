# LumiGrid Hardware Certification Specification & Bench Execution Plan (LG-037)

> **Status**: Hardware certification workflow implemented.
> **Baseline**: `main @ 80617e5d407ba0eb9474df8013071f99cfe4d918`
> **Important Notice**: Physical certification remains dependent on executing the plan on real reference hardware. A code commit cannot certify a PCB sitting on a bench.

---

## 1. Principles & Objectives

The purpose of LG-037 is to create the bridge between:
```text
software passes tests
```
and:
```text
this specific physical LumiGrid unit
was actually tested
against a known certification plan
with traceable evidence
```

### Core Axiom
```text
command accepted != hardware verified
```

- Firmware automatically verifies internal software state, NVS integrity, memory allocations, and network connectivity.
- Physical wiring, visual illumination, PWM duty, power clamp thresholds, and output timing require **operator observation** or **instrument measurement**.
- Firmware and browser APIs reject automated fake-passes for physical tests.

---

## 2. Evidence Sources

Every certification test definition explicitly specifies its required evidence source. The browser cannot downgrade this requirement:

1. **`AUTOMATIC` (`automatic`)**:
   - Firmware directly verifies internal state (e.g., API responds, NVS checksum valid, recovery state asserted).
2. **`OPERATOR OBSERVATION` (`operator_observation`)**:
   - Requires physical human inspection (e.g., A4 illuminates exclusively, RGB color order correct, chase pattern completes across full length, zero flicker, E-stop blackouts physical outputs).
3. **`INSTRUMENT REQUIRED` (`instrument_measurement`)**:
   - Requires measurement with calibrated bench equipment (e.g., oscilloscope duty cycle, calibrated DC power meter / current shunt, logic analyzer multi-node skew).
   - Numerical value and measurement unit (`percent`, `mW`, `us`) are mandatory. Firmware plan enforces pass/fail tolerances.

---

## 3. Certification Profiles

| Profile | Target | Required Scope |
| :--- | :--- | :--- |
| **`development`** | Fast bench bring-up | Boot, basic A/P outputs, safety, recovery, network. Serial number optional. |
| **`production_unit`** | Manufactured unit qualification | All A1–A8, all P1–P8, crosstalk, safety, recovery, power limit, 15–30 min burn-in. **Non-empty serial number & valid hardware revision mandatory.** |
| **`release_candidate`** | Firmware/hardware release validation | Full matrix: DMX, backup/restore, OTA rollback, extended burn-in (1h/8h/24h/72h), multi-node physical timing skew with oscilloscope/logic analyzer. |

---

## 4. Test Catalog & Invariant Categories (Plan v1)

| Test ID | Category | Evidence Source | Dev | Prod | RC | Description / Requirement |
| :--- | :--- | :---: | :---: | :---: | :---: | :--- |
| `BOOT-001` | BOOT | `AUTOMATIC` | Req | Req | Req | Boot sequence & immutable node identity assertion |
| `BOOT-OUTPUT-SAFE` | BOOT | `OPERATOR OBSERVATION` | Req | Req | Req | Power-on output inhibit: no flash/glitch before runtime |
| `BOOT-REBOOT-001` | BOOT | `AUTOMATIC` | Req | Req | Req | Reboot configuration persistence across warm resets |
| `BOOT-SAFE-001` | BOOT | `AUTOMATIC` | Opt | Opt | Req | Safe Mode boot & recovery partition isolation |
| `NET-AP-001` | NETWORK | `AUTOMATIC` | Req | Req | Req | Access Point reachability, DHCP server, HTTP API |
| `NET-STA-001` | NETWORK | `AUTOMATIC` | Opt | Req | Req | Station association, DHCP acquisition & mDNS discovery |
| `NET-RECONNECT-001`| NETWORK | `AUTOMATIC` | Opt | Req | Req | Network disconnect/reconnect resilience without reboot |
| `NET-DHCP-001` | NETWORK | `AUTOMATIC` | Opt | Opt | Req | DHCP route reconciliation & lease renewal |
| `ADDR-A1`..`ADDR-A8`| ADDRESSABLE | `OPERATOR OBSERVATION` | Req | Req | Req | Independent port tests: Red → Green → Blue → White → Chase → Blackout |
| `ADDR-XTALK` | ADDRESSABLE | `OPERATOR OBSERVATION` | Opt | Req | Req | Crosstalk isolation: active A1 with A2–A8 fully off |
| `PWM-P1`..`PWM-P8` | PWM | `OPERATOR OBSERVATION` | Req | Req | Req | Stepped duty (0% → 25% → 50% → 75% → 100% cap → 0%) |
| `PWM-XTALK` | PWM | `OPERATOR OBSERVATION` | Opt | Req | Req | PWM crosstalk isolation & neighboring pin silence |
| `SAFE-ESTOP-001` | SAFETY | `OPERATOR OBSERVATION` | Req | Req | Req | Hardware E-stop trip: immediate blackout & latched inhibit |
| `SAFE-DISABLE-001`| SAFETY | `OPERATOR OBSERVATION` | Req | Req | Req | Output disable / OE pin shutdown verification |
| `SAFE-WATCHDOG-001`| SAFETY | `AUTOMATIC` | Opt | Opt | Req | Live control source loss fallback trigger |
| `POWER-LIMIT-001` | POWER | `INSTRUMENT REQUIRED` | Opt | Req | Req | Calibrated bench PSU current shunt: measured mW within tolerance |
| `DMX-ARTNET-001` | DMX | `AUTOMATIC` | Opt | Opt | Req | Art-Net 4 universe reception & port routing |
| `DMX-SACN-001` | DMX | `AUTOMATIC` | Opt | Opt | Req | ANSI E1.31 sACN reception & priority arbitration |
| `REC-BACKUP-001` | RECOVERY | `AUTOMATIC` | Opt | Req | Req | Streaming `.lgbak` export & secret exclusion verification |
| `REC-RESTORE-001` | RECOVERY | `AUTOMATIC` | Opt | Opt | Req | Full restore preflight validation and reboot apply |
| `REC-INTERRUPT-001`| RECOVERY | `AUTOMATIC` | Opt | Opt | Req | Interrupted restore recovery flag survival & AP fallback |
| `OTA-GOOD-001` | OTA | `AUTOMATIC` | Opt | Opt | Req | Valid OTA candidate flash, reboot, confirmation |
| `OTA-ROLLBACK-001`| OTA | `AUTOMATIC` | Opt | Opt | Req | Invalid image rollback to previous active partition |
| `BURN-001` | BURN-IN | `AUTOMATIC` | Opt | Req | Req | Continuous burn-in job (15m–30m prod, 1h–72h RC) |
| `API-001` | API | `AUTOMATIC` | Req | Req | Req | Canonical HTTP `/api/v1` contract conformance |
| `WS-001` | API | `AUTOMATIC` | Req | Req | Req | WebSocket Schema 3 telemetry stream monotonicity |
| `SYNC-ID-001` | MULTI-NODE | `AUTOMATIC` | Opt | Opt | Req | Discovery & distinct immutable node IDs across fleet |
| `SYNC-LEADER-001` | MULTI-NODE | `AUTOMATIC` | Opt | Opt | Req | Leader election stability & follower lock |
| `SYNC-SCENE-001` | MULTI-NODE | `OPERATOR OBSERVATION` | Opt | Opt | Req | Visually synchronized scene transition |
| `SYNC-TIMING-001` | MULTI-NODE | `INSTRUMENT REQUIRED` | Opt | Opt | Req | Oscilloscope/logic analyzer inter-node optical skew (<5 ms) |

---

## 5. Hardware Fingerprinting & Staleness Rules

Certification results become **STALE** when underlying hardware-relevant reality changes:
- Firmware version upgraded (`1.2.0` → `1.3.0`)
- Hardware revision changed
- Plan version incremented
- Addressable channel strip length, pixel order, or chip type modified
- PWM output allocation modified
- Power limiter wattage changed
- DMX universe patch changed

> **Non-Invalidating Changes**: Scene changes, Cue list edits, Timeline edits, and display name changes do **NOT** invalidate hardware qualification.

---

## 6. Execution Protocol & Safety Gates

1. **Exclusivity**: Only one certification session can be active per controller.
2. **FactoryRunner Integration**: Physical output tests are commanded through `FactoryRunner` with owner tagged as `Certification`. Brightness caps (LG-035) remain enforced.
3. **Safe Timeouts**: Physical tests timeout and automatically blackout after a bounded duration (default: 10s for channel tests; 5 minutes maximum for operator evaluation). No LED output is left on indefinitely.
4. **Failure Auditing**: A test failure requires a mandatory reason category (`no_output`, `wrong_port`, `wrong_color_order`, `partial_strip`, `flicker`, `crosstalk`, `unstable`, `other`) and descriptive note. Attempt counters are monotonically incremented.
5. **Persistence**: Dual-slot NVS storage (`cert_a` / `cert_b` in `hw_cert` namespace) with CRC32 protection guarantees survival across reboots.

---

## 7. Recommended First Real Bench Plan

Upon deploying this firmware to bench units, execute tests in this sequence:
1. `BOOT-OUTPUT-SAFE`
2. `ADDR-A1`
3. `ADDR-A8`
4. `PWM-P1`
5. `PWM-P8`
6. `SAFE-ESTOP-001`
7. Complete all `ADDR-A1`..`ADDR-A8`
8. Complete all `PWM-P1`..`PWM-P8`
9. `ADDR-XTALK` & `PWM-XTALK`
10. `NET-RECONNECT-001`
11. `REC-BACKUP-001`
12. Reboot verification
13. `BURN-001` (15-minute qualification)
