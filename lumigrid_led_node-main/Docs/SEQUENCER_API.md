# Sequencer API Documentation

REST API endpoints for the LumiGrid multitrack sequencer system.

## Base URL

All endpoints are relative to `/api`

## Sequences

### List Sequences

```http
GET /sequences
```

**Response:**
```json
{
  "sequences": [
    {
      "id": 1,
      "name": "My Sequence",
      "bpm": 120,
      "total_duration_ms": 10000,
      "track_count": 4
    }
  ]
}
```

### Get Sequence

```http
GET /sequences/{id}
```

**Response:**
```json
{
  "id": 1,
  "name": "My Sequence",
  "bpm": 120,
  "time_signature": 68,
  "total_duration_ms": 10000,
  "tracks": [
    {
      "id": 0,
      "name": "ALED 1",
      "enabled": true,
      "channel": 0,
      "volume": 1.0,
      "muted": false,
      "solo": false
    }
  ],
  "events": [
    {
      "timestamp_ms": 0,
      "duration_ms": 5000,
      "track_id": 0,
      "event_type": 0
    }
  ]
}
```

### Create Sequence

```http
POST /sequences
```

**Request Body:**
```json
{
  "name": "New Sequence",
  "bpm": 120,
  "time_signature": 68
}
```

**Response:**
```json
{
  "id": 2,
  "name": "New Sequence",
  "bpm": 120,
  "total_duration_ms": 0,
  "track_count": 0
}
```

### Update Sequence

```http
PUT /sequences/{id}
```

**Request Body:**
```json
{
  "name": "Updated Name",
  "bpm": 140
}
```

### Delete Sequence

```http
DELETE /sequences/{id}
```

**Response:**
```json
{
  "status": "ok"
}
```

## Playback Control

### Play Sequence

```http
POST /sequences/{id}/play
```

**Response:**
```json
{
  "status": "playing",
  "playhead_ms": 0
}
```

### Pause Sequence

```http
POST /sequences/{id}/pause
```

**Response:**
```json
{
  "status": "paused",
  "playhead_ms": 1500
}
```

### Stop Sequence

```http
POST /sequences/{id}/stop
```

**Response:**
```json
{
  "status": "stopped",
  "playhead_ms": 0
}
```

### Seek

```http
POST /sequences/{id}/seek
```

**Request Body:**
```json
{
  "time_ms": 5000
}
```

**Response:**
```json
{
  "status": "ok",
  "playhead_ms": 5000
}
```

## Tracks

### Add Track

```http
POST /sequences/{id}/tracks
```

**Request Body:**
```json
{
  "name": "New Track",
  "channel": 3
}
```

**Response:**
```json
{
  "track_id": 3,
  "name": "New Track",
  "channel": 3
}
```

### Update Track

```http
PUT /sequences/{id}/tracks/{track_id}
```

**Request Body:**
```json
{
  "name": "Updated Track",
  "volume": 0.8,
  "muted": false
}
```

### Remove Track

```http
DELETE /sequences/{id}/tracks/{track_id}
```

## Events

### Add Event

```http
POST /sequences/{id}/events
```

**Request Body:**
```json
{
  "timestamp_ms": 1000,
  "duration_ms": 2000,
  "track_id": 0,
  "event_type": 0,
  "event_data": {
    "effect_name": "scanner",
    "speed": 1.5,
    "color": 16711680
  }
}
```

**Response:**
```json
{
  "event_index": 5,
  "status": "ok"
}
```

### Update Event

```http
PUT /sequences/{id}/events/{event_index}
```

**Request Body:**
```json
{
  "timestamp_ms": 1500,
  "duration_ms": 3000
}
```

### Remove Event

```http
DELETE /sequences/{id}/events/{event_index}
```

## Import/Export

### Export Sequence

```http
GET /sequences/{id}/export
```

**Response:**
```json
{
  "id": 1,
  "name": "My Sequence",
  "bpm": 120,
  "tracks": [...],
  "events": [...]
}
```

### Import Sequence

```http
POST /sequences/import
```

**Request Body:** (Full sequence JSON from export)

**Response:**
```json
{
  "id": 3,
  "name": "Imported Sequence",
  "status": "ok"
}
```

## Event Types

| Value | Name | Description |
|-------|------|-------------|
| 0 | EVT_EFFECT_START | Start an effect on the track |
| 1 | EVT_EFFECT_STOP | Stop current effect |
| 2 | EVT_KEYFRAME | Parameter automation point |
| 3 | EVT_CUE | Scene recall |
| 4 | EVT_TEMPO_CHANGE | BPM adjustment |
| 5 | EVT_SYNC_MARKER | Master/slave sync point |

## Quantization Values

| Value | Name | Description |
|-------|------|-------------|
| 0 | QUANT_NONE | No quantization |
| 1 | QUANT_1_4 | Quarter note |
| 2 | QUANT_1_8 | Eighth note |
| 3 | QUANT_1_16 | Sixteenth note |
| 4 | QUANT_1_32 | Thirty-second note |

## Time Signature

Time signature is encoded as a single byte:
- Upper nibble: beats per measure (e.g., 4)
- Lower nibble: note value (e.g., 4 for quarter note)
- Example: 0x44 = 4/4 time

## Error Responses

All endpoints may return error responses:

```json
{
  "error": "Invalid sequence ID",
  "code": 404
}
```

Common error codes:
- 400: Bad request (invalid parameters)
- 404: Not found (sequence/track/event doesn't exist)
- 500: Internal server error
