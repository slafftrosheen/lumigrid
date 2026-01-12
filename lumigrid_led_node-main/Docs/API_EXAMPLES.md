# LumiGrid LED Node REST API Examples

This document provides example requests and responses for the unified REST API endpoints implemented on the ESP32 LED Node.

## Table of Contents
- [Status](#status)
- [Configuration](#configuration)
- [Presets](#presets)
- [Trigger Actions](#trigger-actions)
- [Sensor Discovery](#sensor-discovery)
- [Sensor Data Updates](#sensor-data-updates)

---

## Status

### GET /api/status
Get the current node status including uptime, memory, and active effects.

**Request:**
```bash
curl http://192.168.1.100/api/status
```

**Response (200 OK):**
```json
{
  "connected": true,
  "uptime": 123456789,
  "heap_free": 245678,
  "effect": "rainbow_flow"
}
```

---

## Configuration

### GET /api/config
Read the current configuration.

**Request:**
```bash
curl http://192.168.1.100/api/config
```

**Response (200 OK):**
```json
{
  "node_name": "lumigrid-node-01",
  "brightness": 0.8,
  "strips": [
    {
      "ch": 0,
      "type": "WS2812B",
      "order": "GRB",
      "length": 120
    },
    {
      "ch": 1,
      "type": "WS2812B",
      "order": "GRB",
      "length": 60
    }
  ],
  "wifi": {
    "ssid": "MyNetwork"
  }
}
```

### POST /api/config
Update configuration with partial updates allowed. Only provided fields will be updated.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "brightness": 0.9,
    "node_name": "lumigrid-living-room"
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error Response (400 Bad Request):**
```
config must be JSON object
```

**Error Response (500 Internal Server Error):**
```
config lock timeout
```

---

## Presets

### GET /api/presets
List all available presets.

**Request:**
```bash
curl http://192.168.1.100/api/presets
```

**Response (200 OK):**
```json
[
  {
    "name": "led_ocean"
  },
  {
    "name": "fire_warm"
  },
  {
    "name": "rainbow_party"
  }
]
```

### POST /api/presets
Create or update a preset. Validates against the preset schema.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "led_ocean",
    "description": "Smooth ocean wave effect",
    "effect_id": 1105,
    "speed": 0.8,
    "intensity": 1.0,
    "brightness": 0.9,
    "palette_id": 1,
    "color1": [0, 0, 255],
    "color2": [0, 100, 200],
    "color3": [0, 50, 150]
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error Response (400 Bad Request):**
```
effect_id=500 out of range [1000,9999]
```

### GET /api/presets/{name}
Load a specific preset by name.

**Request:**
```bash
curl http://192.168.1.100/api/presets/led_ocean
```

**Response (200 OK):**
```json
{
  "preset_name": "led_ocean",
  "description": "Smooth ocean wave effect",
  "effect_id": 1105,
  "speed": 0.8,
  "intensity": 1.0,
  "brightness": 0.9,
  "palette_id": 1,
  "color1": [0, 0, 255],
  "color2": [0, 100, 200],
  "color3": [0, 50, 150]
}
```

**Error Response (404 Not Found):**
```
preset not found
```

### DELETE /api/presets/{name}
Delete a preset.

**Request:**
```bash
curl -X DELETE http://192.168.1.100/api/presets/led_ocean
```

**Response (204 No Content):**
```
(empty body)
```

---

## Trigger Actions

### POST /api/trigger
Execute one-shot actions or bounded overrides. Useful for temporary effects or event-driven responses.

#### Flash Action
Trigger a brief brightness flash on a channel.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{
    "action": "flash",
    "channel": 0,
    "duration_ms": 500
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

#### Pulse Action
Trigger a pulse effect (strobe with longer duration).

**Request:**
```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{
    "action": "pulse",
    "duration_ms": 1000
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

#### Blackout Action
Immediately turn off all channels.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{
    "action": "blackout"
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

#### Apply Preset Temporarily (Future)
Apply a preset for a specified duration, then revert.

**Note:** This action is currently logged but not fully implemented. Preset loading and temporary application with automatic restoration will be added in a future update.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{
    "action": "apply_preset",
    "preset": "fire_warm",
    "channel": 0,
    "duration_ms": 5000
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

## Sensor Discovery

### GET /api/discover/needs_sensor_data
Check if this LED node accepts sensor data from sensor nodes.

**Request:**
```bash
curl http://192.168.1.100/api/discover/needs_sensor_data
```

**Response (200 OK):**
```json
{
  "needs_data": true,
  "types": [
    "temperature",
    "humidity",
    "motion",
    "light"
  ]
}
```

---

## Sensor Data Updates

### POST /api/sensor_update
Send sensor data to the LED node. Used by sensor nodes to provide temperature, humidity, motion, and light level data for reactive effects.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/sensor_update \
  -H "Content-Type: application/json" \
  -d '{
    "node_id": "sensor_node_01",
    "timestamp": 1678886400,
    "readings": {
      "temperature_c": 23.5,
      "humidity_percent": 55.2,
      "motion_detected": true,
      "light_level": 450.0
    }
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Error Response (400 Bad Request):**
```
json
```

**Error Response (500 Internal Server Error):**
```
payload too large
```

---

## Error Handling

All endpoints follow consistent error handling:

### HTTP Status Codes
- **200 OK** - Successful request
- **204 No Content** - Successful deletion
- **400 Bad Request** - Invalid request (malformed JSON, validation errors)
- **404 Not Found** - Resource not found
- **500 Internal Server Error** - Server error

### Error Response Format
Error responses include a descriptive message in plain text:
```
effect_id=500 out of range [1000,9999]
```

Or for general errors:
```
json
```

---

## Integration Examples

### Python Example
```python
import requests
import json

# Get status
response = requests.get('http://192.168.1.100/api/status')
print(response.json())

# Update config
config_update = {
    "brightness": 0.85,
    "node_name": "bedroom-leds"
}
response = requests.post(
    'http://192.168.1.100/api/config',
    json=config_update
)
print(response.json())

# Create preset
preset = {
    "preset_name": "sunset",
    "effect_id": 1104,
    "speed": 0.5,
    "intensity": 0.8,
    "color1": [255, 100, 0],
    "color2": [255, 50, 0]
}
response = requests.post(
    'http://192.168.1.100/api/presets',
    json=preset
)
print(response.json())

# Trigger flash
trigger = {
    "action": "flash",
    "channel": 0,
    "duration_ms": 300
}
response = requests.post(
    'http://192.168.1.100/api/trigger',
    json=trigger
)
print(response.json())
```

### Node.js Example
```javascript
const axios = require('axios');

const BASE_URL = 'http://192.168.1.100';

// Get status
async function getStatus() {
  const response = await axios.get(`${BASE_URL}/api/status`);
  console.log(response.data);
}

// Update config
async function updateConfig() {
  const config = {
    brightness: 0.85,
    node_name: 'bedroom-leds'
  };
  const response = await axios.post(`${BASE_URL}/api/config`, config);
  console.log(response.data);
}

// Create preset
async function createPreset() {
  const preset = {
    preset_name: 'sunset',
    effect_id: 1104,
    speed: 0.5,
    intensity: 0.8,
    color1: [255, 100, 0],
    color2: [255, 50, 0]
  };
  const response = await axios.post(`${BASE_URL}/api/presets`, preset);
  console.log(response.data);
}

// Trigger action
async function triggerFlash() {
  const trigger = {
    action: 'flash',
    channel: 0,
    duration_ms: 300
  };
  const response = await axios.post(`${BASE_URL}/api/trigger`, trigger);
  console.log(response.data);
}

// Run examples
(async () => {
  await getStatus();
  await updateConfig();
  await createPreset();
  await triggerFlash();
})();
```

---

## Notes

1. **Partial Config Updates**: The `/api/config` POST endpoint supports partial updates. Only fields present in the request body will be updated; other fields retain their current values.

2. **Preset Validation**: All presets are validated against the schema defined in `firmware/schemas/preset-led.schema.json`. Invalid presets will be rejected with a descriptive error message.

3. **Sensor Data**: The sensor data endpoints enable integration with separate sensor nodes in the LumiGrid system. LED effects can react to environmental data for dynamic, context-aware lighting.

4. **Trigger Actions**: The `/api/trigger` endpoint is designed for temporary effects and one-shot actions. For persistent changes, use the appropriate configuration or preset endpoints.

5. **Thread Safety**: Configuration updates are thread-safe and use mutex locking to prevent race conditions.

6. **Atomic Writes**: Preset and configuration writes use atomic file operations (write to temp file, then rename) to prevent corruption.
