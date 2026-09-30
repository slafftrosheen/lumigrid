# LumiGrid LED Node API

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LED Node exposes an HTTP API on port 80 and serves the operator dashboard at
`/`. All API responses are JSON and include `Cache-Control: no-store`.

## API Contracts and Constraints

The node strictly enforces memory budgets to ensure ESP32 stability. Clients must respect these constraints:
- Normal API response size: `< 8 KB`
- Large diagnostic response size: `< 16 KB`
- Backup payload size: `< 32 KB`
- WebSocket frame size: `< 4 KB`
- Incoming JSON request body size: `< 8 KB`

## Authentication & Authorization (LG-029)

LumiGrid uses node-authoritative authentication. The node is the sole authority for credentials, session lifecycle, and role assignment.

### Transport Security Limitation

> [!WARNING]
> **Transport Security Limitation:**
> HTTP on an untrusted LAN does not provide confidentiality against packet interception. Bearer tokens and API keys transmitted over plaintext HTTP are susceptible to network eavesdropping on an untrusted or open network. Production deployments requiring transport encryption require dedicated network isolation or future TLS capabilities.

### Accepted Credentials

1. **Browser / Web Dashboard Session:**
   ```http
   Authorization: Bearer <session-token>
   ```
   Ephemeral 256-bit random token issued by `POST /api/v1/auth/session`. Valid for 30 minutes of idle time, with an absolute 8-hour timeout.

2. **Machine / Automation API Key:**
   ```http
   X-API-Key: <long-lived-credential>
   ```
   Direct persistent credential for automated tools and scripts.

> [!IMPORTANT]
> Query parameter tokens (`?token=`, `?auth=`) and legacy headers (`X-Auth-Token`) are strictly rejected by the node firmware.

### Role Hierarchy

Endpoints enforce the following role hierarchy:
- **Admin**: Full control (firmware OTA, backup/restore, network settings, credential management, factory recovery).
- **Installer**: Diagnostics, health, logs, channel bench testing, factory output tests.
- **Operator**: Read status, trigger scene presets, manual override, timeline and cue controls, read network/diagnostics.
- **Public**: Dashboard static assets, `GET /api/v1/info`, `GET /api/v1/auth/status`, `POST /api/v1/auth/bootstrap` (unprovisioned only), `POST /api/v1/auth/session`.

### Authentication Endpoints

