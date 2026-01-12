# LumiGrid API Reference

Complete REST and MQTT API documentation for LumiGrid nodes. All nodes expose a consistent API schema regardless of hardware type.

## Table of Contents

1. [REST API](#rest-api)
2. [MQTT API](#mqtt-api)
3. [Server-Sent Events (SSE)](#server-sent-events-sse)
4. [Sync Protocol](#sync-protocol)
5. [Error Handling](#error-handling)
6. [Code Examples](#code-examples)

---

## REST API

All nodes expose a REST API over HTTP on port 80 (or HTTPS on port 443 if configured).

### Base URL

```
http://<node-id>.local
```

Or use direct IP:

```
http://192.168.1.100
```

### Authentication

Optional HTTP Basic Auth or bearer token:

```bash
curl -u admin:password http://node.local/api/status
# OR
curl -H "Authorization: Bearer <token>" http://node.local/api/status
```

---

### GET /api/status

Get current node status, health, and runtime information.

#### Response

```json
{
  "node_id": "led-node-001",
  "role": "participant",
  "mode": "playing",
  "uptime_ms": 3456789,
  "heap_free_kb": 127,
  "heap_min_kb": 98,
  "fps": 60,
  "wifi_rssi": -45,
  "ip_address": "192.168.1.100",
  "firmware_version": "v2.3.1",
  "last_sync_ms_ago": 42,
  "power_limit_active": false,
  "thermal_celsius": 48.2
}
```

#### Fields

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | string | Unique node identifier |
| `role` | enum | `"master"`, `"participant"`, or `"standalone"` |
| `mode` | enum | `"playing"`, `"paused"`, `"blackout"`, `"idle"` |
| `uptime_ms` | number | Milliseconds since boot |
| `heap_free_kb` | number | Free heap memory in KB |
| `heap_min_kb` | number | Minimum free heap since boot |
| `fps` | number | Current effect render frame rate |
| `wifi_rssi` | number | Wi-Fi signal strength (dBm) |
| `ip_address` | string | Current IP address |
| `firmware_version` | string | Semantic version string |
| `last_sync_ms_ago` | number | Time since last master clock sync |
| `power_limit_active` | boolean | True if power budget is limiting brightness |
| `thermal_celsius` | number | Current temperature reading |

#### Example

```bash
curl http://led-node-001.local/api/status | jq
```

---

### GET /api/config

Retrieve current node configuration.

#### Response

```json
{
  "node_id": "led-node-001",
  "role": "participant",
  "network": {
    "wifi_ssid": "LumiGrid-Net",
    "static_ip": "192.168.1.100",
    "gateway": "192.168.1.1"
  },
  "hardware": {
    "pwm_channels": 8,
    "aled_channels": 8,
    "aled_pixels_per_channel": [60, 60, 30, 30, 144, 144, 0, 0]
  },
  "limits": {
    "power_limit_watts": 150,
    "thermal_limit_celsius": 75
  },
  "sync": {
    "master_ip": "192.168.1.1",
    "multicast_group": "239.1.1.1",
    "multicast_port": 4210
  }
}
```

#### Example

```bash
curl http://led-node-001.local/api/config
```

---

### POST /api/config

Update node configuration (merge or replace).

#### Request Body

```json
{
  "merge": true,
  "config": {
    "limits": {
      "power_limit_watts": 120
    }
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `merge` | boolean | No | If true, merge with existing config; if false, replace entirely |
| `config` | object | Yes | Configuration object to apply |

#### Response

```json
{
  "success": true,
  "message": "Configuration updated",
  "restart_required": false
}
```

#### Example

```bash
curl -X POST http://led-node-001.local/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "merge": true,
    "config": {
      "limits": {
        "power_limit_watts": 120
      }
    }
  }'
```

---

### GET /api/presets

List all stored presets.

#### Response

```json
{
  "presets": [
    {
      "name": "ambient-glow",
      "effect": "solid",
      "params": {
        "color": "#ff9500",
        "brightness": 0.5
      }
    },
    {
      "name": "rainbow-chase",
      "effect": "rainbow",
      "params": {
        "speed": 1.5,
        "density": 0.8
      }
    }
  ]
}
```

#### Example

```bash
curl http://led-node-001.local/api/presets
```

---

### POST /api/presets

Save a new preset or update an existing one.

#### Request Body

```json
{
  "name": "custom-red",
  "effect": "solid",
  "params": {
    "color": "#ff0000",
    "brightness": 0.8
  }
}
```

#### Response

```json
{
  "success": true,
  "message": "Preset 'custom-red' saved"
}
```

#### Example

```bash
curl -X POST http://led-node-001.local/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "name": "custom-red",
    "effect": "solid",
    "params": {
      "color": "#ff0000",
      "brightness": 0.8
    }
  }'
```

---

### DELETE /api/presets/:name

Delete a stored preset.

#### Response

```json
{
  "success": true,
  "message": "Preset 'custom-red' deleted"
}
```

#### Example

```bash
curl -X DELETE http://led-node-001.local/api/presets/custom-red
```

---

### POST /api/trigger

Execute an immediate action (play preset, blackout, beat pulse, etc.).

#### Request Body

```json
{
  "action": "play_preset",
  "preset": "rainbow-chase"
}
```

#### Supported Actions

| Action | Parameters | Description |
|--------|-----------|-------------|
| `play_preset` | `preset` (string) | Load and play a stored preset |
| `set_pwm` | `channel` (0-15), `value` (0-4095) | Set PWM channel output |
| `set_aled` | `channel` (0-7), `color` (hex), `pixels` (array or "all") | Set addressable LED color |
| `blackout` | — | Immediately cut all outputs to zero |
| `beat` | — | Trigger beat-reactive flash |
| `resume` | — | Resume from paused or blackout state |

#### Examples

##### Play Preset

```bash
curl -X POST http://led-node-001.local/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "play_preset", "preset": "rainbow-chase"}'
```

##### Set PWM Channel

```bash
curl -X POST http://led-node-001.local/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "set_pwm", "channel": 0, "value": 2048}'
```

##### Blackout

```bash
curl -X POST http://led-node-001.local/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "blackout"}'
```

---

### POST /api/cue

Schedule a time-bound action (scene, transition, or clip).

#### Request Body

```json
{
  "name": "sunset-fade",
  "start_time_ms": 1704730000000,
  "duration_ms": 30000,
  "preset": "warm-fade-out"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Cue identifier |
| `start_time_ms` | number | Yes | Unix timestamp in milliseconds |
| `duration_ms` | number | Yes | Duration in milliseconds |
| `preset` | string | Yes | Preset to activate during cue |

#### Response

```json
{
  "success": true,
  "cue_id": "cue-1704730000"
}
```

#### Example

```bash
curl -X POST http://led-node-001.local/api/cue \
  -H "Content-Type: application/json" \
  -d '{
    "name": "sunset-fade",
    "start_time_ms": 1704730000000,
    "duration_ms": 30000,
    "preset": "warm-fade-out"
  }'
```

---

### GET /api/discover

Discover other nodes on the network.

#### Response

```json
{
  "nodes": [
    {
      "node_id": "led-node-002",
      "ip": "192.168.1.101",
      "role": "participant",
      "rssi": -52
    },
    {
      "node_id": "hub-001",
      "ip": "192.168.1.1",
      "role": "master",
      "rssi": -38
    }
  ]
}
```

#### Example

```bash
curl http://led-node-001.local/api/discover
```

---

## MQTT API

Nodes can optionally connect to an MQTT broker for cloud integration and remote control.

### Configuration

Enable MQTT in node config:

```json
{
  "mqtt": {
    "enabled": true,
    "broker": "mqtt.lumigrid.dev",
    "port": 1883,
    "username": "lumigrid-user",
    "password": "secret",
    "client_id": "led-node-001"
  }
}
```

### Topic Structure

```
lumigrid/<node_id>/<topic>
```

### Published Topics (Node → Broker)

| Topic | Payload | Description |
|-------|---------|-------------|
| `lumigrid/<node_id>/status` | JSON | Node status (same as REST `/api/status`) |
| `lumigrid/<node_id>/events` | JSON | Events (sensor triggers, errors) |
| `lumigrid/<node_id>/telemetry` | JSON | Periodic health data |

### Subscribed Topics (Broker → Node)

| Topic | Payload | Description |
|-------|---------|-------------|
| `lumigrid/<node_id>/trigger` | JSON | Execute action (same as REST `/api/trigger`) |
| `lumigrid/<node_id>/config` | JSON | Update configuration |
| `lumigrid/all/trigger` | JSON | Broadcast trigger to all nodes |
| `lumigrid/all/cue` | JSON | Broadcast cue to all nodes |

### Examples

#### Subscribe to Node Status

```bash
mosquitto_sub -h mqtt.lumigrid.dev -t "lumigrid/led-node-001/status"
```

#### Trigger Blackout

```bash
mosquitto_pub -h mqtt.lumigrid.dev \
  -t "lumigrid/led-node-001/trigger" \
  -m '{"action": "blackout"}'
```

#### Broadcast to All Nodes

```bash
mosquitto_pub -h mqtt.lumigrid.dev \
  -t "lumigrid/all/trigger" \
  -m '{"action": "play_preset", "preset": "emergency-red"}'
```

---

## Server-Sent Events (SSE)

Real-time event stream for monitoring node state changes.

### Endpoint

```
GET /events
```

### Event Types

| Event | Data | Description |
|-------|------|-------------|
| `status` | JSON | Node status update |
| `playhead` | JSON | Current playback position |
| `power_limit` | JSON | Power budget state change |
| `error` | JSON | Error or warning message |

### Example Stream

```
event: status
data: {"mode":"playing","fps":60}

event: playhead
data: {"position_ms":12345,"duration_ms":60000}

event: power_limit
data: {"active":true,"brightness_scale":0.75}
```

### JavaScript Client

```javascript
const eventSource = new EventSource('http://led-node-001.local/events');

eventSource.addEventListener('status', (e) => {
  const data = JSON.parse(e.data);
  console.log('Status update:', data);
});

eventSource.addEventListener('playhead', (e) => {
  const data = JSON.parse(e.data);
  updateProgressBar(data.position_ms / data.duration_ms);
});

eventSource.addEventListener('power_limit', (e) => {
  const data = JSON.parse(e.data);
  if (data.active) {
    showWarning('Power limit active, brightness reduced');
  }
});
```

### Python Client

```python
import sseclient
import requests
import json

url = 'http://led-node-001.local/events'
response = requests.get(url, stream=True)
client = sseclient.SSEClient(response)

for event in client.events():
    if event.event == 'status':
        data = json.loads(event.data)
        print(f"Status: {data['mode']}, FPS: {data['fps']}")
    elif event.event == 'power_limit':
        data = json.loads(event.data)
        if data['active']:
            print(f"⚠️  Power limit active, scaled to {data['brightness_scale']*100:.0f}%")
```

---

## Sync Protocol

The LumiGrid sync protocol uses UDP multicast for low-latency, distributed timing.

### Multicast Configuration

- **Group**: `239.1.1.1` (configurable)
- **Port**: `4210` (configurable)
- **Rate**: 10 Hz (every 100ms)

### Tick Packet

Broadcast by master node:

```json
{
  "type": "tick",
  "master_id": "hub-001",
  "timestamp_ms": 1704730123456,
  "bpm": 120,
  "beat_phase": 0.25
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"tick"` |
| `master_id` | string | Master node identifier |
| `timestamp_ms` | number | Unix timestamp in milliseconds |
| `bpm` | number | Beats per minute (optional) |
| `beat_phase` | number | Phase within beat cycle (0.0-1.0) |

### Cue Packet

Broadcast cue to all nodes:

```json
{
  "type": "cue",
  "master_id": "hub-001",
  "timestamp_ms": 1704730123456,
  "cue_name": "sunrise-sequence",
  "target_nodes": ["led-*"]
}
```

### Python UDP Listener

```python
import socket
import json

MCAST_GRP = '239.1.1.1'
MCAST_PORT = 4210

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('', MCAST_PORT))

mreq = socket.inet_aton(MCAST_GRP) + socket.inet_aton('0.0.0.0')
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

print("Listening for LumiGrid sync packets...")

while True:
    data, addr = sock.recvfrom(1024)
    packet = json.loads(data.decode('utf-8'))
    
    if packet['type'] == 'tick':
        print(f"⏱️  Tick from {packet['master_id']}, BPM: {packet.get('bpm', 'N/A')}")
    elif packet['type'] == 'cue':
        print(f"🎬 Cue: {packet['cue_name']}")
```

---

## Error Handling

### HTTP Status Codes

| Code | Meaning | Example |
|------|---------|---------|
| `200` | Success | Request completed successfully |
| `400` | Bad Request | Invalid JSON or missing required field |
| `401` | Unauthorized | Authentication required |
| `404` | Not Found | Preset or resource doesn't exist |
| `500` | Internal Error | Node firmware error |
| `503` | Service Unavailable | Node is booting or updating |

### Error Response Format

```json
{
  "success": false,
  "error": "preset_not_found",
  "message": "Preset 'invalid-name' does not exist"
}
```

---

## Code Examples

### JavaScript: Full Control Panel

```javascript
class LumiGridNode {
  constructor(baseUrl) {
    this.baseUrl = baseUrl;
    this.eventSource = null;
  }

  async getStatus() {
    const res = await fetch(`${this.baseUrl}/api/status`);
    return await res.json();
  }

  async playPreset(presetName) {
    const res = await fetch(`${this.baseUrl}/api/trigger`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'play_preset', preset: presetName })
    });
    return await res.json();
  }

  async blackout() {
    const res = await fetch(`${this.baseUrl}/api/trigger`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'blackout' })
    });
    return await res.json();
  }

  subscribeToEvents(callbacks) {
    this.eventSource = new EventSource(`${this.baseUrl}/events`);
    
    this.eventSource.addEventListener('status', (e) => {
      callbacks.onStatus?.(JSON.parse(e.data));
    });
    
    this.eventSource.addEventListener('playhead', (e) => {
      callbacks.onPlayhead?.(JSON.parse(e.data));
    });
  }

  disconnect() {
    this.eventSource?.close();
  }
}

// Usage
const node = new LumiGridNode('http://led-node-001.local');

node.subscribeToEvents({
  onStatus: (data) => console.log('Status:', data),
  onPlayhead: (data) => console.log('Playhead:', data)
});

await node.playPreset('rainbow-chase');
```

### Python: Multi-Node Controller

```python
import requests
import asyncio
from typing import List

class LumiGridController:
    def __init__(self, nodes: List[str]):
        self.nodes = nodes  # ["http://node1.local", "http://node2.local"]
    
    def trigger_all(self, action: str, **params):
        """Trigger action on all nodes simultaneously"""
        payload = {"action": action, **params}
        
        tasks = []
        for node_url in self.nodes:
            url = f"{node_url}/api/trigger"
            tasks.append(
                requests.post(url, json=payload)
            )
        
        return [r.json() for r in tasks]
    
    def play_preset_all(self, preset_name: str):
        return self.trigger_all("play_preset", preset=preset_name)
    
    def blackout_all(self):
        return self.trigger_all("blackout")
    
    def get_status_all(self):
        results = []
        for node_url in self.nodes:
            res = requests.get(f"{node_url}/api/status")
            results.append(res.json())
        return results

# Usage
controller = LumiGridController([
    "http://led-node-001.local",
    "http://led-node-002.local",
    "http://led-node-003.local"
])

# Play synchronized preset across all nodes
controller.play_preset_all("rainbow-chase")

# Get health status of all nodes
statuses = controller.get_status_all()
for status in statuses:
    print(f"{status['node_id']}: {status['mode']}, FPS: {status['fps']}")
```

### Bash: Quick Commands

```bash
#!/bin/bash
# lumigrid-cli.sh - Quick CLI for LumiGrid nodes

NODE_URL="http://led-node-001.local"

# Get status
status() {
  curl -s "$NODE_URL/api/status" | jq
}

# Play preset
play() {
  curl -s -X POST "$NODE_URL/api/trigger" \
    -H "Content-Type: application/json" \
    -d "{\"action\":\"play_preset\",\"preset\":\"$1\"}"
}

# Blackout
blackout() {
  curl -s -X POST "$NODE_URL/api/trigger" \
    -H "Content-Type: application/json" \
    -d '{"action":"blackout"}'
}

# Usage: ./lumigrid-cli.sh status
# Usage: ./lumigrid-cli.sh play rainbow-chase
# Usage: ./lumigrid-cli.sh blackout

"$@"
```

---

**LumiGrid API Reference** — Control your distributed lighting system with REST, MQTT, and real-time events.
