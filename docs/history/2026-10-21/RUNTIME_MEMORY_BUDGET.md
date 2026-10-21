> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-038.3 runtime memory budget

Status: software changes implemented and automated checks passed; **physical acceptance UNMEASURED**. Baseline requested: `0ead0f020aeb4bbbfe33cf971be54bec09de091d`. This work finishes the initial implementation already on `main` at `dd077a54ad859d33de3a3423b2e4ee68e8dc1f0b`.

## Product gates

Use `MALLOC_CAP_8BIT` for usable heap. Internal and DMA capability totals overlap other pools and must not be added to free8 or called general free RAM. Fragmentation ratio is diagnostic information, with no percentage acceptance threshold.

| State | Minimum free8 | Minimum largest8 | Target evidence |
| --- | ---: | ---: | --- |
| Fresh NVS, First Setup, idle after 30 seconds | 32,768 bytes | 16,384 bytes | UNMEASURED |
| Provisioned Ready, minimum topology, protocols off, idle after 60 seconds | 24,576 bytes | 12,288 bytes | UNMEASURED |
| Accepted eight-lane, 256-pixel RGBW topology | 16,384 bytes | 8,192 bytes | UNMEASURED |

Admission preserves at least 16,384 free8 bytes. These gates are unchanged. Passing compilation, linked stack checks, or a browser fixture does not satisfy them.

## Task lifecycle and stacks

| Task/service | Configured stack | Lifetime | Measured minimum free stack |
| --- | ---: | --- | --- |
| `main` | 10,240 bytes | Boot only; `app_main` returns, IDF idle cleanup reclaims the stack | UNMEASURED |
| `lighting_rt` | 10,240 bytes | Provisioned normal runtime | UNMEASURED |
| `scheduler` | 4,096 bytes | Provisioned normal runtime | UNMEASURED |
| `httpd` | 8,192 bytes | Setup, recovery and normal runtime | UNMEASURED |
| `fleet_disc` | 4,096 bytes | Connected STA only; rebind on network change | UNMEASURED |
| `lg_artnet` | 4,096 bytes | Config enabled, normal runtime | UNMEASURED |
| `lg_sacn` | 4,096 bytes | Config enabled, normal runtime | UNMEASURED |
| `sync_auto` / `sync_master` / `sync_worker` | 4,096 bytes | One configured role, connected STA | UNMEASURED |
| `lg_factory` | 8,192 bytes | First explicit Factory/Burn-In/physical Certification command; retained until stop/reboot | UNMEASURED |
| `net_apply` | 8,192 bytes | One owned pending request; task starts on acceptance and deletes itself after apply/rollback | UNMEASURED |
| MQTT | Existing IDF configuration | Enabled config with nonempty broker | UNMEASURED |

No stack was reduced. There is no replacement heartbeat or memory-monitor task. mDNS remains available. Existing persisted protocol settings are preserved; only fresh defaults are off.

The planner controls startup and reconciliation after provisioning, config changes and Wi-Fi events. Wi-Fi callbacks enqueue reconciliation on the existing HTTP worker rather than blocking the ESP event loop. A recursive service mutex serializes lifecycle changes; observable service flags and failure counters are atomic. During network apply, shutdown waits for cooperative completion before tearing down dependencies.

On the pinned `espressif32@6.7.0` / ESP-IDF 5.2.1 Xtensa port, `portSTACK_TYPE` is `uint8_t`. High-water marks are therefore **bytes**, not 32-bit words. A compile-time assertion protects this assumption. The diagnostics request takes an IDF kernel task snapshot, compares handle identities without dereferencing deleted tasks, and exposes only observed active tracked tasks. Trace support is enabled for this snapshot API; no trace recorder or periodic monitoring task is started.

Configured stack bytes are allocation sizes, not measured heap savings: TCBs, queues, allocator overhead and asynchronous idle cleanup also matter. Per-service cost measurements below remain required.

## Topology allocation

Wire buffers use 3 bytes/pixel for RGB or 4 for RGBW; diagnostic shadows use 4. The preliminary estimate adds 256 bytes/lane for encoder/channel overhead. That overhead is an engineering estimate, **not a measured IDF allocation cost**.

The candidate allocates all wire/shadow buffers and prepares its transports before touching the live topology. Existing board GPIO channels are retained and a new encoder is prepared separately, so full eight-lane replacement does not need spare RMT channels. Previous submissions must drain successfully first. After every candidate allocation, the actual free8 reserve is checked again. Failure cancels only candidate resources; it does not reconstruct the old topology, lose its pixel order, or change driver validity flags. Successful preparation swaps buffers and encoders without further allocation.

The preliminary check is conservative. A shrinking configuration can still be rejected if it cannot coexist with the live buffers. The old allocation is never counted as already-free memory. The final reserve is a real post-allocation check; preliminary overhead estimates cannot bypass it.

`/api/v1/config/validate` reports estimated remaining heap and the reserve warning. Allocation rejection uses `insufficient_runtime_memory` with `requiredBytes`, `freeBytes` and `reserveBytes`.

## Operation scratch and bounded transport

| Operation | Scratch policy | Evidence/limitation |
| --- | --- | --- |
| OTA | 2,048-byte engineering scratch allowance, 1,024-byte stream buffer; body bounded to target partition | Target peak UNMEASURED |
| Restore validation | Parsed package model + 16,384-byte record buffer + 32,768-byte parse/legacy allowance | Target peak UNMEASURED |
| Restore | Above plus full rollback model, admitted before job/mutation | Target peak UNMEASURED; intentionally rejects when aggregate scratch cannot fit |
| Certification report | 8,192-byte scratch allowance; headers and one result at a time, chunked JSON | Target peak UNMEASURED |
| Backup v4 | One record serialized and sent at a time; no whole backup JSON document | Target peak UNMEASURED |
| Ordinary JSON request | Explicit 8,192-byte maximum; body allocation is actual bounded length + terminator | Depth bounded to 32; shape-derived cJSON tree budget preserves reserve before parsing |