| Method | Path | Min Role | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/auth/status` | Public | Check if node is provisioned and if bootstrap is allowed. |
| `POST` | `/api/v1/auth/bootstrap` | Public (Unprovisioned only) | Provision initial Admin access key (16–64 characters). |
| `POST` | `/api/v1/auth/session` | Public | Authenticate node credential to open an ephemeral session. |
| `GET` | `/api/v1/auth/session` | Operator+ | Authoritative check of current session role and time-to-live. |
| `DELETE` | `/api/v1/auth/session` | Operator+ | Invalidate current session (logout). |
| `POST` | `/api/v1/auth/ws-ticket` | Operator+ | Generate a single-use, 15-second ticket for WebSocket handshake. |
| `GET` | `/api/v1/auth/credentials` | Admin | Query configured credential roles (no secrets exposed). |
| `POST` | `/api/v1/auth/credentials` | Admin | Rotate a role credential (invalidates associated sessions). |
| `DELETE` | `/api/v1/auth/credentials` | Admin | Disable an Installer or Operator credential. |

### WebSocket Authentication

The live telemetry WebSocket (`/ws`) requires pre-handshake ticket validation:
1. Client requests ticket: `POST /api/v1/auth/ws-ticket` with `Authorization: Bearer <session>`
2. Client opens WebSocket passing ticket in subprotocol:
   ```ts
   new WebSocket(wsUrl, [`lumigrid-ticket.${ticket}`]);
   ```
3. Firmware validates and consumes the ticket during pre-handshake. Unauthorized connections are rejected.

Errors use this shape:

```json
{ "error": "invalid_credentials" }
```

`400` denotes invalid input, `401` authentication failure / session expired, `403` insufficient role permissions, `409` conflict / state rejection, `429` rate limiting (failed logins), and `503` subsystem unready.

## Operator endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/status` | Read canonical Schema 3 runtime snapshot (identical to WebSocket state). |
| `GET` | `/api/v1/dmx/frame` | Read on-demand raw 512-channel DMX universe frame (Installer+). |
| `GET` | `/api/v1/channels` | Read all 16 physical output channels (A1–A8 and P1–P8). |
| `POST` | `/api/v1/channel/test` | Test or identify a specific physical channel connector. |
| `PUT` | `/api/v1/manual` | Apply live manual control override frame across 16 physical channels. |
| `DELETE` | `/api/v1/manual` | Release live manual control override lease. |
| `GET` | `/api/v1/presets` | List stored scene presets (or `?index=N` for singular preset fetch). |
| `PUT` | `/api/v1/presets` | Save a scene preset in slot 0–31. |
| `DELETE` | `/api/v1/presets` | Free/delete a scene preset in slot 0–31 (via `?index=N`). |
| `POST` | `/api/v1/activate` | Activate a preset. |
| `GET` | `/api/v1/effects` | Read display names, descriptions, and controls for each effect. |
| `GET` | `/api/v1/sequence` | Read the 16-channel, 16-step timeline. |
| `PUT` | `/api/v1/sequence` | Save the timeline. |
| `GET` | `/api/v1/config` | Read persisted node settings. |
| `PUT` | `/api/v1/config` | Update persisted node settings. |
| `GET` | `/api/v1/schedules` | List calendar automation cues. |
| `PUT` | `/api/v1/schedules` | Replace all calendar automation cues. |
| `POST` | `/api/v1/show/control` | Control unified Show Transport (Scene, Timeline, Cue) with leader authority. |
| `GET` | `/api/v1/show/status` | Read unified Show Transport runtime status and mode. |
| `DELETE` | `/api/v1/scene-motion` | Delete motion tracks for a scene preset (via `?index=N`). |
| `GET` | `/api/v1/sync` | Read sync role, clock health, transport, master address, and discovered followers. |
| `GET` | `/api/v1/fleet/nodes` | Read discovered fleet nodes table and self identity via mDNS. |
| `GET` | `/api/v1/show-manifest` | Read compact CRC32 show manifest, playbackHash, and scheduleHash. |
| `GET` | `/api/v1/health` | Read system memory, uptime, and telemetry. |
| `GET` | `/api/v1/logs` | Read recent diagnostic logs. |
| `GET` | `/api/v1/audit` | Read recent security and configuration events. |
| `GET` | `/api/v1/backup` | Export node configuration and presets as JSON. |
| `POST` | `/api/v1/restore` | Import configuration from JSON and reboot. |
| `POST` | `/api/v1/ota` | Upload new firmware binary and reboot. |

### Activate a preset

```http
POST /api/v1/activate
Content-Type: application/json
```

```json
{
  "preset": 2
}
```

The preset index must be between 0 and 31.

When the running lighting runtime accepts and loads the requested preset, the
endpoint returns:

```http
202 Accepted
```

```json
{
  "accepted": true,
  "preset": 2,
  "generation": 41
}
```

`accepted` means the request was accepted by the lighting runtime. It does not
claim that a physical output frame has already been committed to the hardware,
and a higher-priority live control source may still temporarily own the output.

If the scene cannot be activated — for example because the preset cannot be
loaded or the lighting runtime is not in an accepting state — the endpoint
returns:

```http
409 Conflict
```

```json
{
  "error": "scene_activation_rejected"
}
```

The `generation` value identifies this accepted scene request. Clients can
compare it with `appliedGeneration` from `/api/v1/status`. The request is
physically acknowledged only when `appliedGeneration >= generation`.

`/api/v1/status` also exposes `requestedPreset`, `requestedGeneration`,
`appliedGeneration`, and `sceneApplyPending`. A generation advances to
applied only after the render path successfully commits a frame through the
hardware output transaction. If output faults, the generation remains pending
and `outputFault` reports the failure.

Clients must therefore use node telemetry rather than treating the `202`
response itself as hardware acknowledgement.

### Scenes and effects

`GET /api/v1/effects` is the canonical effect metadata source for clients. Each
item includes an effect `id`, a human-readable `label` and `description`, and,
where applicable, a parameter label, unit, range, and default. Clients should
use this endpoint rather than exposing the internal `fxParam` name.

`PUT /api/v1/presets` accepts a scene `index`, `name`, `brightness` (0–255),
`transitionMs`, and up to eight channel definitions. A channel accepts `r`,
`g`, `b`, `pwmDuty` (0–4095), `effect`, `effectSpeedMs`, `fxParam`,
`pixelEnabled`, and `pwmEnabled`.

Scenes can also add one preset-wide finishing layer:

```json
{
  "overlayEffect": "strobe",
  "overlaySpeedMs": 0,
  "overlayParam": 50
}
```

`overlayEffect` is `none` or `strobe`. `overlaySpeedMs: 0` follows the
sequencer beat; otherwise it is the flash period in milliseconds.

### Live manual control (`/api/v1/manual`)

