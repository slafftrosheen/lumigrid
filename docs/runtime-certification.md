# LumiGrid Runtime Certification Status

> **Target Base Commit**: `ea586e39aca19df0d051e2534cf336c73316f582`
> **Date**: October 2026
> **Scope**: Certification Hardening, Lifecycle Integrity, Concurrency Safety, Runtime Snapshot Authority, and Verification Status.
> **Certification Stage**: Pre-Hardware Verification (Architecture & Simulation Complete). Full certification is **NOT** claimed until physical hardware milestones are completed.

---

## 1. Runtime Invariant Verification Matrix

| Category | Invariant Description | Status | Verification Level |
| :--- | :--- | :---: | :--- |
| **Lifecycle** | Transactional `start()`: Synchronous initialization with sequential rollback on failure | `[PASS]` | Compile Verified, Integration |
| **Lifecycle** | Zero `ESP_ERROR_CHECK()` in runtime startup and infrastructure paths | `[PASS]` | Compile Verified, Static CI |
| **Lifecycle** | Cooperative `stop()`: Task observes shutdown signal and completes before state destruction | `[PASS]` | Compile Verified, Integration |
| **Lifecycle** | Double-start rejection with `ESP_ERR_INVALID_STATE` and deterministic repeated stops | `[PASS]` | Compile Verified, Unit Test |
| **Lifecycle** | Instance-scoped `services_started_` (zero process-global static state) | `[PASS]` | Compile Verified, Unit Test |
| **Factory** | Transactional startup: queue, mutex, and task allocations rollback on partial failure | `[PASS]` | Compile Verified, Unit Test |
| **Factory** | Serialized command transition pipeline (`validate -> state -> ownership -> output`) | `[PASS]` | Compile Verified, Integration |
| **Factory** | Strict decoupling: Factory Abort releases ownership and blacks out WITHOUT activating E-Stop | `[PASS]` | Compile Verified, Unit Test |
| **Output** | Scoped Output Transaction API (`begin_transaction` / `commit`) with RAII auto-unlock | `[PASS]` | Compile Verified, Unit Test |
| **Output** | Unified commit fault owner: physical commit failure drives `Healthy -> Faulted -> Armed -> Healthy` | `[PASS]` | Compile Verified, Integration |
| **Protocols** | Deterministic LTP tie-breaking on same-millisecond arrivals via monotonic `arrival_seq` | `[PASS]` | Compile Verified, Unit Test |
| **Protocols** | Safe timestamp wraparound comparison using bounded `elapsed_ms(now, then)` | `[PASS]` | Compile Verified, Simulated |
| **Protocols** | Strict 4-source bounded capacity policy (5th rejected, live sources never overwritten) | `[PASS]` | Compile Verified, Unit Test |
| **Protocols** | DMX universe timeout without packets via `DmxRouter::tick()` maintenance loop | `[PASS]` | Compile Verified, Integration |
| **Protocols** | Single authoritative universe patch collector: `Art-Net == sACN == DmxRouter` | `[PASS]` | Compile Verified, Unit Test |
| **Protocols** | Art-Net 4 15-bit Port-Address serialization (NetSwitch @ 18, SubSwitch @ 19, SwOut @ 190..193) | `[PASS]` | Compile Verified, Unit Test |
| **Observability** | Purely observational `/api/v1/health`: zero NVS reads/writes, zero hardware operations | `[PASS]` | Compile Verified, Unit Test |
| **Observability** | Real-time diagnostic startup state (`startup_state`, `startup_stage`, `failing_stage`, `failing_error`) | `[PASS]` | Compile Verified, Integration |
| **Security** | Production credentials policy: non-universal AP recovery credential (no static `"lumigrid123"`) | `[PASS]` | Compile Verified, Static CI |
| **Hardware** | Factory recovery button: 3s grace period, 20ms debounce, stuck pin protection, safe blackout | `[PASS]` | Compile Verified, Simulated |
| **Transport** | Show Transport v2 explicit state machine: Scene, Timeline, Cue preemption with generation tracking | `[PASS]` | Compile Verified, Unit Test |
| **Transport** | NodeSync protocol v4 (60-byte packed frame): leader-authoritative cues, remote BPM, clock sync | `[PASS]` | Compile Verified, Unit Test |
| **Transport** | Follower local playback suppression and 409 `sync_follower_read_only` guard | `[PASS]` | Compile Verified, Unit Test |
| **Parity** | Manifest schema 2: separation of playback parity (`playbackHash`) from optional schedule parity | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Deterministic CRC calculation: field-by-field hashing in defined order eliminating padding artifacts | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Dual-slot NVS storage (`network`/`network_b`) with CRC verification and self-healing backup recovery | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Strict input validation: SSID (1..32), WPA (8..63/open), channel (1..13, reject 14), mode, and types | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Asynchronous network apply lifecycle via `queue_network_apply` with 409 concurrency lock | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Transactional rollback: failed apply restores previous configuration without overwriting NVS | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Bounded service rebind (MQTT, NodeSync, sACN, Art-Net, mDNS, API) with zero lighting blackout | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Intentional vs accidental disconnect separation: `intentional_disconnect_` prevents reconnect loops | `[PASS]` | Compile Verified, Unit Test |
| **Network** | `StaWithApFallback` bounded backoff retry (`5s -> 10s -> 30s -> 60s`) while keeping recovery AP active | `[PASS]` | Compile Verified, Unit Test |
| **Network** | Safe-mode recovery invariant: non-recovery network apply rejected with `safe_mode_network_locked` | `[PASS]` | Compile Verified, Unit Test |
| **Security** | Write-only credentials: passwords never logged and never exposed over GET APIs (`passwordSet: true` only) | `[PASS]` | Compile Verified, Unit Test |
| **Auth (LG-029)** | Node-backed authentication authority: zero universal fallback tokens (`lumigrid_admin`, `DEFAULT_OP_PIN`) | `[PASS]` | Compile Verified, Static CI, Unit Test |
| **Auth (LG-029)** | Dedicated `AuthStore` in dual-slot NVS (`node_auth` namespace): salt + SHA-256 digest, constant-time compare | `[PASS]` | Compile Verified, Unit Test |
| **Auth (LG-029)** | One-time bootstrap commissioning (`POST /api/v1/auth/bootstrap`, 16-64 chars) unavailable once provisioned | `[PASS]` | Compile Verified, Unit Test |
| **Auth (LG-029)** | RAM-only ephemeral session table: 8 sessions max, 256-bit entropy, 30 min idle / 8 hr absolute expiry | `[PASS]` | Compile Verified, Unit Test |
| **Auth (LG-029)** | WebSocket pre-handshake ticket authentication (`Sec-WebSocket-Protocol: lumigrid-ticket.<ticket>`) | `[PASS]` | Compile Verified, Unit Test |
| **Auth (LG-029)** | Explicit separation: `Authorization: Bearer <session>` for browser, `X-API-Key` for automation; query tokens rejected | `[PASS]` | Compile Verified, Static CI, Unit Test |
| **Auth (LG-029)** | Backup schema v3: zero authentication tokens, salts, or hashes in backup; restore v2 ignores legacy secrets | `[PASS]` | Compile Verified, Static CI, Unit Test |
| **Auth (LG-029)** | Per-node browser session state in memory and `sessionStorage` (zero tokens in `localStorage`) | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Side-effect free `RuntimeSnapshot` collection (zero command journal mutations or lifecycle side effects) | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Authoritative single runtime-state contract: Schema 3 shared identically by `GET /api/v1/status` and `/ws` | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Bounded 8192-byte serialization budget with `FixedJsonWriter` failure-awareness and overflow truncation protection | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Coalesced status broadcasts via atomic `status_pending_` CAS loop at max 5 Hz rate limit | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Instance-scoped timing atomics `last_preview_ms_` and `last_status_broadcast_ms_` (zero process-global timing statics) | `[PASS]` | Compile Verified, Unit Test |
| **State (LG-031)** | Decoupled heavy payload: DMX raw frame isolated behind dedicated `GET /api/v1/dmx/frame` (Installer+ role) | `[PASS]` | Compile Verified, Unit Test |
| **Scenes (LG-032)** | Safe scene persistence invariant: saving preset does not alter live physical outputs until explicit activation | `[PASS]` | Compile Verified, Unit Test |
| **Scenes (LG-032)** | CRC32 optimistic concurrency for SceneDocument saves returning 409 Conflict on concurrent modifications | `[PASS]` | Compile Verified, Unit Test |
| **Scenes (LG-032)** | Preflight scene deletion reference check blocking removal when preset is referenced in Timeline or Cues | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Separation of timeline persistence from transport: `PUT /sequence` never mutates running playback | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Deprecated `Sequence.enabled` ignored for ordinary playback; legacy `enabled:true` cannot auto-run on reboot | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Immutable active sequence snapshot on RUN with deterministic Beat 0 start anchor clock (`timeline_anchor_ms`) | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Monotonic transport generation incremented on every RUN/restart; same generation does not reset anchor | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | NodeSync protocol v5 transmitting 68-byte packed packets with generation, anchor, and checksum | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Follower timeline checksum validation with fail-closed rejection on mismatch (`SyncTransportError::TimelineMismatch`) | `[PASS]` | Compile Verified, Unit Test |
| **Timeline (LG-033)** | Pure TimelineEvaluator helpers guaranteeing identical beat and step progression between firmware and dashboard | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Single-lock `CueRunner::snapshot(now)` is the only sanctioned reader; zero unlocked public state accessors | `[PASS]` | Compile Verified, Static CI, Unit Test |
| **Cues (LG-034)** | Deadlock-free stop path: `stop()` never blocks on the runner mutex while `update()` executes; stop reason reported via event callback | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Typed stop reasons (`operator`, `preempted`, `natural_completion`, `sync_lost`); end action executes only on natural completion | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Pause freezes elapsed/remaining anchors and the in-flight fade (`SceneManager::freeze_transition`); resume continues the frozen transition without visual restart | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Immutable active Cue List snapshot on RUN: saving while running never mutates active playback; divergence reported as `contentStale` | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | `restart` adopts the newest saved list with a fresh generation and never executes an end action | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Strict schema-2 validation: UTF-8 byte caps (name 31 / label 23), 1..16 cues, bounded timing, missing scene rejected with 400 `cue_scene_missing` | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Optimistic concurrency: stale `expectedChecksum` fails with 409 `cue_list_conflict` + `currentChecksum` | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Dependency-safe deletion: 409 `cue_list_in_use` with exact references while active as transport or referenced by schedules | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Deterministic cue transport via `/api/v1/show/control`: invalid-state moves return 400/409, never silent no-ops | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | NodeSync protocol v6: 76-byte packed frame with cue list checksum, transition progress, paused-from phase, and source scene | `[PASS]` | Compile Verified, Unit Test |
| **Cues (LG-034)** | Follower cue content parity gate: missing list fails with `cue_list_missing`, divergent list fails closed with `SyncTransportError::CueListMismatch` | `[PASS]` | Compile Verified, Unit Test |
| **Factory (LG-035)** | Software state machine & runner for automated bench validation (A1–A8, P1–P8, visual test patterns, burn-in) | `[PASS]` | Compile Verified, Unit Test |
| **Factory (LG-035)** | Abort factory test cleanly transitions output ownership to blackout without ungraceful crash | `[PASS]` | Compile Verified, Unit Test |
| **Security (LG-036)** | Elimination of universal recovery credentials (`lumigrid123` removed from production builds) | `[PASS]` | Static Invariant, CI |
| **Security (LG-036)** | Destructive factory reset is physical-only (GPIO0 hold); zero HTTP factory reset endpoints | `[PASS]` | Architecture Invariant, Static CI |
| **Recovery (LG-036)** | Explicit `RecoveryStatus` model (`GET /api/v1/recovery/status`) with fail-safe Safe Mode | `[PASS]` | Compile Verified, Unit Test |
| **Config (LG-036)** | Authoritative CRC checksum contract on `DeviceConfig` with optimistic concurrency (`409 Conflict`) | `[PASS]` | Compile Verified, Unit Test |
| **Config (LG-036)** | Strict fail-closed nested bounds validation (reject >16 channels, >8 groups, invalid enums) | `[PASS]` | Compile Verified, Unit Test |
| **Config (LG-036)** | Runtime `ConfigApplyPlan` transaction: driver reconfig with blackout, service restart rollback | `[PASS]` | Compile Verified, Unit Test |
| **Backup (LG-036)** | Streaming Recovery Package v4 (`.lgbak`) excluding credentials/salts/Wi-Fi/sync tokens | `[PASS]` | Compile Verified, Unit Test |
| **Restore (LG-036)** | Preflight validation (`POST /restore/validate`) and transactional restore with NVS marker and rollback | `[PASS]` | Compile Verified, Unit Test |
| **OTA (LG-036)** | Bootloader application rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) in production | `[PASS]` | Build Config, Invariant |
| **OTA (LG-036)** | OTA descriptor validation, candidate self-test in `OtaBootVerifier`, and automatic rollback on fault | `[PASS]` | Compile Verified, Unit Test |

