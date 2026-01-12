# ESP32 LED Node Integration Guide

This guide explains how to integrate LumiGrid Display Nodes (Raspberry Pi) with ESP32 LED Nodes for coordinated lighting and display systems.

## Table of Contents

- [Overview](#overview)
- [Integration Architecture](#integration-architecture)
- [Preset Schema Alignment](#preset-schema-alignment)
- [UDP Synchronization](#udp-synchronization)
- [Sensor Data Bridging](#sensor-data-bridging)
- [Configuration Examples](#configuration-examples)
- [Troubleshooting](#troubleshooting)

## Overview

LumiGrid supports integration with ESP32-based LED nodes to create unified lighting and display systems. The integration enables:

- **Synchronized Playback**: Display and LED nodes stay in perfect sync via UDP
- **Shared Presets**: Use compatible preset formats across both platforms
- **Sensor Bridging**: ESP32 sensors can send data to Pi displays
- **Coordinated Control**: Trigger synchronized effects across all nodes

## Integration Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Network (LAN/WiFi)                        │
└──────┬────────────────────┬─────────────────────┬───────────┘
       │                    │                     │
       │                    │                     │
┌──────▼──────────┐  ┌──────▼──────────┐  ┌──────▼──────────┐
│  Display Node   │  │  Display Node   │  │   ESP32 LED     │
│  (Pi - Master)  │  │  (Pi - Slave)   │  │   Node (Slave)  │
│                 │  │                 │  │                 │
│  - LED Matrix   │  │  - LED Matrix   │  │  - LED Strip    │
│  - Broadcasts   │  │  - Syncs to     │  │  - Syncs to     │
│    UDP sync     │  │    Master       │  │    Master       │
│  - Hosts API    │  │                 │  │  - Sends sensor │
│                 │  │                 │  │    data to Pi   │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

### Communication Protocols

1. **UDP Multicast (Sync Protocol)**
   - Purpose: Time synchronization across all nodes
   - Port: 45454
   - Group: 239.10.7.42
   - Rate: 20 Hz (50ms intervals)

2. **HTTP REST API**
   - Purpose: Control, configuration, and sensor data
   - Default Port: 5000
   - Protocol: HTTP/JSON

3. **mDNS (Service Discovery)**
   - Purpose: Automatic node discovery on local network
   - Service Type: `_lumigrid._tcp`

## Preset Schema Alignment

Both Display Nodes (Pi) and LED Nodes (ESP32) use compatible preset schemas for coordinated effects.

### Display Node Preset Format

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

### ESP32 LED Node Preset Format

ESP32 LED nodes use a simplified format focusing on color and effect parameters:

```json
{
  "preset_name": "led_red_pulse",
  "description": "Red pulsing effect",
  "effect_config": {
    "type": "pulse",
    "duration": 10,
    "colors": {
      "primary": "#FF0000",
      "secondary": "#CC0000"
    },
    "speed": 1.0,
    "brightness": 50
  }
}
```

### Shared Color Schema

Both platforms use hex color codes for consistency:

```json
{
  "colors": {
    "background": "#000000",  // Black
    "text": "#FF0000",        // Red
    "accent": "#CC0000",      // Dark red
    "primary": "#00FFFF",     // Cyan
    "secondary": "#FF00FF"    // Magenta
  }
}
```

### Cross-Platform Preset Coordination

To create coordinated effects across displays and LEDs:

**1. Match Colors:**

```json
// Display Node
{
  "preset_name": "coordinated_cyan",
  "frame_config": {
    "type": "time",
    "colors": {
      "background": "#000000",
      "text": "#00FFFF"
    }
  }
}

// ESP32 LED Node
{
  "preset_name": "coordinated_cyan_led",
  "effect_config": {
    "type": "solid",
    "colors": {
      "primary": "#00FFFF"
    }
  }
}
```

**2. Match Timing:**

```json
// Both use same duration
{
  "duration": 10  // 10 seconds
}
```

**3. Trigger Simultaneously:**

```bash
# Trigger on Display Node
curl -X POST http://display-node:5000/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "play_preset", "name": "coordinated_cyan"}'

# Trigger on ESP32 LED Node (if it has HTTP API)
curl -X POST http://esp32-node/preset \
  -H "Content-Type: application/json" \
  -d '{"preset": "coordinated_cyan_led"}'
```

## UDP Synchronization

### Overview

UDP sync keeps all nodes (Pi and ESP32) synchronized to within milliseconds.

See [docs/UDP_SYNCHRONIZATION.md](docs/UDP_SYNCHRONIZATION.md) for complete protocol details.

### Setting Up a Master Display Node

**config.yaml:**

```yaml
network:
  hostname: "lumigrid-master"
  mdns_enabled: true

control:
  mode: "master"              # This node broadcasts sync
  sync_role: "master"
  external_source: null

sync:
  enabled: true
  multicast_group: "239.10.7.42"
  port: 45454
  broadcast_rate_hz: 20       # 20 packets per second
```

**Start the master:**

```bash
sudo systemctl restart lumigrid
```

### Setting Up a Slave Display Node

**config.yaml:**

```yaml
network:
  hostname: "lumigrid-slave-01"
  mdns_enabled: true

control:
  mode: "slave"               # This node listens for sync
  sync_role: "slave"
  external_source: "udp"      # Sync source is UDP

sync:
  enabled: true
  multicast_group: "239.10.7.42"
  port: 45454
  timeout_ms: 1000            # Desync if no packet for 1 second
```

### ESP32 LED Node Configuration

For ESP32 nodes to sync with Display Nodes, implement UDP sync client:

**Arduino/ESP32 Example:**

```cpp
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Sync configuration
const char* MULTICAST_ADDR = "239.10.7.42";
const uint16_t MULTICAST_PORT = 45454;

WiFiUDP udp;
IPAddress multicastIP;

void setupSync() {
  multicastIP.fromString(MULTICAST_ADDR);
  udp.beginMulticast(multicastIP, MULTICAST_PORT);
}

void handleSyncPacket() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 255);
    packetBuffer[len] = 0;
    
    // Parse JSON
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, packetBuffer);
    
    if (!error) {
      const char* type = doc["type"];
      uint32_t timestamp_ms = doc["timestamp_ms"];
      
      if (strcmp(type, "tick") == 0) {
        // Sync LED animations to this timestamp
        syncAnimation(timestamp_ms);
      }
    }
  }
}

void loop() {
  handleSyncPacket();
  // ... LED animation code
}
```

### Monitoring Sync Status

**Check sync status via API:**

```bash
curl http://display-node:5000/api/status
```

Response includes sync state:

```json
{
  "mode": "slave",
  "sync": {
    "enabled": true,
    "clock_state": "synced",
    "drift_ewma_ms": 2.3,
    "packets_received": 1543
  }
}
```

**Clock states:**
- `local` - Not syncing (master or independent)
- `syncing` - Attempting to sync
- `synced` - Successfully synchronized
- `desync` - Lost sync with master

## Sensor Data Bridging

ESP32 nodes can send sensor data to Display Nodes for visualization.

### ESP32 Sending Sensor Data

**Arduino/ESP32 Example:**

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* DISPLAY_NODE_IP = "192.168.1.100";
const int DISPLAY_NODE_PORT = 5000;

void sendSensorData(float temperature, float humidity) {
  HTTPClient http;
  
  String url = String("http://") + DISPLAY_NODE_IP + 
               ":" + DISPLAY_NODE_PORT + "/api/sensor_update";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON payload
  StaticJsonDocument<200> doc;
  doc["node_id"] = "esp32-sensor-01";
  doc["timestamp"] = millis() / 1000;
  
  JsonObject readings = doc.createNestedObject("readings");
  readings["temperature"] = temperature;
  readings["humidity"] = humidity;
  
  String payload;
  serializeJson(doc, payload);
  
  // Send POST request
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Sensor data sent: " + response);
  }
  
  http.end();
}

void loop() {
  float temp = readTemperature();
  float humidity = readHumidity();
  
  sendSensorData(temp, humidity);
  delay(5000);  // Send every 5 seconds
}
```

### Display Node Receiving Sensor Data

Display nodes automatically accept sensor data on `/api/sensor_update`.

**Verify sensor data endpoint:**

```bash
curl -X POST http://display-node:5000/api/sensor_update \
  -H "Content-Type: application/json" \
  -d '{
    "node_id": "esp32-test",
    "readings": {
      "temperature": 23.5,
      "humidity": 55.0
    },
    "timestamp": 1234567890
  }'
```

**Response:**

```json
{
  "success": true,
  "message": "Sensor data received"
}
```

### Display Sensor Data on Screen

Create a preset that shows sensor data:

```json
{
  "preset_name": "esp32_temperature",
  "description": "Show temperature from ESP32",
  "frame_config": {
    "type": "temperature",
    "name": "ESP32 Temp",
    "enabled": true,
    "duration": 5,
    "colors": {
      "background": "#000000",
      "text": "#FF8000"
    },
    "font": {
      "size": 16
    },
    "unit": "celsius",
    "source": "external"  // Use external sensor data
  }
}
```

## Configuration Examples

### Example 1: Single Master with Multiple Slaves

**Network Setup:**
- 1 Display Node (Master): 192.168.1.100
- 2 Display Nodes (Slaves): 192.168.1.101, 192.168.1.102
- 2 ESP32 LED Nodes (Slaves): 192.168.1.150, 192.168.1.151

**Master Node (192.168.1.100) - config.yaml:**

```yaml
network:
  hostname: "lumigrid-master"
  mdns_enabled: true

control:
  mode: "master"
  sync_role: "master"

sync:
  enabled: true
  multicast_group: "239.10.7.42"
  port: 45454
  broadcast_rate_hz: 20
```

**Slave Display Nodes (192.168.1.101, 192.168.1.102) - config.yaml:**

```yaml
network:
  hostname: "lumigrid-slave-01"  # Change for each node
  mdns_enabled: true

control:
  mode: "slave"
  sync_role: "slave"
  external_source: "udp"

sync:
  enabled: true
  multicast_group: "239.10.7.42"
  port: 45454
```

**ESP32 LED Nodes - Arduino Code:**

```cpp
const char* MULTICAST_ADDR = "239.10.7.42";
const uint16_t MULTICAST_PORT = 45454;
// ... (sync setup code from earlier example)
```

### Example 2: Sensor-Driven Display System

**Setup:**
- 1 Display Node: Shows temperature and humidity
- 2 ESP32 Sensor Nodes: Collect environmental data
- All nodes synchronized

**ESP32 Sensor Node Code:**

```cpp
void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  setupSync();  // Enable sync reception
  setupSensors();
}

void loop() {
  handleSyncPacket();  // Stay in sync
  
  // Read sensors every 5 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000) {
    float temp = readDHT22Temperature();
    float humidity = readDHT22Humidity();
    
    sendSensorData(temp, humidity);
    lastSend = millis();
  }
}
```

**Display Node Timeline:**

```yaml
timelines:
  - id: "sensor-display"
    name: "Sensor Data Display"
    enabled: true
    loop: true
    entries:
      - id: "show-temp"
        preset_name: "esp32_temperature"
        trigger_type: "interval"
        interval: 10
        enabled: true
      
      - id: "show-humidity"
        preset_name: "esp32_humidity"
        trigger_type: "interval"
        interval: 10
        enabled: true
```

### Example 3: Coordinated Color Theme

**Shared Color Palette:**

```json
{
  "theme": "sunset",
  "colors": {
    "background": "#000000",
    "primary": "#FF6B35",
    "secondary": "#F7931E",
    "accent": "#FFB400"
  }
}
```

**Display Node Preset:**

```json
{
  "preset_name": "sunset_time",
  "frame_config": {
    "type": "time",
    "colors": {
      "background": "#000000",
      "text": "#FF6B35",
      "accent": "#F7931E"
    }
  }
}
```

**ESP32 LED Preset:**

```json
{
  "preset_name": "sunset_led",
  "effect_config": {
    "type": "gradient",
    "colors": {
      "primary": "#FF6B35",
      "secondary": "#F7931E",
      "accent": "#FFB400"
    }
  }
}
```

## Troubleshooting

### Sync Issues

**Problem: Slave nodes not syncing**

1. Verify multicast is enabled on network:
   ```bash
   # On Pi, check multicast route
   route -n | grep 239.10.7.42
   ```

2. Check firewall allows UDP port 45454:
   ```bash
   sudo ufw allow 45454/udp
   ```

3. Verify master is broadcasting:
   ```bash
   # On master node
   curl http://localhost:5000/api/status
   # Check sync.packets_sent is increasing
   ```

4. Monitor sync packets on slave:
   ```bash
   # On slave node
   sudo tcpdump -i any -n udp port 45454
   ```

**Problem: High drift (>50ms)**

1. Reduce network latency
2. Check CPU load on nodes
3. Ensure master has stable clock (RTC recommended)
4. Increase `gpio_slowdown` if display is causing CPU spikes

### Sensor Data Issues

**Problem: ESP32 sensor data not appearing**

1. Verify HTTP endpoint is accessible:
   ```bash
   curl http://display-node:5000/api/discover/needs_sensor_data
   ```

2. Check ESP32 can reach display node:
   ```bash
   # From ESP32 serial monitor
   ping display-node-ip
   ```

3. Verify JSON format:
   ```bash
   # Test with curl
   curl -X POST http://display-node:5000/api/sensor_update \
     -H "Content-Type: application/json" \
     -d '{"node_id":"test","readings":{"temperature":25.0}}'
   ```

4. Check display node logs:
   ```bash
   sudo journalctl -u lumigrid -f | grep sensor
   ```

### Preset Compatibility

**Problem: Presets not working across platforms**

1. Verify color format is hex (e.g., `#FF0000` not `255,0,0`)
2. Check duration is in seconds (integer)
3. Ensure `type` field matches supported types on each platform
4. Validate JSON syntax:
   ```bash
   cat preset.json | python -m json.tool
   ```

### Network Discovery

**Problem: Nodes not discovering each other**

1. Ensure mDNS is enabled:
   ```yaml
   network:
     mdns_enabled: true
   ```

2. Install Avahi (should be installed by install.sh):
   ```bash
   sudo apt-get install avahi-daemon
   ```

3. Test discovery:
   ```bash
   avahi-browse -a
   # Look for _lumigrid._tcp
   ```

## Best Practices

### Network Configuration

1. **Use Static IPs** for critical nodes (master)
2. **Enable QoS** on router for multicast traffic
3. **Isolate LumiGrid Network** on dedicated VLAN if possible
4. **Monitor Network Load** with tools like `iftop`

### Sync Configuration

1. **Single Master** - Only one node should be master
2. **Stable Master Clock** - Use RTC on master for best results
3. **Monitor Drift** - Alert if drift exceeds 50ms
4. **Graceful Degradation** - Slaves should work independently if sync fails

### Preset Management

1. **Use Descriptive Names** - e.g., `blue_time_12h` not `preset1`
2. **Version Control** - Keep presets in git repository
3. **Test Individually** - Verify each preset before combining
4. **Document Colors** - Maintain color palette reference

### Development Workflow

1. **Test Locally** - Develop presets on single node first
2. **Staged Rollout** - Deploy to one slave before all
3. **Monitor Logs** - Watch for errors during deployment
4. **Backup Configs** - Keep backup of working configurations

## Additional Resources

- **[UDP Synchronization Guide](docs/UDP_SYNCHRONIZATION.md)** - Complete sync protocol documentation
- **[API Reference](docs/API_REFERENCE.md)** - Full REST API documentation
- **[Preset Schema](schemas/preset-display.schema.json)** - JSON schema for presets
- **[Example Presets](preset_examples/)** - Sample preset files
- **[Timeline Sequencer](TIMELINE_SEQUENCER.md)** - Automated scheduling

## Support

For issues or questions:

- **GitHub Issues**: https://github.com/algorhythmicdev/lumigrid_display_node/issues
- **Discussions**: https://github.com/algorhythmicdev/lumigrid_display_node/discussions

---

**Happy integrating!** 🎨✨