Live Desk v1 (LG-024) introduces non-persistent, lease-bound manual override across all 16 physical channels (channels 0–7 addressable A1–A8, 8–15 PWM P1–P8).

#### Apply manual control frame

```http
PUT /api/v1/manual
Content-Type: application/json
Authorization: ******
```

```json
{
  "leaseMs": 15000,
  "channels": [
    { "index": 0, "r": 255, "g": 128, "b": 0, "brightness": 255 },
    { "index": 8, "pwm": 3072 }
  ]
}
```

- **Replacement semantics**: Each `PUT /api/v1/manual` completely replaces the previous manual override frame and active channel mask. It does not merge with prior manual state. Channels omitted from the `channels` array are released from manual control and return to their lower-priority source.
- **Server-derived channel mask**: The firmware derives the active 16-bit channel mask directly from the indices present in the `channels` array. Clients do not specify `channelMask` in the request body.
- **Lease duration**: `leaseMs` is clamped between `1000` and `60000` ms (default: `15000` ms). Overrides automatically expire and fall back to the lower-priority source (CueRunner, Timeline, or active Preset) if heartbeats cease.
- **Channel scoping**: Only channels included in `channels` are acquired or overridden. Unclaimed channels remain in their background playback state.
- **Safety pipeline**: Manual control commands pass through the identical `ColorPipeline`, `max_brightness`, per-channel limits, gamma calibration, global `PowerBudget`, and `OutputScheduler` transactions. Safety limits cannot be bypassed.
- **Authorization**: Requires `Operator` or `Admin` role (`403 Forbidden` if unprivileged).
- **Arbitration priority**:
  1. `EmergencyStop` (highest)
  2. `Factory` (bench/burn-in tests)
  3. `LiveDmx` (Art-Net / sACN)
  4. `ManualOverride` (Live Desk v1)
  5. `CueRunner` (playlist/cue execution)
  6. `Timeline` (step sequencer)
  7. `SavedScene` (default background scene)

Response:
```json
{
  "accepted": true,
  "channelMask": 257,
  "leaseMs": 15000
}
```
The response `channelMask` reflects the exact mask acquired by firmware (`257` = `0x0101` for channels 0 and 8). Clients must verify that this matches their requested mask, failing closed on any discrepancy.

#### Release manual control

```http
DELETE /api/v1/manual
Authorization: ******
```

Response:
```json
{
  "released": true
}
```

`DELETE /api/v1/manual` releases the complete `ManualOverride` control source state on the node, returning all previously overridden channels to background playback immediately without waiting for lease expiration. It does not accept a payload. Other control sources (such as `LiveDmx` or `Factory`) remain entirely unaffected.

### Timeline

The timeline contains 16 independently enabled tracks corresponding to the 16 physical output channels (A1–A8 addressable, P1–P8 PWM) with up to 16 ordered steps per track. A step has `presetIndex` (0–31) and `lengthBeats` (1–64). `bpm` is constrained to 10–500 and `enabled` controls manual playback.

### Device configuration

`GET /api/v1/config` returns canonical node settings:
- Node metadata: `deviceName`, `activePreset`, `fallbackPreset`, `watchdogTimeoutS`, `maxPowerMw`, `gamma`, `softStart`, `softStartMs`, `timezone`, `syncEnabled`, `syncRole`.
- Physical outputs: `channels` array containing 16 channel objects (`id`, `name`, `type`, `pin`, `maxCurrentMa`, `length`, `groupMask`).
- Groups: `groups` array containing up to 8 group objects (`id`, `name`, `channelMask`, `enabled`).
- For legacy client compatibility, responses also provide `zones` and `ports` alias projections.

`PUT /api/v1/config` updates persisted node configuration. It accepts canonical fields or compatibility aliases, and validates 16-channel and 8-group structures. Persisted configuration writes require the `Admin` role (`403 Forbidden` for `Operator`). Limits are: preset indexes 0–31, watchdog 0–3600 seconds, power 0–60000 mW, soft-start 0–5000 ms, and sync role 0–2.

### Calendar automation

`PUT /api/v1/schedules` accepts up to eight entries. `daysOfWeek` is a
7-bit mask with Sunday at bit 0 and Saturday at bit 6. A `preset` action
activates a scene; a `sequence` action starts the saved timeline at that
minute. A scene action stops a calendar-started timeline.

```json
{
  "entries": [
    {
      "hour": 9,
      "minute": 0,
      "daysOfWeek": 62,
      "action": "sequence",
      "presetIndex": 0,
      "enabled": true
    }
  ]
}
```

### Show Transport v2 Coordinator

