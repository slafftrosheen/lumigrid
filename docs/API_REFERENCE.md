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

`400` denotes invalid input, `401` authentication failure, `409` a full command
queue, and `503` a subsystem that has not completed initialization.

## Operator endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/v1/status` | Read signage playback state. |
| `GET` | `/api/v1/presets` | List stored scene presets. |
| `PUT` | `/api/v1/presets` | Save a scene preset in slot 0–31. |
| `POST` | `/api/v1/activate` | Activate a preset. |
| `GET` | `/api/v1/effects` | Read display names, descriptions, and controls for each effect. |
| `GET` | `/api/v1/sequence` | Read the eight-zone, 16-step timeline. |
| `PUT` | `/api/v1/sequence` | Save the timeline. |
| `GET` | `/api/v1/config` | Read persisted node settings. |
| `PUT` | `/api/v1/config` | Update persisted node settings. |
| `GET` | `/api/v1/schedules` | List calendar automation cues. |
| `PUT` | `/api/v1/schedules` | Replace all calendar automation cues. |
| `GET` | `/api/v1/sync` | Read sync role, clock health, and discovered followers. |
| `GET` | `/api/v1/health` | Read system memory, uptime, and telemetry. |
| `GET` | `/api/v1/logs` | Read recent diagnostic logs. |
| `GET` | `/api/v1/audit` | Read recent security and configuration events. |
| `GET` | `/api/v1/backup` | Export node configuration and presets as JSON. |
| `POST` | `/api/v1/restore` | Import configuration from JSON and reboot. |
| `POST` | `/api/v1/ota` | Upload new firmware binary and reboot. |

### Activate a preset

```json
POST /api/v1/activate
{ "preset": 2 }
```

The preset index must be between 0 and 31. A successful request returns
`202 {"activated":true}`.

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

### Timeline

The timeline contains eight independently enabled zones with up to 16 ordered
steps per zone. A step has `presetIndex` (0–31) and `lengthBeats` (1–64).
`bpm` is constrained to 10–500 and `enabled` controls manual playback.

### Device configuration

`GET /api/v1/config` returns `deviceName`, `activePreset`, `fallbackPreset`,
`watchdogTimeoutS`, `maxPowerMw`, `gamma`, `softStart`, `softStartMs`,
`timezone`, `syncEnabled`, `syncRole`, and a `zones` array containing up to eight objects with `name` and `role` (e.g. `"Sign"`, `"Sign Element"`, `"Ambiance Light"`, `"Main Light"`, or `"Unassigned"`).

`PUT /api/v1/config` accepts any supported subset. Limits are: preset indexes
0–31, watchdog 0–3600 seconds, power 0–60000 mW, soft-start 0–5000 ms, and
sync role 0–2.

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

### Sync health

`GET /api/v1/sync` returns the configured and active role, clock lock and
epoch state, receive/miss counts, the latest clock correction and jitter, and
the master node's recently acknowledged followers with round-trip latency.
`auto` nodes elect a leader using a stable MAC-address tie-breaker; only one
node should be explicitly configured as `master`.

Sync uses UDP broadcast on port 6455. Keep sync nodes on a trusted, isolated
network. The endpoint reports measured health, but hardware deployment testing
is still required to establish timing accuracy for a specific Wi-Fi layout.

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
`liveStreamLayer` in `protocol` configuration determines which layer (0–2, default: 2) incoming DMX color data is injected into across all 8 zones.

## Factory vs Signage Effects Boundary

The system operates two separate effect engines:
1. **Signage Effects (`/api/v1/effects`, `/api/v1/presets`)**: Production signage pipeline supporting multi-zone multi-layer blending (Solid, Breathe, Chase, Sweep, Reveal, Sparkle, Gradient, Fire, Aurora, ColorWave, StaticPulse).
2. **Factory Effects (`/api/v1/factory/effect`)**: Hardware diagnostic test routines time-bounded by the firmware for factory quality control and wiring validation (`identity`, `pixel_chase`, `rainbow`, `white_walk`, `pwm_walk`, `pwm_fade`, `combined_pulse`, `burn_in`, `blackout`).

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
