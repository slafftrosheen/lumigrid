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
| `PUT` | `/api/v1/presets` | Save a preset in slot 0–7. |
| `POST` | `/api/v1/activate` | Activate a preset. |
| `GET` | `/api/v1/config` | Read persisted node settings. |
| `PUT` | `/api/v1/config` | Update persisted node settings. |
| `GET` | `/api/v1/schedules` | List schedule slots. |
| `PUT` | `/api/v1/schedules` | Save a schedule slot. |

### Activate a preset

```json
POST /api/v1/activate
{ "preset": 2 }
```

The preset index must be between 0 and 7. A successful request returns
`202 {"activated":true}`.

### Device configuration

`GET /api/v1/config` returns `deviceName`, `activePreset`, `fallbackPreset`,
`watchdogTimeoutS`, `maxPowerMw`, `gamma`, `softStart`, `softStartMs`,
`timezone`, `syncEnabled`, and `syncRole`.

`PUT /api/v1/config` accepts any supported subset. Limits are: preset indexes
0–7, watchdog 0–3600 seconds, power 0–60000 mW, soft-start 0–5000 ms, and
sync role 0–2.

### Schedule slots

`PUT /api/v1/schedules` accepts:

```json
{
  "index": 0,
  "hourStart": 9,
  "minuteStart": 0,
  "hourEnd": 17,
  "minuteEnd": 0,
  "presetIndex": 2,
  "daysOfWeek": 127,
  "enabled": true
}
```

There are eight slots. `daysOfWeek` is a 7-bit mask (0–127).

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