`POST /api/v1/show/control` provides explicit transport switching across Scene, Timeline, and Cue List:
```json
{
  "mode": "scene",
  "sceneIndex": 7
}
```
Or for Timeline:
```json
{
  "mode": "timeline",
  "action": "start",
  "bpm": 124
}
```
Or for Cue List:
```json
{
  "mode": "cue",
  "action": "start",
  "cueListIndex": 0,
  "cueIndex": 0
}
```
Supported Cue actions: `start`, `stop`, `pause`, `resume`, `next`, `previous`, `jump`, `crossfade`.

`GET /api/v1/show/status` returns unified transport coordinator status:
```json
{
  "mode": "cue",
  "generation": 42,
  "scene": { "index": 0 },
  "timeline": { "running": false, "bpm": 120 },
  "cue": {
    "running": true,
    "state": "fading",
    "listIndex": 0,
    "cueIndex": 3,
    "nextCueIndex": 4,
    "elapsedMs": 450,
    "remainingMs": 1550,
    "manualFade": 0
  }
}
```

#### Follower Read-Only Guard (409 Conflict)
Active followers (`activeRole === 'worker'` tracking a reachable leader) reject local direct playback requests (`POST /api/v1/activate`, `POST /api/v1/cue-control`, `POST /api/v1/show/control`) with:
```json
{
  "error": "sync_follower_read_only",
  "message": "Playback is controlled by sync leader 192.168.1.50",
  "leader": "192.168.1.50"
}
```
Show content editing (`PUT /api/v1/presets`, `PUT /api/v1/sequence`, `PUT /api/v1/cue-list`) remains permitted.

### Sync Health, Topology, and NodeSync Protocol v4

`GET /api/v1/sync` returns:
- `protocolVersion`: `4` (NodeSync v4 with packed 60-byte payload).
- `configuredRole`: `auto`, `master`, or `worker`.
- `activeRole`: Currently resolved role (`auto`, `master`, or `worker`).
- `running`: Boolean indicating if NodeSync UDP background task is active.
- `groupLabel`: Configured cluster group label.
- `syncEnabled`: Boolean indicating if multi-node sync is enabled.
- `clockLocked`: Boolean indicating bounded clock offset lock (< 5 ms).
- `clockHasEpoch`: Boolean indicating valid epoch reference.
- `masterAddress`: IPv4 string of the current leader node (or null for the leader itself).
- `transport`: Active playback transport mode (`standalone`, `scene`, `timeline`, `cue`).
- `transportGeneration`: Monotonic generation counter of the leader's transport commands.
- `transportApplyOk`: Boolean indicating if follower successfully applied remote transport state.
- `transportError`: Error string if follower failed to apply remote transport.
- `remoteBpm`: Remote BPM broadcast from leader during timeline transport (or null).
- `remoteTimelineEnabled`: Boolean indicating if leader's timeline sequence is active.
- `cue`: Active cue playback telemetry (`listIndex`, `state`, `cueIndex`, `nextCueIndex`, `manualFade`).
- `peerCount`, `packetsReceived`, `packetsMissed`, `clockOffsetMs`, `clockJitterUs`.
- `peers[]`: List of acknowledged peers with address, ageMs, and latencyMs.

### Show Manifest and Content Parity

`GET /api/v1/show-manifest` returns compact content CRC32 checksums under schema 2:
```json
{
  "schema": 2,
  "firmware": "0.10.0",
  "protocol": 4,
  "playbackHash": 182930411,
  "scheduleHash": 7174411,
  "showHash": 2851193042,
  "scenes": [
    { "index": 0, "checksum": 182736455, "motionChecksum": 98233441 }
  ],
  "timeline": { "checksum": 33182761 },
  "cueLists": [
    { "index": 0, "checksum": 9182771 }
  ],
  "schedules": { "checksum": 7174411 }
}
```
- `playbackHash`: Deterministic CRC32 computed across real-time playback assets (Scenes, Scene Motion tracks, Timeline sequence, and Cue Lists). Parity of `playbackHash` is required for cluster synchronization readiness.
- `scheduleHash`: CRC32 of calendar automation cues. Schedule divergence between nodes is considered optional and does not block cluster playback.
- `showHash`: Aggregate hash across all assets including calendar schedules.
- Hardware configuration (IP, Wi-Fi, channel mapping, brightness limits) is strictly separated and does not affect show hashes.

