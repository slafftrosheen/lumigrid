# LumiGrid Next Phase — Operator Platform v2

Baseline: `ea586e39aca19df0d051e2534cf336c73316f582`.

This phase is deliberately **integration-first**. The runtime/lifecycle architecture is frozen unless a concrete defect requires change. The objective is to make the existing firmware and dashboard behave like one coherent lighting-control product before adding another large feature family.

## Reference patterns

The direction follows proven patterns from mature/open-source lighting projects:

- WLED: keep the device API explicit and automation-friendly; expose presets/effects/segments without making the UI the source of truth.
- WLEDashboard: treat fleet state, groups/scenes, schedules, and live previews as first-class operator concepts.
- ASLS Studio: separate fixture/group state from scene composition, timeline sequencing, and visualisation.
- Aurora: use HTTP for commands and a push channel for live state propagation.

These references are inspiration for architecture and UX, not code dependencies.

## Target architecture

```text
Svelte Operator Console
        |
        +-- typed HTTP client ----> /api/v1/* ----> Firmware services
        |   (Authorization: Bearer <session>)
        |   (X-API-Key: <key> for automation)
        |
        +-- WebSocket state ------> live snapshot
            (Sec-WebSocket-Protocol: lumigrid-ticket.<ticket>)

Firmware
  FactoryApi
    |-- auth (AuthManager & AuthStore)
    |-- scenes / effects
    |-- timeline / schedules
    |-- cue lists / show transport
    |-- diagnostics / health
    |-- factory / burn-in
    |-- configuration / recovery
    `-- OTA
          |
    ShowTransportCoordinator
          |
    Control arbitration
          |
    LightingRuntime
          |
    OutputController
