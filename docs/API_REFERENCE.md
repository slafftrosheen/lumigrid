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

## Authentication

Every `/api/v1/` endpoint requires:

```http
Authorization: ******
```

The embedded dashboard requests the token at connection time and retains it only
for the current browser session. Use HTTPS or a trusted isolated network whenever
credentials are in use. The current firmware token provisioning is documented as
an open firmware-security follow-up in `PLAN.md`.

Errors use this shape:

```json
{ "error": "invalid_json" }
```

`400` denotes invalid input, `401` authentication failure, `409` a request
rejected by the current runtime state or command queue, and `503` a subsystem
that has not completed initialization.

## Operator endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/status` | Read signage playback state. |
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
| `GET` | `/api/v1/sync` | Read sync role, clock health, transport, master address, and discovered followers. |
| `GET` | `/api/v1/show-manifest` | Read compact CRC32 show manifest and aggregate showHash. |
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

### Sync health and Topology

`GET /api/v1/sync` returns:
- `configuredRole`: `auto`, `master`, or `worker`.
- `activeRole`: Currently resolved role (`auto`, `master`, or `worker`).
- `running`: Boolean indicating if NodeSync UDP background task is active.
- `clockLocked`: Boolean indicating bounded clock offset lock (< 5 ms).
- `clockHasEpoch`: Boolean indicating valid epoch reference.
- `masterAddress`: IPv4 string of the current leader node (or null for the leader itself).
- `transport`: Active playback transport mode (`standalone`, `scene`, `timeline`).
- `remoteBpm`: Remote BPM broadcast from leader during timeline transport (or null).
- `remoteTimelineEnabled`: Boolean indicating if leader's timeline sequence is active.
- `peerCount`, `packetsReceived`, `packetsMissed`, `clockOffsetMs`, `clockJitterUs`.
- `peers[]`: List of acknowledged peers with address, ageMs, and latencyMs.

### Show Manifest and Content Parity

`GET /api/v1/show-manifest` returns compact content CRC32 checksums:
```json
{
  "schema": 1,
  "firmware": "0.9.0",
  "protocol": 1,
  "showHash": 2851193042,
  "scenes": [
    { "index": 0, "checksum": 182736455, "motionChecksum": 98233441 }
  ],
  "timeline": { "checksum": 33182761 },
  "schedules": { "checksum": 7174411 },
  "cueLists": [
    { "index": 0, "checksum": 9182771 }
  ]
}
```
- `showHash`: Deterministic CRC32 computed across schema, scenes, scene motions, timeline, calendar schedule, and cue lists.
- Hardware configuration (IP, Wi-Fi, channel mapping, brightness limits) is strictly separated and does not affect `showHash`.
- Cluster READY state requires both clock synchronization (`clockLocked === true`) and content parity (`showHash` match).

### Show Deployment Rules
- Show deployment distributes Show content (Scenes, Motions, Timeline, Cue Lists, and optional Schedules) from a source node to target nodes via sequential HTTP requests (`GET source` -> `PUT target`).
- **Never** uses `/api/v1/backup` or `/api/v1/restore` for Show deployment to avoid overwriting node hardware, networking, or credentials.
- When `mirrorExactly` is enabled, stale scenes and cue lists on target nodes are purged using `DELETE /api/v1/presets?index=N` and `DELETE /api/v1/cue-list?index=N`.
- **Cue Lists**: Cue Lists can be mirrored across the fleet, but live Cue playback is not synchronized over UDP in Sync v1. Synchronized playback is supported for Scenes (leader slot propagation) and Timeline (clock + BPM transport).

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