### Show Deployment Rules
- Show deployment distributes Show content (Scenes, Motions, Timeline, Cue Lists, and optional Schedules) from a source node to target nodes via sequential HTTP requests (`GET source` -> `PUT target`).
- **Never** uses `/api/v1/backup` or `/api/v1/restore` for Show deployment to avoid overwriting node hardware, networking, or credentials.
- When `mirrorExactly` is enabled:
  - Stale presets are deleted with `DELETE /api/v1/presets?index=N`.
  - Stale motion tracks are deleted with `DELETE /api/v1/scene-motion?index=N`.
  - Stale cue lists are deleted with `DELETE /api/v1/cue-list?index=N`.
- **Synchronized Cue Playback**: Under Show Transport v2 and NodeSync v4, live Cue playback is fully synchronized over UDP across the cluster. The Leader runs the authoritative cue list timing and crossfades, broadcasting cue indices, hold/fade states, and manual crossfade progress to all follower nodes at 10 Hz. Followers suppress local conflicting playback and render matching frames locked to the cluster clock.

### Diagnostics and Logs

`GET /api/v1/health` returns system telemetry including uptime, free heap memory, minimum free heap memory, tasks statistics, and other diagnostic data useful for monitoring node stability.

`GET /api/v1/logs` streams the last N lines of system logs (if the RAM-based log sink is enabled). It is useful for debugging connection or sync issues without attaching a serial monitor.

`GET /api/v1/audit` returns the ring-buffer of security and configuration changes. It includes timestamps, roles (e.g. `Admin`), event types (e.g. `ConfigUpdate`, `AuthFailure`), and descriptive messages.

### Firmware & Configuration Management

`GET /api/v1/backup` aggregates the current `DeviceConfig` (including network, tokens, physical outputs, and zones) and all 32 `SceneConfig` presets (including all 8 zones and 3 layers per zone) into a v2 JSON file downloaded as an attachment.

`POST /api/v1/restore` accepts a JSON payload in v2 or v1 format. It parses the JSON, updates the configuration and presets, logs an `Audit` event, and automatically restarts the node.

`POST /api/v1/ota` accepts a signed `.bin` firmware file via multipart or raw binary stream. The firmware is written to the alternate OTA partition. If successful, the node reboots into the new firmware. Note that the node will attempt to validate the new firmware upon boot and rollback if a crash occurs during early initialization.

### Protocol Configuration

The node supports incoming DMX streams via Art-Net and sACN.
`liveStreamLayer` in `protocol` configuration determines which layer (0–2, default: 2) incoming DMX color data is injected into across configured channels.

## Factory vs Signage Effects Boundary

The system operates two separate effect engines:
1. **Signage Effects (`/api/v1/effects`, `/api/v1/presets`)**: Production signage pipeline supporting multi-channel multi-layer blending (Solid, Breathe, Chase, Sweep, Reveal, Sparkle, Gradient, Fire, Aurora, ColorWave, StaticPulse).
2. **Factory Effects (`/api/v1/factory/effect`)**: Hardware diagnostic test routines time-bounded by the firmware for factory quality control and wiring validation (`identity`, `pixel_chase`, `rainbow`, `white_walk`, `pwm_walk`, `pwm_fade`, `combined_pulse`, `burn_in`, `blackout`).

> [!NOTE]
> Operational live faders and interactive manual overrides must use `PUT /api/v1/manual` and `DELETE /api/v1/manual`. The `/api/v1/factory/*` endpoints are strictly reserved for manufacturing bench diagnostics and hardware validation tests.

## Factory endpoints

Factory commands are deliberately time-bounded by the firmware. Their common
success response is `202 {"accepted":true}`.

| Method | Path | Body |
| --- | --- | --- |
| `GET` | `/api/v1/factory/status` | — |
| `POST` | `/api/v1/factory/effect` | `effect`, optional `brightness` (0–96), `durationMs` |
| `POST` | `/api/v1/factory/lane` | `lane`, `red`, `green`, `blue`, optional `brightness`, `durationMs` |
| `POST` | `/api/v1/factory/pwm` | `channel`, `duty` (0–4095), optional `durationMs` |
| `POST` | `/api/v1/factory/auto-loop` | `enabled` boolean |
| `POST` | `/api/v1/factory/abort` | — |

Available effects are `identity`, `pixel_chase`, `rainbow`, `white_walk`,
`pwm_walk`, `pwm_fade`, `combined_pulse`, `burn_in`, and `blackout`.
Lanes and PWM channels are numbered 0–7. `durationMs` must be a positive value
not greater than the firmware factory-test maximum.

`POST /api/v1/factory/abort` immediately queues a safe blackout and returns:

```json
{ "accepted": true, "action": "blackout" }
```

## Real-time status