---

## 2. Invariant Detail & Verification Results

### 2.1 Lifecycle & Shutdown Safety
- **Synchronous Lifecycle**: `LumigridRuntime::start()` executes synchronous network wait, API startup, mDNS discovery, protocol listeners (Art-Net/sACN), and background services sequentially. Any failure halts progression, executes reverse-order teardown via `rollback_services()`, blackouts outputs, enters `Faulted` state, and returns the error code without crashing the MCU.
- **Cooperative Shutdown**: `LightingRuntime::stop()` sets `shutting_down_ = true`, aborts blocking delays, waits for the task to complete via `shutdown_sem_`, and only then safely deletes runtime-owned synchronization primitives.
- **No Double-Start**: Both `LumigridRuntime` and `LightingRuntime` reject repeated `start()` invocations with `ESP_ERR_INVALID_STATE`. Multiple calls to `stop()` are idempotent.

### 2.2 Protocol Correctness & Serialization
- **Art-Net 4 Spec Adherence**: ArtPollReply serialization strictly formats 15-bit Port-Addresses:
  - Universe 1: `Net=0, SubNet=0, SwOut[0]=1`
  - Universe 16: `Net=0, SubNet=1, SwOut[0]=0`
  - Universe 255: `Net=0, SubNet=15, SwOut[0]=15`
  - Universe 512: `Net=2, SubNet=0, SwOut[0]=0`
