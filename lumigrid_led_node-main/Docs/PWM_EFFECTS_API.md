# PWM Effects API Documentation

REST API endpoints for advanced PWM channel control and effects.

## Base URL

All endpoints are relative to `/api/pwm`

## Channel Control

### Set Channel Duty

```http
POST /pwm/{channel}
```

**Parameters:**
- `channel`: PWM channel number (0-15)

**Request Body:**
```json
{
  "duty": 0.75
}
```

**Response:**
```json
{
  "channel": 0,
  "duty": 0.75,
  "status": "ok"
}
```

### Get Channel Status

```http
GET /pwm/{channel}
```

**Response:**
```json
{
  "channel": 0,
  "duty": 0.75,
  "active_effect": "candle",
  "waveform": null
}
```

## Waveform Generator

### Start Waveform

```http
POST /pwm/{channel}/waveform
```

**Request Body:**
```json
{
  "type": "sine",
  "frequency_hz": 2.0,
  "amplitude": 0.8,
  "offset": 0.1,
  "duty_cycle": 0.5
}
```

**Waveform Types:**
- `sine` - Sine wave
- `triangle` - Triangle wave
- `sawtooth` - Sawtooth wave
- `reverse_sawtooth` - Reverse sawtooth
- `square` - Square wave
- `pulse` - Pulse wave (configurable duty cycle)
- `exponential` - Exponential curve
- `logarithmic` - Logarithmic curve
- `custom` - Custom LUT (requires additional data)

**Response:**
```json
{
  "status": "ok",
  "waveform": "sine"
}
```

### Stop Waveform

```http
DELETE /pwm/{channel}/waveform
```

## Effects

### Start Candle Effect

```http
POST /pwm/{channel}/effect/candle
```

**Request Body:**
```json
{
  "base_brightness": 0.7,
  "flicker_amount": 0.3,
  "flicker_speed": 8.0,
  "seed": 12345
}
```

### Start Lightning Effect

```http
POST /pwm/{channel}/effect/lightning
```

**Request Body:**
```json
{
  "trigger_probability": 0.02,
  "flash_count": 3,
  "flash_duration_ms": 50,
  "inter_flash_ms": 100
}
```

### Start Fade Effect

```http
POST /pwm/{channel}/effect/fade
```

**Request Body:**
```json
{
  "duration_ms": 5000,
  "start_brightness": 0.0,
  "end_brightness": 1.0,
  "curve": 0
}
```

**Curve Types:**
- 0: Linear
- 1: Ease In
- 2: Ease Out
- 3: Ease In-Out
- 4: S-Curve

### Start Color Temperature Transition

```http
POST /pwm/color-temp
```

**Request Body:**
```json
{
  "channels": [0, 1, 2, 3],
  "kelvin_start": 2700,
  "kelvin_end": 6500,
  "transition_ms": 10000
}
```

Note: Requires 4 channels (R, G, B, W)

### Stop Effect

```http
DELETE /pwm/{channel}/effect
```

## ADSR Envelope

### Configure ADSR

```http
POST /pwm/{channel}/adsr
```

**Request Body:**
```json
{
  "attack_ms": 100,
  "decay_ms": 200,
  "sustain_level": 0.7,
  "release_ms": 500
}
```

### Trigger Envelope

```http
POST /pwm/{channel}/adsr/trigger
```

**Response:**
```json
{
  "status": "triggered",
  "gate": true
}
```

### Release Envelope

```http
POST /pwm/{channel}/adsr/release
```

**Response:**
```json
{
  "status": "released",
  "gate": false
}
```

## Channel Groups

### Create Group

```http
POST /pwm/groups
```

**Request Body:**
```json
{
  "channels": [0, 1, 2, 3],
  "synchronized": true,
  "phase_offsets": [0.0, 0.25, 0.5, 0.75]
}
```

**Response:**
```json
{
  "group_id": 0,
  "channel_count": 4,
  "status": "ok"
}
```

### Update Group Phase

```http
PUT /pwm/groups/{group_id}/phase
```

**Request Body:**
```json
{
  "channel_index": 1,
  "offset": 0.33
}
```

### Delete Group

```http
DELETE /pwm/groups/{group_id}
```

## Automation

### Start Recording

```http
POST /pwm/{channel}/automation/record
```

**Response:**
```json
{
  "status": "recording",
  "start_time_ms": 1234567890
}
```

### Stop Recording

```http
POST /pwm/{channel}/automation/stop
```

**Response:**
```json
{
  "status": "stopped",
  "event_count": 42
}
```

### Add Control Change

```http
POST /pwm/{channel}/automation/cc
```

**Request Body:**
```json
{
  "cc_number": 7,
  "value": 0.8
}
```

Note: Only works when recording is active

### Playback Automation

```http
POST /pwm/{channel}/automation/playback
```

**Request Body:**
```json
{
  "start_time_ms": 0,
  "loop": true
}
```

### Get Automation Data

```http
GET /pwm/{channel}/automation
```

**Response:**
```json
{
  "events": [
    {
      "timestamp_ms": 0,
      "cc_number": 7,
      "value": 0.5
    },
    {
      "timestamp_ms": 1000,
      "cc_number": 7,
      "value": 0.8
    }
  ]
}
```

## Bulk Operations

### Set Multiple Channels

```http
POST /pwm/bulk
```

**Request Body:**
```json
{
  "channels": [
    { "channel": 0, "duty": 0.5 },
    { "channel": 1, "duty": 0.7 },
    { "channel": 2, "duty": 0.9 }
  ]
}
```

### All Off

```http
POST /pwm/all-off
```

**Response:**
```json
{
  "status": "ok",
  "channels_affected": 16
}
```

## DMX Integration

### Map PWM to DMX

```http
POST /pwm/{channel}/dmx-map
```

**Request Body:**
```json
{
  "dmx_address": 1,
  "dmx_universe": 0,
  "inverted": false,
  "scale": 1.0
}
```

### Get DMX Mapping

```http
GET /pwm/{channel}/dmx-map
```

**Response:**
```json
{
  "dmx_address": 1,
  "dmx_universe": 0,
  "inverted": false,
  "scale": 1.0
}
```

### Remove DMX Mapping

```http
DELETE /pwm/{channel}/dmx-map
```

## Error Responses

```json
{
  "error": "Invalid channel number",
  "code": 400
}
```

Common errors:
- 400: Invalid parameters (channel out of range, invalid value)
- 404: Channel or resource not found
- 409: Conflict (e.g., waveform already active)
- 500: Internal error