Connect to `GET /ws` with the WebSocket protocol. The node broadcasts factory
status JSON whenever its state changes. Clients should treat it as read-only and
use the HTTP command endpoints for all writes.

## Network Commissioning & Lifecycle (LG-028)

The node provides an operator-grade commissioning path that allows moving from Factory/Recovery SoftAP to the local LAN without rebooting the lighting runtime, losing active output, exposing passwords, or risking unreachability from a mistyped credential.

### Network Modes

1. **`ap_only`**: Standalone Access Point mode. Node hosts a SoftAP (default `192.168.4.1`) and does not connect to a station LAN.
2. **`sta_only`**: Station-only mode. Node connects to the configured Wi-Fi network. SoftAP is disabled. If connection fails, rollback occurs to the previous configuration.
3. **`sta_with_ap_fallback`**: Station with Recovery AP fallback. Node attempts to join the configured Wi-Fi LAN. If STA connection fails after configured retries, the node automatically spins up a Recovery SoftAP (`192.168.4.1`) in AP+STA mode so operators can reconnect and remediate credentials, while continuing background reconnect attempts with bounded backoff (5s, 10s, 30s, 60s max).

### Credentials & Security Invariants

- **Write-Only Passwords**: Stored Wi-Fi passwords are never returned in `GET /api/v1/network/config`. Responses contain only `"passwordSet": true`.
- **Zero Plaintext Logging**: Credentials are never printed in diagnostic logs, serial console messages, or audit trails.
- **Preservation by Default**: Submitting a blank password field preserves existing credentials. To explicitly clear credentials for an open network, clients must set `"clearPassword": true`.
- **Safe-Mode Locked**: When the node is in Safe/Recovery mode, network configuration is locked (`400 {"error":"safe_mode_network_locked"}`) to guarantee physical recovery access remains available.

### Asynchronous Lifecycle & Sockets Rebind

Reconfiguring Wi-Fi is an asynchronous runtime-owned operation:
1. `PUT /api/v1/network/config` strictly validates parameters (`ssid`, `password`, `apChannel` 1..13, `mode`).
2. If another apply operation is already in flight, returns `409 Conflict` (`{"error":"network_apply_in_progress"}`).
3. Returns `202 Accepted` (`{"accepted":true,"commandId":<id>,"applyPending":true}`) and allows the HTTP response to cleanly escape the socket before reconfiguration begins.
4. Runtime network transition shuts down network-dependent listeners (MQTT, NodeSync, sACN, Art-Net, mDNS, HTTP/API) while keeping `LightingRuntime`, `OutputController`, and active LED output running without interruption (zero blackout).
5. Wi-Fi reconfigures. Once network readiness is confirmed, network services are rebound.
6. The new configuration is persisted to NVS **only after** successful application.
7. **Transactional Rollback**: If the new network fails to achieve readiness, the runtime restores the previous configuration (or stays in recovery AP for `sta_with_ap_fallback`), leaves the previously known-good NVS configuration intact, and marks the command failed.

### Endpoints

| Method | Path | Auth Role | Description |
| --- | --- | --- | --- |
| `GET` | `/api/v1/network/config` | Operator | Returns current network configuration (`mode`, `ssid`, `passwordSet`, `apName`, `apChannel`). Never returns password. |
| `PUT` | `/api/v1/network/config` | Admin | Asynchronously queues a network configuration apply request. Returns `202 Accepted`. |
| `GET` | `/api/v1/network/status` | Operator | Returns runtime network telemetry (`state`, `mode`, `staConnected`, `staIp`, `apActive`, `applyState`, `lastApplyError`, `persistedMatchesRuntime`). |

## Fleet Discovery, Presence & Immutable Identity (LG-030)

LumiGrid nodes automatically discover peer nodes on the local network via mDNS Zeroconf. The web dashboard connects to a single node, which queries the local subnet and reports the discovered fleet.

### Stable Immutable Hostname & Zeroconf Service

- **Immutable Hostname**: The node mDNS hostname is permanently derived from its factory-assigned MAC address (`lg-<mac>.local`, e.g. `lg-84f703c29110.local`), ensuring stability across operator renames and avoiding collision.
- **Display Name**: Friendly device names are advertised as the Zeroconf service instance name.
- **Dedicated Service**: Nodes advertise the product-specific Zeroconf service:
  ```text
  _lumigrid._tcp.local.
  ```
- **TXT Record Metadata**:
  - `id`: Immutable node identifier (`lg-84f703c29110`)
  - `family`: Hardware family (`led-node`)
  - `model`: Device model (`lg-led-v1`)
  - `fw`: Firmware version derived authoritatively from `esp_app_get_description()->version`
  - `hw`: Hardware revision (`A`)
  - `api`: HTTP API schema version (`1`)
  - `ws`: WebSocket protocol version (`2`)
  - `auth`: Node-backed authentication schema version (`1`)