- **Deterministic LTP**: Packet arrival sequencing assigns a monotonic 64-bit `arrival_seq` to every incoming packet, breaking timestamp ties deterministically when packets arrive within the same millisecond.
- **Bounded Capacity**: Bounded at 4 concurrent active sources per universe. A 5th source is rejected and increments `sources_dropped` without evicting active transmitters.

### 2.3 Output & Safety Architecture
- **Output Transaction RAII**: All frame staging occurs inside `OutputTransaction` objects acquiring the hardware lock once. Staging methods (`pixels()`, `pwm()`) are scoped to the transaction, and `commit()` invokes the physical driver commit pipeline.
- **Unified Fault State Transition**: Physical commit failures immediately trigger the `CommitObserver`, advancing `LightingRuntime` output state to `Faulted` and recording distinct metrics for failed commits and fault events.

### 2.4 Show Transport v2 & NodeSync v4
- **Single Explicit Transport Coordinator**: `ShowTransportCoordinator` models explicit Scene, Timeline, and Cue List playback, eliminating hidden competing playback sources.
- **NodeSync Protocol v4**: Transmits packed 60-byte UDP packets with leader-authoritative cue lists, random progression, hold/fade duration, and manual crossfade progress.
- **Follower Guard**: Active followers reject direct playback activations with HTTP 409 `sync_follower_read_only` while permitting show asset editing.

