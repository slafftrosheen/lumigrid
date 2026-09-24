# LumiGrid Next Phase — Operator Platform v2

Baseline: `e5e18f31034371ba8939d758ca91bc137cc79565`.

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

### 4. Scene Studio

- Visual scene preview.
- Zone/layer editing.
- Effect metadata drives controls dynamically.
- Save / Save As / duplicate / activate.
- Clear dirty/saved/error states.

### 5. Timeline Studio

- Replace form-like editing with a visual beat grid.
- Drag/resize/duplicate steps.
- BPM-aware editing.
- Multi-track view (16 physical channels: A1–A8 and P1–P8).
- Preview against the same scene model used by the device.

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

## Explicit non-goals for this phase

- No new node families.
- No broad runtime rewrite.
- No cloud service.
- No proliferation of new effects before the existing scene/effect system is excellent.
- No second independent dashboard architecture.

## Definition of done

A new operator can discover/add a node, see its real live state, compose a scene, preview it, activate it, sequence it, schedule it, run synchronized cue shows across a cluster, diagnose a fault, run a factory test, and recover/update the node without needing to understand firmware internals.