```

## Phase order

### 1. API consolidation

- Make `/api/v1/*` the canonical external-dashboard contract.
- Contain legacy `/json/*` calls behind a compatibility boundary.
- Split the TypeScript client by domain instead of growing one `node.ts` file indefinitely.
- Standardise error objects and validation messages.
- Verify response-size budgets for presets, health, logs, backup, and WebSocket snapshots.

### 2. Session and authorization (LG-029 Complete)

- Replaced browser-only PIN gating with node-backed sessions and credential provisioning.
- Dedicated `AuthStore` in dual-slot NVS (`node_auth` namespace) storing salt + SHA-256 digests; zero plaintext credentials.
- Universal backdoor credentials (`lumigrid_admin`, `lumigrid_install`, `lumigrid_op`, `DEFAULT_OP_PIN`, `DEV_API_TOKEN`) eliminated.
- Node-authoritative session table: max 8 concurrent sessions, 256-bit entropy, 30 min idle / 8 hr absolute expiry.
- Single-use WebSocket tickets (15 s validity) passed via `Sec-WebSocket-Protocol: lumigrid-ticket.<ticket>` pre-handshake authentication.
- Client transport handles per-node session tokens stored in memory and `sessionStorage` (NEVER in `localStorage`).
- Long-lived credentials supported strictly via `X-API-Key` headers for automated tooling; query parameter tokens (`?token=`, `?auth=`) rejected.
- Physical recovery button remains the hardware trust anchor for credential recovery and factory commissioning.
- **Transport Security Limitation:** HTTP on an untrusted LAN does not provide confidentiality against packet interception. Bearer/API-key traffic over plaintext HTTP is not cryptographically protected against eavesdropping on an untrusted network. TLS is a separate future scope.
- Firmware remains the authority for authorization.

### 3. Live state

- WebSocket is read-only state propagation.
- HTTP remains the command path.
- One frontend node-state store feeds Fleet, Scenes, Factory, Diagnostics, and Visualizer.
- Explicit connection states: offline / connecting / online / degraded.

### 4. Scene Studio (LG-032 Complete)

- Visual scene authoring across 16 physical channels (A1–A8 and P1–P8).
- Zone/layer inspector with effect parameter automation and multi-track motion curves (`AutomationTrackEditor`).
- Visual library sidebar with 16-channel thumbnails (`SceneThumbnail`).
- Strict validation: 23-byte UTF-8 name cap, optimistic concurrency via CRC32 conflict detection.
- Reference-aware safe scene deletion guarding against active timeline and cue list dependencies.
- Atomic scene + motion persistence ensuring live output is unchanged after save until intentional activation.

### 5. Timeline Studio (LG-033 Complete)

- Decouple timeline document persistence from playback transport: saving timeline content never mutates running playback.
- Immutable active sequence snapshot on RUN with monotonic transport generation and start anchor clock.
- Visual block sequencer with variable-width beat blocks (1..64 beats) and independent track looping.
- 16 canonical physical tracks permanently mapped to A1–A8 and P1–P8.
- NodeSync protocol v5 transmitting transport generation, timeline anchor, and checksum for synchronized cluster playback.
- Strict document validation, optimistic concurrency conflict detection, and fail-closed follower checksum verification.

### 6. Diagnostics + Factory

- Diagnostics becomes an actionable engineering instrument, not a raw metric dump.
- Factory becomes the standard bench/manufacturing test console.
- Burn-in gets elapsed/remaining time, live health, abort, and final result.

### 7. Configuration + recovery

- Structured configuration editor.
- Explicit persistence state.
- Backup/restore validation and recovery UI.
- OTA progress/reboot/reconnect state.

### 8. Fleet Sync Studio & Show Deployment (LG-025)

- Multi-node cluster orchestration and fleet discovery.
- Leader election, peer RTT latency, bounded clock discipline (< 5 ms).
- Compact show manifest CRC32 checksums (`GET /api/v1/show-manifest`) and content parity (`showHash`).
- Real Scene slot deletion (`DELETE /api/v1/presets?index=N`).
- Follower timeline synchronization via transient transport (zero NVS writes during playback).
- Concurrency-limited show deployment engine with exact mirror mode and verification.
- Synchronized Scene and Timeline playback controls routed to Leader only.

### 9. Show Transport v2 & Synchronized Cue Playback (LG-026)

- Single explicit `ShowTransportCoordinator` unifying Scene, Timeline, and Cue List playback.
- NodeSync protocol v4 with packed 60-byte UDP frames broadcasting explicit transport mode, monotonic generation, and cue snapshots.
- Leader-authoritative cue lists, random progression, hold/fade duration, and manual crossfade progress.
- Follower local playback suppression and 409 `sync_follower_read_only` guard on follower direct activate/transport requests.
- Manifest schema 2 separating `playbackHash` from optional `scheduleHash`.
- Fleet Sync Studio UI with group selector, cluster topology truth evaluation (`VALID_LEADER`, `NO_LEADER`, `SPLIT_BRAIN`, `LEADER_DISAGREEMENT`), polling race protection, leader-cache reset, and unified transport controls.

### 10. Network & Runtime Hardening (LG-027)

- Dual-slot NVS storage (`network` / `network_b`) with CRC-protected schema validation and automatic corruption recovery.
- Network API endpoints for runtime configuration and diagnostics.
- Command journaling with command IDs for state verification.

### 11. Network Commissioning & Live Network Lifecycle (LG-028)

- Full operator commissioning path: `Factory/Recovery AP -> configure LAN -> apply -> node changes network -> dashboard reconnects -> configuration survives reboot`.
- Three explicit network modes: `ApOnly` (standalone AP), `StaOnly` (infrastructure Wi-Fi only), and `StaWithApFallback` (joins Wi-Fi, drops to recovery AP if unreachable while retrying STA with bounded backoff: 5s, 10s, 30s, 60s).
- Strict validation: SSIDs 1..32 bytes, passwords 8..63 bytes (or 0 for open networks), AP channels 1..13 only (channel 14 strictly rejected with 400 `invalid_ap_channel`), and strict JSON types.
- Deterministic CRC calculation: field-by-field hashing in defined order (`schema_version`, `mode`, `sta_ssid`, `sta_password`, `ap_name`, `ap_channel`) eliminating padding discrepancies.
- Asynchronous runtime-owned network apply (`queue_network_apply`): returns immediate `202 Accepted` (`{"accepted":true,"commandId":N,"applyPending":true}`); single in-flight apply enforced (409 Conflict if active).
- Zero blackout network service rebind: bounds graceful restart across MQTT, NodeSync, sACN, Art-Net, mDNS, and HTTP while `LightingRuntime`, `OutputController`, DMX routing, and active physical output continue running uninterrupted.
- Intentional vs accidental disconnect separation: `intentional_disconnect_` flag bypasses auto-reconnect loops during administrative reconfigurations.
- Transactional rollback: holds previous and desired configurations; failures restore previous network (or recovery AP) without altering known-good persisted NVS; new configuration is persisted only after network ready state is confirmed.
- Write-only credentials: passwords never logged and never returned over GET APIs (`passwordSet: true` only).
- Safe-mode recovery protection: network reconfiguration that removes recovery connectivity is blocked in safe mode (`400 safe_mode_network_locked`).
- Fleet Devices operator UI: interactive `NetworkConfigCard` with live status, mode selection, blank-to-preserve password semantics, explicit `clearPassword`, and reconnecting banner handling temporary network disconnects.

### 12. Hardware validation

Execute the documented matrix: boot, networking, API, WebSocket, pixel output, PCA9685, DMX, arbitration, limiter, E-stop, watchdog, recovery, backup/restore, OTA, extended burn-in, and multi-node sync bench tests.

### 13. Fleet Discovery, Presence & Immutable Identity (LG-030 Complete)

- Automated mDNS fleet discovery transforming Fleet Devices from a manually maintained IP list into a self-discovering fleet.
- Mandatory LG-029 security correction: Runtime-owned `CommissioningState` (`None`, `FactoryFirstBoot`, `PhysicalRecovery`). Bootstrap requires `provisioned == false` AND genuine commissioning context; blocked with `403 Forbidden` (`{"error": "bootstrap_not_allowed"}`) on ordinary infrastructure STA.
- Fixed `esp_err_t-as-boolean` truthiness bugs across endpoints (`load_config(cfg) == ESP_OK`).
- Dynamic firmware version derived from authoritative ESP-IDF metadata (`esp_app_get_description()->version`).
- Stable mDNS hostname derived permanently from hardware MAC (`lg-<mac>.local`, e.g. `lg-84f703c29110.local`), uncoupled from mutable display names.
- Dedicated Zeroconf service `_lumigrid._tcp.local.` advertising bounded TXT metadata (`id`, `family`, `model`, `fw`, `hw`, `api=1`, `ws=2`, `auth=1`).
- Low-priority background `FleetDiscoveryService` maintaining bounded table of 32 nodes max, 10s query intervals, <=1s timeouts, presence timing (online <=20s, stale 20s..60s, offline >60s), self-exclusion by immutable ID, and automatic DHCP/IP address reconciliation under immutable `id`.
- Dashboard queries `GET /api/v1/fleet/nodes` on focused node, reconciles discovered peers into `storage.ts`, renders live presence badges, and connects WebSocket strictly to the single focused node.

### 14. Canonical Runtime State & API v1 Cutover (LG-031 Complete)

- **One Authoritative State Model (Schema 3 `RuntimeSnapshot`):** Replaced divergent state payloads (`GET /api/v1/status` vs WebSocket `build_schema2_status_json`) with a single, unified Schema 3 snapshot model shared identically across HTTP and authenticated WebSocket channels.
- **Side-Effect Free Collection:** Snapshot collection and serialization are purely observational with zero runtime mutations; command journal transitions (`APPLIED`) are strictly owned by the runtime renderer upon hardware commit.
- **Bounded Failure-Aware Serialization:** Implemented `FixedJsonWriter` within an explicit 8192-byte max payload budget, preventing silent string truncation on ESP32 while logging diagnostics on overflow.
- **Independent Stream Sequencing:** Decoupled WebSocket gap tracking (`streamSeq > 0`) from HTTP polling (`streamSeq == 0`), preventing false packet-gap alarms when operators query status over HTTP.
- **Coalesced Broadcast Scheduling:** State broadcast notifications utilize an atomic CAS `status_pending_` guard collapsing rapid multi-event notifications into the latest state snapshot at max 5 Hz.
- **Static Capabilities & DMX Decoupling:** Static feature and limit metadata are kept strictly in `GET /api/v1/capabilities`. 1024-char raw DMX frame data is removed from generic status and isolated to on-demand `GET /api/v1/dmx/frame` (`Installer+`).
- **Unified Frontend Authority (`nodeStore`):** Centralized live state store on the focused node; stream sequence gaps trigger `degraded` state and automatic HTTP status recovery back to `online`.
- **Page Cutover:** Scenes, Timeline, Cues, Factory, and Diagnostics consume `nodeStore.snapshot` directly without redundant polling loops. Overview and Firmware use typed `GET /api/v1/info` instead of legacy `/json/info`. Thermal page reports hardware reality ("Temperature sensor unavailable on this hardware/configuration").
- **Closed LG-030 Client Contracts:** Sessions bind strictly to immutable node IDs; DHCP route changes preserve browser sessions; IP address reuse decouples old sessions; browser-local aliases are separate from firmware `deviceName`; transport schemes strictly use `http://` and `ws://` with `transport-blocked` detection for mixed-content browser contexts.

### 15. Scene Studio v2 & Safe Persistence (LG-032 Complete)

- **Full Visual Scene Authoring:** Complete 16-channel canvas mapping channels A1–A8 (pixel strips) and P1–P8 (PWM dimmers) with tabbed channel and motion workspaces.
- **16-Channel Visual Previews:** `SceneThumbnail` component rendering color ribbons for addressable outputs and brightness bars for PWM outputs with dynamic effect indicators.
- **Automation Track Editor:** Multi-track parameter automation editor (`AutomationTrackEditor`) supporting up to 4 tracks and 6 curve points per scene with human-readable value scaling.
- **Optimistic Concurrency & Safe Persistence:** Strict CRC32 checksum verification preventing silent overwrite of remote modifications (`409 Conflict`). Atomic scene configuration and motion curve persistence.
- **Safe-Save Invariant:** Saving scene modifications writes exclusively to persisted NVS without altering active hardware output; explicit activation required to apply changes to physical fixtures.
- **Reference-Aware Deletion:** Preflight checks for scene dependencies in Timeline sequences and Cue Lists, blocking destructive deletions when references exist.
- **Validation:** Strict UTF-8 23-byte scene name enforcement, bounds clamping, and clipboard copy/paste with cross-channel normalization.

### 16. Timeline Studio v2 — Anchored Transport, Safe Playback & Visual Sequencing (LG-033 Complete)

- **Decoupled Persistence & Playback:** `PUT /api/v1/sequence` persists validated timeline documents to NVS without starting, stopping, or altering running playback. Playback is controlled exclusively via `POST /api/v1/show/control` with `mode: "timeline"` (`start`, `restart`, `stop`).
- **Immutable Active Snapshot:** Starting a timeline snapshots the latest saved sequence into `active_sequence_`. Subsequent saves while running do not alter active playback, cleanly reporting stale content status.
- **Anchored Deterministic Timing:** Playback starts deterministically at Beat 0 from an explicit anchor timestamp (`timeline_anchor_ms`) rather than deriving phase from uptime.
- **Monotonic Transport Generations:** Every start/restart increments a transport generation counter, allowing followers and UI to detect new runs without restarting on repeated sync packets.
- **NodeSync Protocol v5:** Transmits 68-byte packed UDP frames carrying `transport_generation`, `timeline_anchor_ms`, and `timeline_checksum`. Followers validate local timeline checksum against the leader and fail closed on mismatch. Late followers calculate exact phase from the leader's anchor.
- **Visual Block Sequencer:** Replaces rigid equal-width step tables with variable-width blocks proportional to beat duration (1..64 beats). Supports block addition, dragging, resizing, duplication, deletion, copy/paste across channels, and independent track loop visualization.
- **Fail-Closed API Validation:** Strict schema 2 validation, 16 physical tracks permanently mapped to A1–A8 and P1–P8, 0..16 steps per track, missing scene reference validation (400), and checksum conflict protection (409).

### 17. Cue Studio v2 & Cue Runtime Hardening (LG-034 Complete)

- **Deadlock-Free Snapshot Runtime:** Rewrote `CueRunner` around a single-lock `snapshot(now_ms)` copy — the only sanctioned read path for all consumers (Scheduler transport snapshots, Factory API, RuntimeSnapshot collector). The original design could deadlock calling `stop()` from inside `update()` on natural completion; stops are now emitted via lock-aware internal helpers and reported through the `Event` callback with a typed stop reason.
- **Typed Stop Reasons:** `operator` (console Stop), `preempted` (scene activation/timeline start), `natural_completion` (final cue finished), `sync_lost` (leader packets timed out). Only natural completion can trigger the configured end action (hold last / blackout / restore fallback scene).
- **Pause/Fade Freeze:** Pausing captures elapsed/remaining **before** the state change, freezes the in-flight fade via `SceneManager::freeze_transition()`, and resuming continues the frozen visual transition over the remaining time — never a restart or visual snap. Applied to auto holds and manual crossfades alike.
- **Immutable Active Snapshots:** RUN snapshots the latest saved list into an immutable active copy with its own checksum and monotonic generation. Saving while running only flips `contentStale` telemetry; `restart` re-snapshots the saved version without executing an end action.
- **Strict Validation & Checksum Conflicts:** Schema 2 documents with UTF-8 byte caps (name 31, label 23), 1..16 cues, bounded timing (hold ≤ 1 h, fade ≤ 60 s), strict enums/curves, and missing-scene rejection (`400 cue_scene_missing`). Saves with stale `expectedChecksum` fail with `409 cue_list_conflict` + `currentChecksum`.
- **Dependency-Safe Deletion:** `DELETE /api/v1/cue-list` returns `409 cue_list_in_use` with exact references (active transport, calendar schedules) instead of silently orphaning a running show.
- **Canonical Transport:** All cue transport moves go through `POST /api/v1/show/control` (`start`, `restart`, `stop`, `pause`, `resume`, `next`, `previous`, `jump`, `crossfade`); every invalid-state move returns deterministic 400/409 (`pause_invalid_state`, `crossfade_requires_manual`, ...). `POST /api/v1/cue/control` remains as a deprecated alias.
- **NodeSync v6 (76 bytes):** Adds `cue_list_checksum`, `cue_transition_progress_q15`, `cue_paused_from`, and `cue_source_scene` to the packed UDP frame. Followers gate remote cue application on list parity (`cue_list_mismatch`) and reconstruct the paused fade from leader progress.
- **Cue Telemetry in RuntimeSnapshot:** §22 show status and the Schema 3 snapshot now carry full cue transport truth: pausedFrom, mode, current/next scene indices, elapsed/remaining, manual fade, transition progress, generation, active/saved checksums, contentStale, remoteDriven.
- **Cue Studio v2 (Operator Console):** Schema-2 editor library (typed document helpers, clipboard, throttled Q15 T-Bar sender, editor store with checksum-conflict workflow), Save vs Save & Run/Restart separation, save/run divergence banner with one-click "Run saved version" (restart), end-action selector, conflict dialog, dependency-aware delete dialog, and unsaved-changes navigation guard. Embedded dashboard gains a Cue transport page (list select, Run/Stop/Pause/Resume/Prev/Next/Restart, manual T-Bar, live telemetry).
- **Editor Validation Parity:** The Svelte editor library enforces the same byte and timing caps as the firmware so clients cannot author documents the node would reject.

### 18. Diagnostics, Factory Validation & Burn-In Workflow (LG-035 Complete - Software Capabilities)

- **Factory Runner & Test State Machine:** Built-in hardware test runner supporting addressable validation (A1–A8), PWM validation (P1–P8), visual test patterns (solid primary/secondary colors, walking white pixel chase, color cycle, full-on white), and time-bounded burn-in cycles.
- **Factory API Surface:** `/api/v1/factory/start`, `/api/v1/factory/stop`, `/api/v1/factory/status`, `/api/v1/factory/report` with Operator+ authorization and command journal integration.
- **Diagnostics Dashboard & Hardware Validation UI:** Dedicated web dashboard pages for real-time diagnostics (runtime state, configuration integrity, network, sync offset, DMX routing, audit log) and factory validation workflow.
- **Physical Validation Status:** Note that LG-035 is complete for *software capabilities only*. Physical bench testing (A1–A8, P1–P8, thermal power budget, multi-node synchronization offset) requires physical hardware bench execution.

### 19. Configuration, Recovery, Backup/Restore & OTA Hardening (LG-036 Active)

- **Removal of Universal Recovery AP Credentials:** Eliminate default `lumigrid123` credentials from production firmware builds; recovery AP uses MAC-derived transitional credentials (`lg_<mac-fragment>`).
- **Physical-Only Factory Reset:** Preserves GPIO0 5-second hold as the sole trust boundary; no unauthenticated or remote software factory reset URLs.
- **Explicit RecoveryStatus:** Runtime-owned recovery model exposed via `GET /api/v1/recovery/status` with fail-safe Safe Mode guarantees.
- **Authoritative DeviceConfig Checksum Contract:** Checksum exposure on `GET /api/v1/config`, optimistic concurrency with `expectedChecksum` on `PUT /api/v1/config` (`409 Conflict`), and strict fail-closed nested bounds validation.
- **Runtime Config Apply Plan:** `ConfigApplyPlan` diff classification, central runtime transition in `LumigridRuntime::apply_device_config`, transactional persistence with readback verification and rollback, and `POST /api/v1/config/validate`.
- **LumiGrid Recovery Package v4:** Streaming newline-delimited JSON (`.lgbak`) with zero auth/network/sync secrets, complete recoverable content coverage, two-stage restore UI, in-memory rollback snapshots, and power-loss-safe `restore_in_progress` NVS marker.
- **OTA Hardening with Bootloader Rollback:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` enabled in production, canonical `POST /api/v1/ota`, partition size and descriptor verification (`lumigrid_led_node`), `OtaBootVerifier` runtime component with bounded core self-test, and cross-reboot OTA state tracking (`GET /api/v1/ota/status`).
- **First-Install Commissioning Wizard:** Step-by-step setup flow reusing existing Identity, Network, Channels, Safety, and Factory validation systems.

## Explicit non-goals for this phase

- No new node families.
- No broad runtime rewrite.
- No cloud service.
- No proliferation of new effects before the existing scene/effect system is excellent.
- No second independent dashboard architecture.
- No Secure Boot or flash encryption in this batch.

## Definition of done

A new operator can discover/add a node, see its real live state, compose a scene, preview it, activate it, sequence it, schedule it, run synchronized cue shows across a cluster, diagnose a fault, run a factory test, and recover/update the node without needing to understand firmware internals.