### 2.5 Network Commissioning & Live Network Lifecycle (LG-028)
- **Deterministic Checksum**: `calc_network_settings_crc()` computes CRC32 by feeding explicit fields in fixed order (`schema_version`, `mode`, `sta_ssid`, `sta_password`, `ap_name`, `ap_channel`) into `esp_rom_crc32_le()`, preventing compiler padding and checksum fields from causing false corruption detections.
- **Dual-Slot Persistence with Self-Healing**: `ConfigStore` reads primary slot `"network"` with strict `schema_version == 1` enforcement. Upon CRC failure or version mismatch, it falls back to `"network_b"` and heals the primary slot automatically.
- **Strict Input Validation**: Endpoints enforce RFC/Wi-Fi constraints: SSIDs must be 1..32 bytes (required for STA modes), passwords 8..63 bytes (or 0 for open networks), AP channels strictly 1..13 (channel 14 rejected with 400 `invalid_ap_channel`), and strict JSON types.
- **Asynchronous Apply Lifecycle**: `PUT /api/v1/network/config` validates input, journals command ID, enqueues request via `queue_network_apply()`, and responds with `202 Accepted` (`{"accepted":true,"commandId":N,"applyPending":true}`). Concurrent apply requests return `409 Conflict`.
- **Transactional Rollback**: The runtime-owned `net_apply_worker` holds both previous and desired settings. If the desired network cannot become ready within the timeout, it restores previous runtime settings without modifying persisted NVS. Configuration is persisted only upon verified network readiness.
- **Zero Blackout Network Service Rebind**: When switching networks, network-dependent services (MQTT, NodeSync, sACN, Art-Net, mDNS, API) are stopped and rebound sequentially. `LightingRuntime`, `OutputController`, DMX routing, and active hardware outputs remain uninterrupted throughout.
- **Intentional Disconnect Handling**: `WifiService` maintains `intentional_disconnect_` (`std::atomic<bool>`) so administrative reconfigurations do not trigger the automatic reconnect retry loop.
- **StaWithApFallback Lifecycle**: If STA fails to connect under `StaWithApFallback`, the recovery AP is activated for operator access, and a FreeRTOS timer performs bounded exponential backoff retries (`5s -> 10s -> 30s -> 60s -> capped at 60s`) until STA connects.
- **Safe-Mode Recovery Guard**: When `safe_mode_` is active, non-recovery network reconfigurations are rejected with `400 safe_mode_network_locked`.