### Presence & Reconciliation Contract

- **Reconciliation Key**: Nodes are uniquely keyed by immutable `NodeIdentity.id`. If a node's IP address changes via DHCP renewal, the existing entry is updated in place without duplicate records.
- **Self Exclusion**: Querying nodes exclude their own immutable ID from discovered peer lists.
- **Bounded Table**: Bounded table of 32 nodes max, managed in background worker tasks without blocking HTTP handlers.
- **Presence Timing**:
  - **Online**: `age <= 20s`
  - **Stale**: `20s < age <= 60s`
  - **Offline / Expired**: `age > 60s` (automatically pruned)

### Bootstrap Trust Boundary Guard

Bootstrap (`POST /api/v1/auth/bootstrap`) requires **both**:
1. `auth.provisioned == false`
2. Node is in genuine commissioning/recovery context (`CommissioningState::FactoryFirstBoot` or `CommissioningState::PhysicalRecovery`).

Nodes connected to customer infrastructure STA networks or ordinary LANs return `403 Forbidden` (`{"error": "bootstrap_not_allowed"}`) to ensure remote unauthorized claiming cannot occur.

### Discovery Endpoint

```http
GET /api/v1/fleet/nodes
Authorization: Bearer <session-token>
```

#### Response (200 OK)

```json
{
  "self": {
    "id": "lg-84f703c29110",
    "name": "Stage Front",
    "hostname": "lg-84f703c29110",
    "family": "led-node",
    "model": "lg-led-v1",
    "firmware": "1.0.0",
    "hardwareRevision": "A"
  },
  "nodes": [
    {
      "id": "lg-001122334455",
      "name": "Stage Left",
      "hostname": "lg-001122334455.local",
      "ip": "192.168.1.101",
      "port": 80,
      "family": "led-node",
      "model": "lg-led-v1",
      "firmware": "1.0.0",
      "lastSeenMs": 12345000,
      "ageMs": 4000,
      "status": "online"
    }
  ]
}
```

## Canonical Runtime State & API v1 Cutover (LG-031)

LumiGrid control plane maintains **one authoritative runtime-state model**: Schema 3 `RuntimeSnapshot`.

HTTP `GET /api/v1/status` and authenticated WebSocket state streaming (`/ws`) use the **exact same serialized contract**.

```text
Firmware runtime
      ↓
Canonical RuntimeSnapshot v3
      ↓
 ┌───────────────┬─────────────────┐
 │ GET /status   │ authenticated WS │
 └───────────────┴─────────────────┘
          ↓ identical state contract
      nodeStore
          ↓
 Overview / Scenes / Timeline / Cues /
 Live / Factory / Diagnostics / Network
```

### Architectural Invariants

1. **Observational & Side-Effect Free:** Collection and serialization perform zero mutations or bookkeeping transitions. Generation apply acknowledgment occurs strictly in the runtime renderer when frames commit to hardware.
2. **Stream Sequencing Independence:** WebSocket broadcasts include `streamSeq = ++ws_stream_seq` for packet-gap detection. HTTP `/api/v1/status` emits `streamSeq: 0` and does not affect the WebSocket sequence counter.
3. **Bounded Budget:** Serialization uses `FixedJsonWriter` within an 8192-byte max budget. Overflows abort cleanly without transmitting malformed JSON.
4. **Coalesced Broadcasts:** Status notifications utilize atomic CAS (`status_pending_`) collapsing rapid notifications to max 5 Hz.
5. **Decoupled Heavy Data:**
   - Static capabilities are excluded from the live snapshot (`GET /api/v1/capabilities`).
   - 1024-char raw DMX frame data is excluded from generic status (`GET /api/v1/dmx/frame`, role `Installer+`).
   - Binary pixel preview remains on an isolated binary WebSocket channel (header byte `1` or `76`).

### Schema 3 JSON Contract

