# LumiGrid LED Node - REST API Reference

**Version:** 1.0.0  
**Base URL:** `http://{node-ip}/`  
**Content Type:** `application/json`

---

## Table of Contents

1. [Quick Reference](#quick-reference)
2. [Authentication](#authentication)
3. [Status Endpoints](#status-endpoints)
4. [Configuration Endpoints](#configuration-endpoints)
5. [Preset Management](#preset-management)
6. [Effect Control](#effect-control)
7. [Trigger Actions](#trigger-actions)
8. [Sensor Integration](#sensor-integration)
9. [OTA Updates](#ota-updates)
10. [Server-Sent Events](#server-sent-events)
11. [JSON Schemas](#json-schemas)
12. [Code Examples](#code-examples)
13. [Error Handling](#error-handling)

---

## Quick Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Node health, uptime, stats |
| `/api/config` | GET | Get current configuration |
| `/api/config` | POST | Update configuration (partial allowed) |
| `/api/presets` | GET | List all presets |
| `/api/presets` | POST | Create/update preset |
| `/api/presets/{name}` | GET | Get specific preset |
| `/api/presets/{name}` | DELETE | Delete preset |
| `/api/fx` | GET | List available effects |
| `/api/trigger` | POST | Execute one-shot action |
| `/api/cue` | POST | Schedule cue (timeline) |
| `/api/discover/needs_sensor_data` | GET | Check sensor data requirements |
| `/api/sensor_update` | POST | Receive sensor data |
| `/api/ota/upload` | POST | Upload firmware binary |
| `/api/ota/url` | POST | Download firmware from URL |
| `/events` | GET | SSE live stream |

---

## Authentication

**Current Status:** No authentication required (v1.0.0)

**Future:** API key authentication will be added in future releases for production deployments.

**Security Recommendations:**
- Use device only on trusted networks
- Enable firewall rules to restrict access
- Consider VPN for remote access

---

## Status Endpoints

### GET /api/status

Get current node status including health, performance metrics, and telemetry.

**Request:**
```bash
curl http://192.168.1.100/api/status
```

**Response (200 OK):**
```json
{
  "node_type": "led-node",
  "role": "Slave",
  "uptime_ms": 9234567,
  "fps": {
    "aled_ch1": 60,
    "aled_ch2": 60,
    "aled_ch3": 0,
    "aled_ch4": 0,
    "aled_ch5": 0,
    "aled_ch6": 0,
    "aled_ch7": 0,
    "aled_ch8": 0
  },
  "pwm_max_duty": 0.85,
  "last_error": null,
  "heap_free_kb": 112,
  "connected": true,
  "effect": "rainbow_flow"
}
```

**Response Fields:**
- `node_type` - Type of node (always "led-node")
- `role` - "Master" or "Slave" for sync protocol
- `uptime_ms` - Milliseconds since boot
- `fps` - Frames per second for each ALED channel
- `pwm_max_duty` - Maximum PWM duty cycle (0.0-1.0)
- `last_error` - Last error message or null
- `heap_free_kb` - Free heap memory in KB
- `connected` - Wi-Fi connection status
- `effect` - Currently active effect name

---

## Configuration Endpoints

### GET /api/config

Read the current node configuration.

**Request:**
```bash
curl http://192.168.1.100/api/config
```

**Response (200 OK):**
```json
{
  "node_type": "led-node",
  "node_name": "lumigrid-node-01",
  "role": "Slave",
  "control_mode": "Independent",
  "brightness": 0.8,
  "aled": [
    {
      "ch": 1,
      "pixels": 120,
      "type": "WS2812B",
      "order": "GRB",
      "gamma": 2.2,
      "max_fps": 90,
      "mA_per_led": 45
    }
  ],
  "pwm": [
    {
      "ch": 1,
      "freq_hz": 1000,
      "curve": "log",
      "soft_start_ms": 150,
      "max_duty": 0.85
    }
  ],
  "power_limits": {
    "aled_global_mA": 8000,
    "v5_max_A": 10.0
  },
  "wifi": {
    "ssid": "MyNetwork"
  },
  "mqtt": {
    "host": "broker.local",
    "port": 1883,
    "ssl": false
  }
}
```

### POST /api/config

Update configuration with partial updates supported. Only provided fields will be updated.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "brightness": 0.9,
    "role": "Master",
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

**Configuration Schema:**

```json
{
  "node_name": "string (max 32 chars)",
  "role": "Master | Slave",
  "control_mode": "Independent | Synchronized",
  "brightness": "number (0.0-1.0)",
  "aled": [
    {
      "ch": "integer (1-8)",
      "pixels": "integer (1-1200)",
      "type": "WS2812B | SK6812_RGBW",
      "order": "GRB | RGB | GRBW | RGBW",
      "gamma": "number (1.0-3.0)",
      "max_fps": "integer (1-120)",
      "mA_per_led": "integer (1-100)"
    }
  ],
  "pwm": [
    {
      "ch": "integer (1-8)",
      "freq_hz": "integer (50-5000)",
      "curve": "linear | log",
      "soft_start_ms": "integer (0-1000)",
      "max_duty": "number (0.0-1.0)"
    }
  ],
  "power_limits": {
    "aled_global_mA": "integer (1000-20000)",
    "v5_max_A": "number (1.0-20.0)"
  },
  "wifi": {
    "ssid": "string",
    "password": "string"
  },
  "mqtt": {
    "host": "string",
    "port": "integer (1-65535)",
    "ssl": "boolean",
    "user": "string (optional)",
    "pass": "string (optional)"
  }
}
```

---

## Preset Management

Presets are saved LED effect configurations that can be recalled instantly.

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

**Preset Schema:**
- `preset_name` - String, 1-64 chars, alphanumeric + underscore/hyphen only
- `description` - String (optional), max 256 chars
- `effect_id` - Integer, 1000-9999
- `speed` - Number, 0.0-5.0 (default: 1.0)
- `intensity` - Number, 0.0-1.0 (default: 0.5)
- `brightness` - Number, 0.0-1.0 (default: 1.0)
- `palette_id` - Integer, 0-255 (optional)
- `color1` - Array [R, G, B], values 0-255
- `color2` - Array [R, G, B], values 0-255 (optional)
- `color3` - Array [R, G, B], values 0-255 (optional)

### GET /api/presets/{name}

Get a specific preset by name.

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

## Effect Control

### GET /api/fx

List all available effects.

**Request:**
```bash
curl http://192.168.1.100/api/fx
```

**Response (200 OK):**
```json
[
  "solid",
  "gradient",
  "chase",
  "twinkle",
  "rainbow_flow",
  "fire",
  "ocean_wave",
  "plasma",
  "strobe"
]
```

**Built-in Effects:**
- `solid` - Uniform color across all pixels
- `gradient` - Linear color interpolation
- `chase` - Moving pixel with fade trail
- `twinkle` - Randomized sparkle
- `rainbow_flow` - Animated rainbow cycle
- `fire` - Fire/flame simulation
- `ocean_wave` - Ocean wave simulation
- `plasma` - Plasma effect
- `strobe` - Strobe/flash effect

---

## Trigger Actions

### POST /api/trigger

Execute one-shot actions or bounded overrides. Useful for temporary effects or event-driven responses.

**Action Types:**
- `flash` - Brief brightness flash
- `pulse` - Longer duration pulse effect
- `blackout` - Immediate all-off
- `apply_preset` - Apply preset temporarily (future)

#### Flash Action

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

#### Pulse Action

**Request:**
```bash
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{
    "action": "pulse",
    "duration_ms": 1000
  }'
```

#### Blackout Action

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

### POST /api/cue

Schedule a cue for immediate or timed execution (timeline-based).

**Request:**
```bash
curl -X POST http://192.168.1.100/api/cue \
  -H "Content-Type: application/json" \
  -d '{
    "track": "ALEDch1",
    "preset": "ocean_wave",
    "t0": 0,
    "t1": 5000,
    "params": {}
  }'
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

---

## Sensor Integration

LED nodes can receive sensor data to enable reactive lighting effects.

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

### POST /api/sensor_update

Send sensor data to the LED node for reactive effects.

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

**Sensor Data Schema:**
- `node_id` - String, identifier of sensor node
- `timestamp` - Integer, Unix timestamp
- `readings` - Object with sensor values:
  - `temperature_c` - Number, temperature in Celsius
  - `humidity_percent` - Number, relative humidity (0-100)
  - `motion_detected` - Boolean, motion sensor state
  - `light_level` - Number, ambient light level (lux or arbitrary units)

---

## OTA Updates

Over-the-air firmware updates with automatic rollback on boot failure.

### POST /api/ota/upload

Upload a compiled firmware binary for OTA update.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/ota/upload \
  -H "Content-Type: application/octet-stream" \
  --data-binary @firmware.bin
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Note:** Device will reboot after successful upload.

**Error Response (500 Internal Server Error):**
```
OTA update failed
```

### POST /api/ota/url

Download firmware from a URL and perform OTA update.

**Request:**
```bash
curl -X POST http://192.168.1.100/api/ota/url \
  -H "Content-Type: text/plain" \
  -d "http://example.com/firmware.bin"
```

**Response (200 OK):**
```json
{
  "ok": true
}
```

**Note:** Device will reboot after successful download and verification.

**Error Responses:**
- `400 Bad Request` - Invalid URL
- `500 Internal Server Error` - Download or OTA update failed

---

## Server-Sent Events

### GET /events

Real-time event stream using Server-Sent Events (SSE).

**Request:**
```bash
curl -N http://192.168.1.100/events
```

**Response (200 OK):**
```
event: status
data: {"uptime":1000,"fps":60,"heap":112}

event: status
data: {"uptime":2000,"fps":60,"heap":111}

event: effect_change
data: {"channel":1,"effect":"rainbow_flow"}
```

**Event Types:**
- `status` - Periodic status updates (1 Hz)
- `effect_change` - Effect changed on a channel
- `config_update` - Configuration updated
- `error` - Error occurred

**JavaScript Client Example:**
```javascript
const eventSource = new EventSource('http://192.168.1.100/events');

eventSource.addEventListener('status', (event) => {
  const data = JSON.parse(event.data);
  console.log('Status:', data);
});

eventSource.addEventListener('effect_change', (event) => {
  const data = JSON.parse(event.data);
  console.log('Effect changed:', data);
});

eventSource.onerror = (error) => {
  console.error('SSE error:', error);
};
```

---

## JSON Schemas

### Preset LED Schema

Full schema: [/firmware/schemas/preset-led.schema.json](/firmware/schemas/preset-led.schema.json)

**Example Presets:**

**Ocean Effect:**
```json
{
  "preset_name": "led_ocean",
  "description": "Smooth ocean wave effect",
  "effect_id": 1105,
  "speed": 0.8,
  "intensity": 1.0,
  "palette_id": 1,
  "color1": [0, 0, 255],
  "color2": [0, 100, 200],
  "color3": [0, 50, 150]
}
```

**Fire Effect:**
```json
{
  "preset_name": "fire_warm",
  "description": "Warm fire effect",
  "effect_id": 1104,
  "speed": 0.8,
  "intensity": 1.0,
  "color1": [255, 40, 40]
}
```

**Validation Example (Node.js):**
```javascript
const Ajv = require('ajv');
const schema = require('./schemas/preset-led.schema.json');

const ajv = new Ajv();
const validate = ajv.compile(schema);

const preset = {
  preset_name: 'test',
  effect_id: 1105,
  speed: 1.0,
  color1: [255, 0, 0]
};

const valid = validate(preset);
if (!valid) {
  console.error(validate.errors);
}
```

---

## Code Examples

### Python Client

```python
import requests
import json

BASE_URL = 'http://192.168.1.100'

# Get status
response = requests.get(f'{BASE_URL}/api/status')
status = response.json()
print(f"Uptime: {status['uptime_ms']}ms")
print(f"Free heap: {status['heap_free_kb']}KB")

# Update configuration
config_update = {
    "brightness": 0.85,
    "node_name": "bedroom-leds"
}
response = requests.post(
    f'{BASE_URL}/api/config',
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
    f'{BASE_URL}/api/presets',
    json=preset
)
print(response.json())

# Trigger flash effect
trigger = {
    "action": "flash",
    "channel": 0,
    "duration_ms": 300
}
response = requests.post(
    f'{BASE_URL}/api/trigger',
    json=trigger
)
print(response.json())

# Monitor events (SSE)
import sseclient

messages = sseclient.SSEClient(f'{BASE_URL}/events')
for msg in messages:
    if msg.event == 'status':
        print(f"Status update: {msg.data}")
```

### Node.js Client

```javascript
const axios = require('axios');
const EventSource = require('eventsource');

const BASE_URL = 'http://192.168.1.100';

// Get status
async function getStatus() {
  const response = await axios.get(`${BASE_URL}/api/status`);
  console.log('Status:', response.data);
}

// Update config
async function updateConfig() {
  const config = {
    brightness: 0.85,
    node_name: 'bedroom-leds'
  };
  const response = await axios.post(`${BASE_URL}/api/config`, config);
  console.log('Config updated:', response.data);
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
  console.log('Preset created:', response.data);
}

// Trigger action
async function triggerFlash() {
  const trigger = {
    action: 'flash',
    channel: 0,
    duration_ms: 300
  };
  const response = await axios.post(`${BASE_URL}/api/trigger`, trigger);
  console.log('Action triggered:', response.data);
}

// Monitor events
function monitorEvents() {
  const eventSource = new EventSource(`${BASE_URL}/events`);
  
  eventSource.addEventListener('status', (event) => {
    const data = JSON.parse(event.data);
    console.log('Status event:', data);
  });
  
  eventSource.addEventListener('error', (error) => {
    console.error('SSE error:', error);
  });
}

// Run all examples
(async () => {
  await getStatus();
  await updateConfig();
  await createPreset();
  await triggerFlash();
  monitorEvents();
})();
```

### cURL Examples

```bash
# Get status
curl http://192.168.1.100/api/status

# Get configuration
curl http://192.168.1.100/api/config

# Update brightness
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{"brightness": 0.9}'

# List presets
curl http://192.168.1.100/api/presets

# Create preset
curl -X POST http://192.168.1.100/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "cool_blue",
    "effect_id": 1103,
    "speed": 1.0,
    "intensity": 0.8,
    "color1": [0, 50, 255]
  }'

# Trigger blackout
curl -X POST http://192.168.1.100/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "blackout"}'

# Monitor events
curl -N http://192.168.1.100/events

# Upload OTA firmware
curl -X POST http://192.168.1.100/api/ota/upload \
  -H "Content-Type: application/octet-stream" \
  --data-binary @firmware.bin
```

---

## Error Handling

### HTTP Status Codes

- `200 OK` - Successful request
- `204 No Content` - Successful deletion (no response body)
- `400 Bad Request` - Invalid request (malformed JSON, validation errors)
- `404 Not Found` - Resource not found
- `500 Internal Server Error` - Server error

### Error Response Format

Error responses include a descriptive message in plain text:

```
effect_id=500 out of range [1000,9999]
```

```
config must be JSON object
```

```
preset not found
```

### Common Errors

**Invalid JSON:**
```
Request:
curl -X POST http://192.168.1.100/api/config -d 'invalid'

Response (400):
json
```

**Validation Error:**
```
Request:
curl -X POST http://192.168.1.100/api/presets -d '{"preset_name":"test","effect_id":500}'

Response (400):
effect_id=500 out of range [1000,9999]
```

**Not Found:**
```
Request:
curl http://192.168.1.100/api/presets/nonexistent

Response (404):
preset not found
```

### Best Practices

1. **Always check HTTP status codes** before parsing response
2. **Handle timeouts** - Set reasonable timeout values (5-10 seconds)
3. **Implement retry logic** for transient errors (500 errors)
4. **Validate before sending** - Use JSON schemas client-side
5. **Monitor SSE connections** - Implement reconnection logic
6. **Use CORS headers** if accessing from web applications

---

## Rate Limiting

**Current Status:** No rate limiting (v1.0.0)

**Recommendations:**
- Limit status polling to 1 Hz (once per second)
- Batch configuration updates when possible
- Use SSE for real-time updates instead of polling

---

## Versioning

API version is included in firmware version. Check `/api/status` for current version.

**Breaking Changes:** Will be announced in release notes and documented in CHANGELOG.

---

## Support and Resources

- **OpenAPI Specification:** [api_spec.yaml](api_spec.yaml)
- **Examples:** [API_EXAMPLES.md](API_EXAMPLES.md)
- **Schemas:** [/firmware/schemas](/firmware/schemas)
- **Source Code:** [rest_api.c](/firmware/components/rest_api/rest_api.c)
- **Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues

---

**Last Updated:** 2026-01-08  
**API Version:** 1.0.0  
**Protocol:** HTTP/1.1