### 2.6 Canonical Runtime State & API v1 Cutover (LG-031)
- **Side-Effect Free Collection**: `collect_runtime_snapshot` reads atomic state and acquires minimal locks purely for observation. Zero state transitions, zero command journal mutations, and zero hardware operations occur during telemetry collection or serialization.
- **Authoritative Single State Contract**: Schema 3 `RuntimeSnapshot` is shared identically between HTTP `GET /api/v1/status` and authenticated WebSocket (`/ws`) broadcasts. Both channels deliver bit-compatible JSON payloads with identical field hierarchy (`system`, `output`, `dmx`, `network`, `transport`, `hardware`).
- **Bounded Buffer Safety**: Serialization uses `FixedJsonWriter` within a strict 8192-byte stack/heap budget. Overflow detection truncates safely with failure-awareness rather than causing buffer overflows or unbounded dynamic allocations.
- **Coalesced Status Broadcasting**: Status changes trigger WebSocket broadcasts through an atomic `status_pending_` CAS loop, rate-limited to a maximum of 5 Hz (200 ms). Preview frames are transmitted at up to 30 Hz.
- **Zero Process-Global Statics**: Rate limiting and timing tracking use instance-scoped `std::atomic<uint32_t>` members (`last_preview_ms_`, `last_status_broadcast_ms_`) in `FactoryApi`, guaranteeing multi-instance thread safety.
- **DMX Payload Decoupling**: Static capabilities (`CapabilitiesManifest`) are removed from live status to eliminate bandwidth waste, and raw DMX channel frames are decoupled into a dedicated `GET /api/v1/dmx/frame` endpoint restricted to `Installer+` role.

