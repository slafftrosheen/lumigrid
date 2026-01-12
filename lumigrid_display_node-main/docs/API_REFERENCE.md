# LumiGrid Display Node REST API Reference

This document provides comprehensive documentation for the LumiGrid Display Node REST API endpoints, including request/response schemas, examples, and validation rules.

## Table of Contents

- [Overview](#overview)
- [Authentication](#authentication)
- [Error Responses](#error-responses)
- [Endpoints](#endpoints)
  - [Status](#status)
  - [Configuration](#configuration)
  - [Presets](#presets)
  - [Trigger Actions](#trigger-actions)
  - [Sensor Discovery](#sensor-discovery)
  - [Sensor Data Updates](#sensor-data-updates)

## Overview

The LumiGrid Display Node REST API provides programmatic control over LED matrix displays. It supports:

- Device configuration and status monitoring
- Display preset management (create, list, delete)
- Action triggering (play presets, change modes, set brightness, show messages)
- Sensor data bridging between ESP32 sensor nodes and Raspberry Pi display nodes

All endpoints use JSON for request and response bodies unless otherwise specified.

Base URL: `http://<device-ip>:5000/api`

## Authentication

Currently, the API does not require authentication. Rate limiting is enabled by default to prevent abuse.

## Error Responses

All error responses follow this format:

```json
{
  "error": "Error message describing what went wrong"
}
```

Common HTTP status codes:
- `200`: Success
- `400`: Bad request (invalid parameters, validation errors)
- `404`: Resource not found
- `500`: Internal server error

---

## Endpoints

### Status

#### GET /api/status

Get current device and system status.

**Response:**
```json
{
  "mode": "clock",
  "brightness": 50,
  "temperature": 25.0,
  "running": true,
  "uptime": 3600,
  "node_id": "lumigrid-display-01"
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

### Configuration

#### GET /api/config

Retrieve current device configuration.

**Response:**
```json
{
  "led_matrix": {
    "rows": 32,
    "cols": 64,
    "brightness": 50,
    "hardware_mapping": "regular",
    "chain_length": 1
  },
  "network": {
    "hostname": "lumigrid-01",
    "mdns_enabled": true,
    "port": 5000
  },
  "control": {
    "mode": "independent",
    "sync_role": null,
    "external_source": null
  },
  "time": {
    "timezone": "UTC",
    "ntp_enabled": true,
    "ntp_server": "pool.ntp.org"
  }
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/config

Update device configuration. Supports partial updates.

**Request:**
```json
{
  "led_matrix": {
    "brightness": 75
  },
  "network": {
    "hostname": "lumigrid-new"
  }
}
```

**Response:**
```json
{
  "success": true
}
```

**Status Codes:**
- `200`: Configuration updated
- `400`: Invalid configuration data
- `500`: Server error

---

### Presets

Display presets are reusable display configurations that can be saved and triggered on demand.

#### GET /api/presets

List all available display presets.

**Response:**
```json
{
  "presets": [
    {
      "preset_name": "display_time_small_red",
      "description": "Small time display, red color",
      "frame_config": {
        "id": "time_red_01",
        "type": "time",
        "name": "Small Red Time",
        "enabled": true,
        "duration": 10,
        "colors": {
          "background": "#000000",
          "text": "#FF0000",
          "accent": "#CC0000"
        },
        "font": {
          "path": "Minimal.ttf",
          "size": 16
        },
        "position": {
          "x": 5,
          "y": 8
        },
        "format": "24h"
      }
    },
    {
      "preset_name": "welcome_message",
      "description": "Welcome message display",
      "frame_config": {
        "id": "text_welcome",
        "type": "text",
        "name": "Welcome",
        "text": "Welcome to LumiGrid!",
        "enabled": true,
        "duration": 15,
        "colors": {
          "background": "#000000",
          "text": "#00FF00"
        },
        "font": {
          "size": 12
        },
        "scroll": true
      }
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/presets

Create a new display preset.

**Supported Frame Types:**
- `time`: Clock display with customizable format
- `date`: Date display
- `datetime`: Combined date and time
- `temperature`: Temperature reading display
- `humidity`: Humidity reading display
- `text`: Static text message
- `message`: Scrolling message
- `data_visualization`: Data charts and graphs
- `interactive`: Interactive displays
- `template`: Template-based displays
- `conditional`: Conditional displays based on sensor data
- `multi_page`: Multi-page displays
- `scrolling`: Scrolling content
- `animated`: Animated content

**Request Format 1 (Wrapped):**
```json
{
  "preset_name": "display_time_small_red",
  "description": "Small time display, red color",
  "frame_config": {
    "type": "time",
    "name": "Small Red Time",
    "enabled": true,
    "duration": 10,
    "colors": {
      "background": "#000000",
      "text": "#FF0000",
      "accent": "#CC0000"
    },
    "font": {
      "path": "Minimal.ttf",
      "size": 16
    },
    "position": {
      "x": 5,
      "y": 8
    },
    "format": "24h"
  }
}
```

**Request Format 2 (Direct):**
```json
{
  "type": "text",
  "name": "Welcome Message",
  "text": "Welcome!",
  "enabled": true,
  "duration": 15,
  "colors": {
    "background": "#000000",
    "text": "#00FF00"
  },
  "font": {
    "size": 12
  },
  "scroll": true
}
```

**Request Format 3 (Temperature Display):**
```json
{
  "preset_name": "temp_blue",
  "description": "Temperature in blue",
  "frame_config": {
    "type": "temperature",
    "name": "Blue Temperature",
    "enabled": true,
    "duration": 5,
    "unit": "celsius",
    "colors": {
      "background": "#000000",
      "text": "#0000FF"
    },
    "show_icon": true
  }
}
```

**Response:**
```json
{
  "success": true,
  "message": "Preset created"
}
```

**Validation Rules:**
- `type`: Required. Must be one of the supported frame types
- `name`: Optional. Maximum 100 characters
- `text`: Optional for text frames. Maximum 1000 characters
- `duration`: Optional. Must be between 1 and 3600 seconds
- `enabled`: Optional. Boolean value
- `colors.background`: Optional. Hex color format (#RGB or #RRGGBB)
- `colors.text`: Optional. Hex color format (#RGB or #RRGGBB)
- `colors.accent`: Optional. Hex color format (#RGB or #RRGGBB)
- `font.size`: Optional. Must be between 6 and 72
- `font.path`: Optional. Path to font file
- `position.x`: Optional. Integer between -1000 and 1000, or string ('center', 'left', 'right')
- `position.y`: Optional. Integer between -1000 and 1000, or string ('center', 'top', 'bottom')
- `unit`: Optional for temperature frames. Either 'celsius' or 'fahrenheit'

**Status Codes:**
- `200`: Preset created successfully
- `400`: Invalid preset data (validation error)
- `500`: Server error

---

#### DELETE /api/presets/{preset_name}

Delete a display preset by name.

**URL Parameters:**
- `preset_name`: Unique identifier/name of the preset to delete

**Example Request:**
```
DELETE /api/presets/display_time_small_red
```

**Response (Success):**
```json
{
  "success": true,
  "message": "Preset deleted"
}
```

**Response (Failure):**
```json
{
  "error": "Failed to delete preset"
}
```

**Status Codes:**
- `200`: Preset deleted successfully
- `500`: Preset not found or deletion failed

---

### Trigger Actions

#### POST /api/trigger

Trigger a specific action or schedule playback.

**Supported Actions:**
- `play_preset`: Activate a saved preset by name
- `set_mode`: Change display mode
- `set_brightness`: Adjust display brightness
- `show_message`: Display a text message

**Request Format (Play Preset):**
```json
{
  "action": "play_preset",
  "name": "display_time_small_red"
}
```

**Request Format (Set Mode):**
```json
{
  "action": "set_mode",
  "mode": "clock"
}
```

Valid modes: `clock`, `temperature`, `message`, `slideshow`, `frames`

**Request Format (Set Brightness):**
```json
{
  "action": "set_brightness",
  "value": 75
}
```

Brightness must be between 0 and 100.

**Request Format (Show Message):**
```json
{
  "action": "show_message",
  "message": "Hello LumiGrid!"
}
```

Message maximum length: 500 characters.

**Response (Success):**
```json
{
  "success": true,
  "action": "play_preset",
  "name": "display_time_small_red"
}
```

**Response (Error):**
```json
{
  "error": "Preset not found"
}
```

**Status Codes:**
- `200`: Action executed successfully
- `400`: Invalid action or missing parameters
- `404`: Resource not found (e.g., preset doesn't exist)
- `500`: Server error

---

### Sensor Discovery

#### GET /api/discover/needs_sensor_data

Discover sensor data requirements for this display node.

This endpoint is used by sensor nodes to determine which display nodes need sensor data and what types of data to send.

**Response:**
```json
{
  "needs_data": true,
  "types": ["temperature", "humidity", "motion", "light"],
  "node_id": "lumigrid-display-01",
  "node_type": "display"
}
```

**Response Fields:**
- `needs_data`: Boolean indicating if node accepts sensor data
- `types`: Array of sensor types this node is interested in
- `node_id`: Unique identifier for this display node
- `node_type`: Type of node (always "display" for display nodes)

**Status Codes:**
- `200`: Success

---

### Sensor Data Updates

#### POST /api/sensor_update

Receive sensor data from sensor nodes.

This endpoint accepts sensor data pushed from ESP32 or other sensor nodes. Used for bridging sensor data to display nodes.

**Request:**
```json
{
  "node_id": "lumigrid-sensor-01",
  "readings": {
    "temperature": 25.5,
    "humidity": 60.0,
    "motion": false,
    "light": 500
  },
  "timestamp": 1234567890
}
```

**Request Fields:**
- `node_id`: **Required**. Unique identifier of the sensor node
- `readings`: **Required**. Dictionary of sensor readings
- `timestamp`: Optional. Unix timestamp of readings

**Supported Reading Types:**
- `temperature`: Temperature in Celsius (float)
- `humidity`: Relative humidity percentage (float)
- `motion`: Motion detection (boolean)
- `light`: Light level in lux (integer)

**Response (Success):**
```json
{
  "success": true,
  "message": "Sensor data received"
}
```

**Response (Error):**
```json
{
  "error": "node_id and readings required"
}
```

**Status Codes:**
- `200`: Sensor data accepted
- `400`: Invalid request (missing required fields)
- `500`: Server error

---

### Frames Management

#### GET /api/frames

List all frames in the display queue.

**Response:**
```json
{
  "frames": [
    {
      "id": "time-display",
      "type": "time",
      "name": "Current Time",
      "enabled": true,
      "duration": 5,
      "colors": {
        "text": "#00FFFF",
        "background": "#000000"
      },
      "font": {
        "size": 16
      }
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/frames

Add a new frame to the display queue.

**Request:**
```json
{
  "id": "new-frame",
  "type": "temperature",
  "name": "Temperature Display",
  "enabled": true,
  "duration": 10,
  "colors": {
    "text": "#FF8000",
    "background": "#000000"
  },
  "unit": "celsius"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Frame added"
}
```

**Status Codes:**
- `200`: Frame added
- `400`: Invalid frame data
- `500`: Server error

---

#### GET /api/frames/<frame_id>

Get details of a specific frame.

**Response:**
```json
{
  "id": "time-display",
  "type": "time",
  "name": "Current Time",
  "enabled": true,
  "duration": 5
}
```

**Status Codes:**
- `200`: Success
- `404`: Frame not found
- `500`: Server error

---

#### PUT /api/frames/<frame_id>

Update an existing frame.

**Request:**
```json
{
  "duration": 15,
  "colors": {
    "text": "#FF0000"
  }
}
```

**Response:**
```json
{
  "success": true,
  "message": "Frame updated"
}
```

**Status Codes:**
- `200`: Frame updated
- `400`: Invalid frame data
- `404`: Frame not found
- `500`: Server error

---

#### DELETE /api/frames/<frame_id>

Delete a frame from the queue.

**Response:**
```json
{
  "success": true,
  "message": "Frame deleted"
}
```

**Status Codes:**
- `200`: Frame deleted
- `404`: Frame not found
- `500`: Server error

---

#### POST /api/frames/reorder

Reorder frames in the display queue.

**Request:**
```json
{
  "order": ["frame-2", "frame-1", "frame-3"]
}
```

**Response:**
```json
{
  "success": true,
  "message": "Frames reordered"
}
```

**Status Codes:**
- `200`: Frames reordered
- `400`: Invalid order data
- `500`: Server error

---

#### POST /api/frames/<frame_id>/toggle

Enable or disable a specific frame.

**Response:**
```json
{
  "success": true,
  "enabled": true,
  "message": "Frame toggled"
}
```

**Status Codes:**
- `200`: Frame toggled
- `404`: Frame not found
- `500`: Server error

---

### Font Management

#### GET /api/fonts

List available fonts.

**Response:**
```json
{
  "fonts": [
    {
      "name": "DejaVuSans.ttf",
      "path": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "type": "system"
    },
    {
      "name": "CustomFont.ttf",
      "path": "fonts/CustomFont.ttf",
      "type": "custom"
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/fonts/upload

Upload a custom font file.

**Request:** Multipart form data with file upload

**Response:**
```json
{
  "success": true,
  "message": "Font uploaded",
  "filename": "MyFont.ttf"
}
```

**Status Codes:**
- `200`: Font uploaded
- `400`: Invalid file or file too large
- `500`: Server error

---

#### POST /api/fonts/delete

Delete a custom font.

**Request:**
```json
{
  "filename": "MyFont.ttf"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Font deleted"
}
```

**Status Codes:**
- `200`: Font deleted
- `400`: Cannot delete system font
- `404`: Font not found
- `500`: Server error

---

### Network Configuration

#### GET /api/network

Get network configuration.

**Response:**
```json
{
  "hostname": "lumigrid-01",
  "mdns_enabled": true,
  "interfaces": ["eth0", "wlan0"]
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/network

Update network configuration.

**Request:**
```json
{
  "hostname": "lumigrid-master",
  "mdns_enabled": true
}
```

**Response:**
```json
{
  "success": true,
  "message": "Network configuration updated"
}
```

**Status Codes:**
- `200`: Configuration updated
- `400`: Invalid data
- `500`: Server error

---

### Time and Date

#### GET /api/time

Get current time from the system or RTC.

**Response:**
```json
{
  "timestamp": 1234567890,
  "datetime": "2024-01-08T14:30:00Z",
  "timezone": "UTC",
  "source": "rtc"
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/time

Set system time (requires appropriate permissions).

**Request:**
```json
{
  "timestamp": 1234567890
}
```

**Response:**
```json
{
  "success": true,
  "message": "Time updated"
}
```

**Status Codes:**
- `200`: Time updated
- `400`: Invalid timestamp
- `403`: Insufficient permissions
- `500`: Server error

---

### Control Mode

#### GET /api/control

Get current control mode and sync status.

**Response:**
```json
{
  "mode": "slave",
  "sync_role": "slave",
  "external_source": "udp",
  "sync_status": {
    "enabled": true,
    "clock_state": "synced",
    "drift_ewma_ms": 2.3,
    "packets_received": 1543
  }
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/control

Update control mode.

**Request:**
```json
{
  "mode": "master",
  "sync_role": "master"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Control mode updated"
}
```

**Status Codes:**
- `200`: Mode updated
- `400`: Invalid mode
- `500`: Server error

---

### Timeline Management

#### GET /api/timelines

List all timelines.

**Response:**
```json
{
  "timelines": [
    {
      "id": "daily-schedule",
      "name": "Daily Schedule",
      "description": "Automated daily rotation",
      "enabled": true,
      "loop": true,
      "entries": []
    }
  ]
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/timelines

Create a new timeline.

**Request:**
```json
{
  "id": "weekend-schedule",
  "name": "Weekend Schedule",
  "description": "Weekend display rotation",
  "enabled": true,
  "loop": true,
  "entries": []
}
```

**Response:**
```json
{
  "success": true,
  "message": "Timeline created"
}
```

**Status Codes:**
- `200`: Timeline created
- `400`: Invalid timeline data
- `500`: Server error

---

#### GET /api/timelines/<timeline_id>

Get details of a specific timeline.

**Response:**
```json
{
  "id": "daily-schedule",
  "name": "Daily Schedule",
  "enabled": true,
  "entries": [...]
}
```

**Status Codes:**
- `200`: Success
- `404`: Timeline not found
- `500`: Server error

---

#### PUT /api/timelines/<timeline_id>

Update an existing timeline.

**Request:**
```json
{
  "name": "Updated Schedule",
  "enabled": false
}
```

**Response:**
```json
{
  "success": true,
  "message": "Timeline updated"
}
```

**Status Codes:**
- `200`: Timeline updated
- `400`: Invalid data
- `404`: Timeline not found
- `500`: Server error

---

#### DELETE /api/timelines/<timeline_id>

Delete a timeline.

**Response:**
```json
{
  "success": true,
  "message": "Timeline deleted"
}
```

**Status Codes:**
- `200`: Timeline deleted
- `404`: Timeline not found
- `500`: Server error

---

#### POST /api/timelines/<timeline_id>/activate

Activate a timeline for playback.

**Response:**
```json
{
  "success": true,
  "message": "Timeline activated"
}
```

**Status Codes:**
- `200`: Timeline activated
- `404`: Timeline not found
- `500`: Server error

---

#### POST /api/timelines/<timeline_id>/deactivate

Deactivate a timeline.

**Response:**
```json
{
  "success": true,
  "message": "Timeline deactivated"
}
```

**Status Codes:**
- `200`: Timeline deactivated
- `404`: Timeline not found
- `500`: Server error

---

### Sequencer Status

#### GET /api/sequencer/status

Get timeline sequencer status.

**Response:**
```json
{
  "running": true,
  "active_timeline": "daily-schedule",
  "current_entry": "morning-clock",
  "next_trigger": "2024-01-08T18:00:00Z"
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

### System Management

#### POST /api/system/restart

Restart the LumiGrid service.

**Response:**
```json
{
  "success": true,
  "message": "System restarting"
}
```

**Status Codes:**
- `200`: Restart initiated
- `500`: Server error

---

#### GET /api/system/info

Get system information.

**Response:**
```json
{
  "version": "1.0.0",
  "hostname": "lumigrid-01",
  "uptime": 3600,
  "cpu_temp": 45.0,
  "memory_usage": 512
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

### LED Matrix Configuration

#### GET /api/led-matrix

Get LED matrix configuration.

**Response:**
```json
{
  "rows": 32,
  "cols": 64,
  "brightness": 50,
  "hardware_mapping": "regular",
  "chain_length": 1
}
```

**Status Codes:**
- `200`: Success
- `500`: Server error

---

#### POST /api/led-matrix

Update LED matrix configuration (requires restart).

**Request:**
```json
{
  "brightness": 75,
  "gpio_slowdown": 4
}
```

**Response:**
```json
{
  "success": true,
  "message": "LED matrix configuration updated",
  "requires_restart": true
}
```

**Status Codes:**
- `200`: Configuration updated
- `400`: Invalid configuration
- `500`: Server error

---

### Configuration Reload

#### POST /api/config/reload

Reload configuration from disk.

**Response:**
```json
{
  "success": true,
  "message": "Configuration reloaded"
}
```

**Status Codes:**
- `200`: Configuration reloaded
- `500`: Server error

---

## Integration Examples

### Python Example: Create and Trigger a Preset

```python
import requests
import json

BASE_URL = "http://192.168.1.100:5000/api"

# Create a time display preset
preset = {
    "preset_name": "my_clock",
    "description": "Custom clock display",
    "frame_config": {
        "type": "time",
        "name": "My Clock",
        "enabled": True,
        "duration": 10,
        "colors": {
            "background": "#000000",
            "text": "#00FFFF"
        },
        "font": {
            "size": 18
        },
        "format": "12h"
    }
}

# Create the preset
response = requests.post(f"{BASE_URL}/presets", json=preset)
print(f"Create preset: {response.json()}")

# Trigger the preset
trigger = {
    "action": "play_preset",
    "name": "my_clock"
}

response = requests.post(f"{BASE_URL}/trigger", json=trigger)
print(f"Trigger preset: {response.json()}")
```

### JavaScript/Node.js Example: Send Sensor Data

```javascript
const axios = require('axios');

const BASE_URL = 'http://192.168.1.100:5000/api';

// Send sensor data from ESP32
async function sendSensorData() {
  const sensorData = {
    node_id: 'esp32-sensor-01',
    readings: {
      temperature: 23.5,
      humidity: 55.0,
      light: 450
    },
    timestamp: Math.floor(Date.now() / 1000)
  };

  try {
    const response = await axios.post(`${BASE_URL}/sensor_update`, sensorData);
    console.log('Sensor data sent:', response.data);
  } catch (error) {
    console.error('Error sending sensor data:', error.response.data);
  }
}

sendSensorData();
```

### curl Example: Get Status and Update Brightness

```bash
# Get current status
curl http://192.168.1.100:5000/api/status

# Update brightness
curl -X POST http://192.168.1.100:5000/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "set_brightness", "value": 80}'
```

---

## Rate Limiting

The API implements rate limiting to prevent abuse. Default limits:
- 200 requests per day
- 50 requests per hour

Rate limiting can be configured or disabled in the server configuration.

---

## Support

For issues, feature requests, or contributions, visit the project repository:
https://github.com/algorhythmicdev/lumigrid_display_node
