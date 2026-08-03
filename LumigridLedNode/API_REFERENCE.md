# LumiGrid LED Node API

The LED Node exposes an HTTP API on port 80 and serves the operator dashboard at
`/`. All API responses are JSON and include `Cache-Control: no-store`.

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