### 2.7 Scene Studio v2 & Safe Persistence (LG-032)
- **Safe Persistence Invariant**: Modifying or saving a scene preset via `PUT /api/v1/presets` persists data strictly to NVS. The active physical output remains completely unaltered until an explicit activation request (`POST /api/v1/show/control` or `POST /api/v1/activate`) is issued.
- **Optimistic Concurrency & Conflict Protection**: Scene saves accept `expectedChecksum`. If the stored scene CRC32 differs, the endpoint responds with `409 Conflict` containing `currentChecksum`, preventing silent clobbering of edits.
- **Dependency-Protected Deletion**: `DELETE /api/v1/presets?index=N` checks whether the target preset is in use by any step in the sequence or any cue in the cue lists. Active dependencies abort deletion with `400 Bad Request` and detailed reference metadata.

### 2.8 Timeline Studio v2 — Anchored Transport & Safe Playback (LG-033)
- **Persistence / Playback Decoupling**: `PUT /api/v1/sequence` persists validated Schema 2 Timeline documents. It does not start, stop, or mutate active playback, active sequence snapshots, or anchor timestamps.
- **Immutable Active Sequence Snapshot**: Triggering RUN (`POST /api/v1/show/control` with `mode: "timeline"`) copies the latest persisted sequence into `active_sequence_`. Subsequent saves while running modify persisted storage but do NOT alter `active_sequence_`.
- **Anchored Deterministic Timing**: Playback starts deterministically at Beat 0 from an explicit anchor timestamp (`timeline_anchor_ms`). Elapsed time is calculated as `current_clock_ms >= anchor_ms ? current_clock_ms - anchor_ms : 0`, and total beats as `elapsed_ms / ms_per_beat`.
- **NodeSync Protocol v5**: Packets extend to 68 bytes, transmitting `transport_generation`, `timeline_anchor_ms`, and `timeline_checksum`. Followers compare local timeline checksum against the leader's active checksum, failing closed (`SyncTransportError::TimelineMismatch`) on mismatch.
- **Late-Joining Follower Synchrony**: Late followers compute `elapsed = disciplinedClock - leaderAnchor` to seamlessly synchronize to the active playback phase without restarting from Beat 0.

### 2.9 Cue Studio v2 & Cue Runtime Hardening (LG-034)
- **Single-Lock Snapshot Runtime**: `CueRunner::snapshot(now_ms)` is the exclusive read contract (Scheduler transport snapshots, Factory API, and the RuntimeSnapshot collector all consume it). Former unlocked accessors (`is_running()`, `state()`, `current_list()`, index getters) were removed; a static-invariant check (scripts/check_invariants.py) blocks their reintroduction. The legacy deadlock — calling `stop()` from inside `update()` on natural completion — is structurally eliminated via lock-aware internal helpers.
- **Stop Semantics**: Every stop carries a typed reason. Operator stops and preemptions hold the current output; only natural completion of the final cue executes the configured end action (`hold_last`, `blackout`, `restore_fallback`).
- **Pause/Fade Freeze**: Pausing captures hold/fade anchors before the state change and freezes the in-flight transition via `SceneManager::freeze_transition()`; resuming continues the frozen morph over the remaining time. Pause→resume never restarts or snaps the visual.
- **Save/Run Separation**: RUN snapshots the latest saved list into an immutable active copy with its own checksum and monotonic generation. Saves while running flip `contentStale` only. `restart` re-snapshots with a fresh generation and no end-action transient. Backup/restore writes content only; running cue state is never restored.
- **Content Protection**: Strict schema-2 validation (UTF-8 byte caps, bounded timing, strict enums, scene-existence checks) plus `409 cue_list_conflict` optimistic concurrency and `409 cue_list_in_use` dependency-safe deletion.
- **NodeSync v6**: 76-byte packed frames add `cue_list_checksum`, `cue_transition_progress_q15`, `cue_paused_from`, and `cue_source_scene`. Followers fail closed on cue content divergence (`SyncTransportError::CueListMismatch`) and reconstruct paused fades from leader progress. Protocol version verification status is firmware-level (compile + unit tests); see Section 4 for the hardware bench scope.

---

## 3. Remaining Hardware Verification Milestones

The software architecture and simulation boundaries are fully hardened. The runtime now strictly requires physical hardware validation on reference test fixtures before claiming full certification.