For the pinned factory ELF, debug type sizes are `DeviceConfig=1320`, `ParsedBackupPackage=37836`, `RestoreRollbackSnapshot=37548` bytes. Restore admission therefore requests 124,536 scratch bytes before its reserve; validation requests 86,988. These are real costs of the existing typed package/rollback design, not values to hide behind a smaller threshold. This design can safely reject restores on memory-limited configurations. A storage-backed or sparse rollback redesign would be separate work; this batch does not allocate an entire raw `.lgbak` or alter partitions to disguise that cost.

Restore input retains the 16,384-byte individual-record limit and adds an explicit 1 MiB total request limit. Final-byte record overflow and repeated socket timeouts fail before configuration mutation. Legacy v3 is bounded separately to 32,768 bytes. The JSON lexer counts object members and array values outside quoted strings, bounds nesting, and admits the node/string/allocator budget before cJSON expansion. Scratch allowances are engineering estimates until target measurement; they are not hardware acceptance evidence.

WebSocket preview schema 2 has a four-byte header `[schema, lanes, samplesPerLane, reserved]` followed by at most `8 * 64 * 3` RGB bytes: **1,540 bytes total**. Sampling includes both ends of the configured strip, including 256-pixel lanes. Both dashboards decode the header, reject malformed/oversized frames, and display preview samples. Historical 144-pixel frames are resampled for compatibility.

Preview owns one bounded queued allocation; its pending flag stays set until sending finishes. Status keeps its existing coalescing flag. Client enumeration uses fixed bounded fd arrays. No frame-sized vector grows with physical topology. API/preview/RMT allocation failures and Factory/network task start failures are observable counters.

Burn-in captures start, minimum and end free8/largest8. End values are captured when the job ends, not at report download time. These additional samples are session-local; the old persisted certification record format is unchanged.

## Measurement record and required physical acceptance

All rows below are **UNMEASURED** for this change. The previously observed approximately 4.7 KiB idle heap is historical context, not a new measurement.

| Measurement | Required evidence | Result |
| --- | --- | --- |
| First Setup idle | Fresh NVS, 30-second settle, task/service snapshot, free8/largest8/internal/DMA/minimum | UNMEASURED |
| Normal minimal Ready | Provisioned, protocols off, 60-second settle, same metrics | UNMEASURED |
| Service costs | Before/after each protocol, fleet, Factory, Sync/MQTT and network apply | UNMEASURED |
| Topology progression | One lane 144 RGB; eight lanes 144 RGB; eight lanes 256 RGB; eight lanes 256 RGBW; admission failures preserve old topology | UNMEASURED |
| Concurrent load | Protocol traffic + WebSocket + output load; task high-water minima, failures, reserve | UNMEASURED |
| Heavy operations | Low-memory rejection before mutation; OTA/restore/report/backup peak scratch and fragmentation | UNMEASURED |
| Active WebSocket soak | 30 minutes, reconnect/disconnect cycles, no monotonic free8/largest8 loss | UNMEASURED |
| Burn-in soak | 30 minutes, start/min/end memory, task minima, output faults/deadlines/reset evidence | UNMEASURED |

On 2026-10-06, serial enumeration found CH340 on COM3 and Bluetooth COM8/COM9; the project's saved COM4 was absent. No board was flashed, reset, provisioned, or physically accepted during the software checks. Confirm controller identity and preserve existing configuration before destructive fresh-NVS testing.

Record firmware commit/image checksum, physical controller identity, topology, power conditions, network mode and enabled services with every measurement. Reject an unmeasured maximum topology as an acceptance claim even if the firmware admits it in source.

## Automated validation, 2026-10-06

- Factory and test firmware images compile with pinned ESP-IDF 5.2.1. Target assertions in the test image have **not been executed** on a controller.
- Certification architectural invariants pass. Boot config tests: 8 passed; boot stack tests: 7 passed; host runtime math checks pass. These are source/model checks, not target heap or RMT proof.
- Frontend `npm run check`: zero errors/warnings; 385 unit tests pass; production build passes. The initial Windows adapter symlink failure was resolved using the repository's existing `build:local-windows` junction fallback before the final production build.
- Six first-setup Playwright tests pass against the generated, minified embedded asset. Both embedded headers use the canonical Node generator and contain identical 28,200-byte gzip payloads.
- Factory image retains the existing 1,310,720-byte application partition. Flash margin is 3,255 bytes (about 3.2 KiB); linked static RAM usage is 143,300 bytes. Static linker totals are **not runtime free-heap measurements**.
- `git diff --check` passes. No stacks were reduced and no partition layout was changed.

LG-038.3 acceptance remains open: runtime heap thresholds, actual firmware assertions, service allocation deltas, loaded maximum topology and both 30-minute soaks require physical execution. Restore's large existing model/rollback scratch demand remains a documented operational limitation; admission can reject it safely without changing configuration.

Factory `firmware.bin` SHA-256: `F0760DAEF77C5E183C1A64B5AAEDBEF215DAB6B46B39E5ABFEB058A9821CF8F5`. This identifies the locally built image; it is not a flashed-board measurement.