```json
{
  "schema": 3,
  "streamSeq": 42,
  "generatedAtMs": 1234567,

  "node": {
    "id": "lg-84f703c29110"
  },

  "runtime": {
    "effect": "solid",
    "brightness": 180,
    "testActive": false,
    "autoLoop": false,
    "controlOwner": "ScenePlayback",
    "frameCount": 48291
  },

  "output": {
    "enabled": true,
    "powerMw": 7230,
    "state": "healthy",
    "fault": false,
    "faultCount": 0,
    "estopActive": false
  },

  "scene": {
    "activePreset": 4,
    "requestedPreset": 4,
    "requestedGeneration": 88,
    "appliedGeneration": 88,
    "applyPending": false,
    "transitioning": false,
    "fallbackActive": false
  },

  "manual": {
    "active": false,
    "channelMask": 0,
    "leaseRemainingMs": 0
  },

  "transport": {
    "mode": "scene",
    "generation": 31,
    "sceneIndex": 4,
    "timeline": {
      "running": false,
      "bpm": 120,
      "generation": 41,
      "anchorMs": 123456000,
      "elapsedMs": 7420,
      "currentBeat": 14,
      "activeChecksum": 1234,
      "savedChecksum": 1234,
      "contentStale": false
    },
    "cue": {
      "running": false
    }
  },

  "sync": {
    "configuredRole": "auto",
    "activeRole": "worker",
    "clockLocked": true,
    "masterAddress": "192.168.1.50",
    "lastPacketAgeMs": 140,
    "clockOffsetMs": 1.4,
    "clockJitterUs": 240
  },

  "network": {
    "state": "connected",
    "rssi": -54,
    "staConnected": true,
    "staIp": "192.168.1.71",
    "apActive": false,
    "applyState": "idle"
  },

  "protocols": {
    "artnet": {
      "enabled": true,
      "sources": 0,
      "packetsPerSecond": 0,
      "lastPacketAgeMs": 0
    },
    "sacn": {
      "enabled": true,
      "sources": 0,
      "packetsPerSecond": 0
    }
  },

  "timeline": {
    "clockMs": 90014,
    "driftPpm": 0,
    "synced": true
  },

  "automation": {
    "activeTracks": 0,
    "transitionCurve": "easeInOut"
  },

  "job": null,

  "channels": [
    {
      "index": 0,
      "port": "A1",
      "configured": true,
      "enabled": true,
      "owner": "ScenePlayback",
      "fault": false,
      "brightness": 180,
      "pixels": 60
    }
  ]
}
```

## Timeline Studio v2 & Document API (LG-033)

### Fetch Timeline Document

```http
GET /api/v1/sequence
```

Returns the canonical Schema 2 Timeline document:

```json
{
  "schema": 2,
  "checksum": 123456,
  "bpm": 120,
  "tracks": [
    {
      "channelId": 0,
      "enabled": true,
      "numSteps": 3,
      "steps": [
        { "presetIndex": 4, "lengthBeats": 8 },
        { "presetIndex": 2, "lengthBeats": 4 },
        { "presetIndex": 7, "lengthBeats": 2 }
      ]
    }
  ]
}
```

The 16 tracks correspond permanently to physical channels 0..15 (`A1..A8` and `P1..P8`). Each track contains 0..16 steps with lengths from 1 to 64 beats.

### Save Timeline Document

```http
PUT /api/v1/sequence
Content-Type: application/json
```

```json
{
  "expectedChecksum": 123456,
  "bpm": 120,
  "tracks": [ ... ]
}
```

#### Invariants:
1. **Separation of Persistence and Playback:** Saving a timeline persists content to NVS and recalculates `checksum`. It **never** starts or stops playback, mutates `active_sequence_`, or changes active playback BPM or anchor.
2. **Optimistic Concurrency:** If `expectedChecksum` does not match the stored checksum:
   ```http
   409 Conflict
   ```
   ```json
   {
     "error": "timeline_conflict",
     "currentChecksum": 789012
   }
   ```
3. **Missing Scene Reference Protection:** Every active block must reference an existing scene preset slot (`scene_exists(presetIndex)`). If any active step references a non-existent preset:
   ```http
   400 Bad Request
   ```
   ```json
   {
     "error": "timeline_scene_missing",
     "channel": 4,
     "step": 2,
     "presetIndex": 14
   }
   ```
4. **Successful Response:**
   ```json
   {
     "saved": true,
     "checksum": 789012,
     "commandId": 41
   }
   ```

### Show Control Timeline Transport

```http
POST /api/v1/show/control
Content-Type: application/json
```

Start or restart playback:
```json
{
  "mode": "timeline",
  "action": "start",
  "bpm": 120
}
```

Response:
```json
{
  "accepted": true,
  "mode": "timeline",
  "generation": 42,
  "anchorMs": 123456000,
  "checksum": 789012,
  "commandId": 88
}
```

Stop playback:
```json
{
  "mode": "timeline",
  "action": "stop"
}
```

Response:
```json
{
  "accepted": true,
  "mode": "timeline",
  "generation": 43,
  "commandId": 89
}
```