- [ ] **Actual RMT Output Verification**:
  - Verify WS2812 / SK6812 signal integrity on an oscilloscope (timing parameters: $T0H = 350\text{ns} \pm 150\text{ns}$, $T1H = 900\text{ns} \pm 150\text{ns}$).
  - Verify 8 parallel RMT channels under maximum pixel load (8 x 300 pixels @ 40 FPS).
- [ ] **Actual PCA9685 Output Verification**:
  - Verify I2C bus clock stretching, waveform shape, and fast-mode (400 kHz) stability.
  - Verify output enable (OE) physical pull-down and PWM duty accuracy from 0 to 4095.
- [ ] **Physical Art-Net Source Validation**:
  - Test real Art-Net controller broadcast and unicast streaming at 44 Hz continuous.
  - Verify ArtPoll / ArtPollReply round-trip and node discovery in QLC+ and Madrix.
- [ ] **Physical sACN Source Validation**:
  - Verify IGMPv2/v3 multicast group membership joining and leaving on physical managed switch.
  - Test multi-console priority preemption (Priority 100 vs Priority 150).
- [ ] **Hardware Emergency Stop**:
  - Verify physical GPIO interruption line latching.
  - Assert zero PWM / LED output within $< 5\text{ms}$ of E-Stop pin trip.
- [ ] **Factory Hardware Test Output**:
  - Execute factory direct RGBW pattern and verify physical color balance and gamma response.
- [ ] **Thermal and Power Budget Verification**:
  - Validate dynamic power limiter on calibrated DC power supply (assert total draw does not exceed configured `max_power_mw`).
- [ ] **24h / 72h Continuous Burn-in Run**:
  - Continuous 72-hour test at 40 FPS under maximum load with 4 concurrent DMX streams.
  - Assert 0 memory leaks, 0 dropped frames, and zero unexpected watchdog restarts.

---

## 4. Multi-Node Fleet Sync & Show Transport Hardware Test Matrix (LG-025 & LG-026)

> **Hardware Requirements:** Minimum 2 physical nodes required; 3 physical nodes preferred.
> **Timing Certification Gate:** Do NOT mark cluster timing accuracy certified until physical frame-to-frame offset is measured on physical oscilloscope/logic analyzer probes across multiple hardware nodes.

| Test ID | Test Scenario | Hardware Setup | Verification Criteria | Status |
| :--- | :--- | :--- | :--- | :---: |
| **SYNC-HW-01** | Automatic Leader Election | 3 nodes in `auto` role | Exactly one node claims `activeRole = master`; lowest/highest MAC tie-breaker resolves deterministically without oscillation. | `[PENDING BENCH]` |
| **SYNC-HW-02** | Explicit Master/Worker Hierarchy | 1 designated master, 2 workers | Master advertises leadership; workers lock clock and populate `masterAddress`. | `[PENDING BENCH]` |
| **SYNC-HW-03** | Leader Loss & Failover | Disconnect leader power | Workers observe peer timeout (>3000 ms), clear transient timeline transport, and either elect a new leader (auto) or enter degraded standalone mode (worker). | `[PENDING BENCH]` |
| **SYNC-HW-04** | Leader Recovery & Rejoin | Reconnect original leader | Cluster re-synchronizes; followers re-discipline clocks without reboot or watchdog trip. | `[PENDING BENCH]` |
| **SYNC-HW-05** | Sync-Token Mismatch Isolation | Node C configured with wrong token | Node C receives zero sync packets, triggers no timeline transport, sends no ACK, and is isolated from cluster. | `[PENDING BENCH]` |
| **SYNC-HW-06** | Clock Lock & Bounded Jitter | 3 nodes on managed Wi-Fi switch | Assert `clockLocked = true`, clock offset $< 5\text{ms}$, jitter $< 2\text{ms}$, and RTT $< 20\text{ms}$. | `[PENDING BENCH]` |
| **SYNC-HW-07** | Scene Synchronization Accuracy | Trigger Scene on Leader | All follower nodes activate matching scene slot. Measure physical optical output start skew $< 15\text{ms}$. | `[PENDING BENCH]` |
| **SYNC-HW-08** | Timeline Transport Coherence | Start Timeline (120 BPM) on Leader | Follower scheduler tasks execute local sequences against synchronized clock. Measure beat phase alignment across nodes. | `[PENDING BENCH]` |
| **SYNC-HW-09** | Show Content Drift Detection | Alter Scene 3 on Node B | Sync Studio identifies `DRIFT` on Node B; Leader and Node A remain `MATCHED`. | `[PENDING BENCH]` |
| **SYNC-HW-10** | Show Deployment & Parity Repair | Deploy Show from Node A to Node B | Show assets transferred sequentially, verified via `/api/v1/show-manifest`, and Node B transitions from `DRIFT` to `MATCHED`. | `[PENDING BENCH]` |
| **SYNC-HW-11** | Exact Mirror Pruning | Target has extra scenes / cues | Exact mirror purge removes orphaned target assets (presets, cue lists, scene motion tracks) without affecting network or hardware config. | `[PENDING BENCH]` |
| **SYNC-HW-12** | Firmware / Protocol Mismatch | Node with differing schema | Node reports `INCOMPATIBLE` rather than content drift. Cluster blocks synchronized execution. | `[PENDING BENCH]` |
| **SYNC-HW-13** | Synchronized Cue List Progression | Start Cue List on Leader | Followers receive leader-authoritative cue transitions (hold, fade, next cue). Measure physical output optical crossfade alignment. | `[PENDING BENCH]` |
| **SYNC-HW-14** | Synchronized Manual Crossfade | T-Bar slider moved on Leader | Leader broadcasts `manual_fade_q15` at 10 Hz; followers render matching crossfade mix in real time. | `[PENDING BENCH]` |
| **SYNC-HW-15** | Follower Suppression & 409 Guard | Direct POST /api/v1/activate on Follower | Follower responds 409 `sync_follower_read_only`. Follower UI displays leader-controlled lock banner. | `[PENDING BENCH]` |
| **SYNC-HW-16** | Show Transport Mode Preemption | Switch Scene -> Timeline -> Cue on Leader | Coordinator transitions modes cleanly. Channel masks (0xFFFF) and control owners hand off without collision. | `[PENDING BENCH]` |
| **SYNC-HW-17** | Playback vs Schedule Parity | Target has differing calendar schedule | Cluster reports `PLAYBACK PARITY: MATCHED`, `SCHEDULE PARITY: DIFF`. Cluster READY state permits group playback. | `[PENDING BENCH]` |

---

## 5. Security & Transport Notice (LG-029)

With the completion of LG-029 (Node-Backed Authentication, Sessions & Credential Provisioning):
- The firmware is the sole authority for authentication and authorization (`AuthStore` + `AuthManager`).
- Browser PIN-checking authority, hardcoded dev tokens (`lumigrid_admin`), and credential restoration backdoors are permanently removed.
- Ephemeral session management (256-bit entropy, 30 min idle / 8 hr absolute timeout) is enforced on the node.
- WebSocket connections require single-use pre-handshake tickets (`Sec-WebSocket-Protocol: lumigrid-ticket.<ticket>`).

> [!WARNING]
> **Transport Security Limitation:**
> HTTP on an untrusted LAN does not provide confidentiality against packet interception. Bearer tokens and API keys transmitted over plaintext HTTP are susceptible to network eavesdropping on an untrusted or open network. Production deployments requiring transport encryption require dedicated network isolation or future TLS capabilities.

## 6. Stop Condition Notice

With the completion of LG-024 (Live Desk Hardening), LG-025 (Fleet Sync Studio & Show Deployment v1), LG-026 (Show Transport v2 & Synchronized Cue Playback), LG-029 (Node-Backed Authentication & Provisioning), LG-030 (Frontend Architecture Alignment), and LG-031 (Canonical Runtime State & API v1 Cutover):
- **Architecture is frozen** across firmware services, state machines, API contracts, and frontend stores.
- **No new feature families** (Scene Studio redesign, Timeline drag/drop, TLS, cloud services) will be introduced before physical hardware validation.
- Timing certification requires completion of the bench test matrices above on physical LumiGrid LED Node hardware.
