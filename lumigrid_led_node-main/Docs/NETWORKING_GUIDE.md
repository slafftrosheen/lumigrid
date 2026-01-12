# LumiGrid LED Node - Networking Guide

**Version:** 1.0.0  
**Target Platform:** ESP32-WROOM-32U  
**Last Updated:** 2026-01-08

---

## Table of Contents

1. [Wi-Fi Configuration](#wi-fi-configuration)
2. [UDP Multicast Sync Protocol](#udp-multicast-sync-protocol)
3. [MQTT Integration](#mqtt-integration)
4. [Network Security](#network-security)
5. [Troubleshooting Network Issues](#troubleshooting-network-issues)
6. [Advanced Configuration](#advanced-configuration)

---

## Wi-Fi Configuration

### Operating Modes

LumiGrid LED Node supports two Wi-Fi modes:

#### 1. Access Point (AP) Mode
- **Default on first boot** or when saved credentials fail
- Creates its own Wi-Fi network
- Used for initial configuration

#### 2. Station (STA) Mode
- Connects to existing Wi-Fi network
- Preferred for production use
- Saves credentials to NVS (non-volatile storage)

### Access Point Mode

**Default Settings:**
- **SSID:** `LumiGrid-Setup`
- **Password:** `lumigrid-setup` (or open network depending on config)
- **IP Address:** `192.168.4.1`
- **DHCP Range:** `192.168.4.2` to `192.168.4.10`
- **Gateway:** `192.168.4.1`
- **DNS:** `192.168.4.1`

**Connecting to AP:**
1. Search for Wi-Fi network "LumiGrid-Setup"
2. Connect using password "lumigrid-setup"
3. Open browser to http://192.168.4.1
4. Configure device via web UI

**Use Cases:**
- Initial device setup
- Network troubleshooting
- Offline operation
- Configuration backup/restore

### Station Mode

**Configuration via Web UI:**
1. Connect to device AP (see above)
2. Navigate to Settings → Wi-Fi
3. Enter network SSID and password
4. Click Save
5. Device will reboot and connect to your network

**Configuration via REST API:**
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi": {
      "ssid": "YourNetworkName",
      "password": "YourPassword"
    }
  }'
```

**Configuration File:**
Edit `firmware/main/config/default_config.json`:
```json
{
  "wifi": {
    "ssid": "YourNetwork",
    "password": "YourPassword",
    "static_ip": false,
    "ip": "",
    "gateway": "",
    "netmask": "255.255.255.0"
  }
}
```

### Static IP Configuration

**When to use:**
- Fixed IP requirements
- Integration with DMX controllers
- Firewall rules
- DNS mapping

**Configuration:**
```json
{
  "wifi": {
    "ssid": "YourNetwork",
    "password": "YourPassword",
    "static_ip": true,
    "ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "netmask": "255.255.255.0",
    "dns": "192.168.1.1"
  }
}
```

**Via REST API:**
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi": {
      "static_ip": true,
      "ip": "192.168.1.100",
      "gateway": "192.168.1.1",
      "netmask": "255.255.255.0"
    }
  }'
```

### Auto-Reconnect

**Behavior:**
- Automatically reconnects on disconnection
- Maximum retry attempts: 5
- Retry interval: Exponential backoff (1s, 2s, 4s, 8s, 16s)
- Falls back to AP mode after max retries

**Configuration:**
```json
{
  "wifi": {
    "auto_reconnect": true,
    "max_retries": 5,
    "retry_interval_ms": 1000
  }
}
```

### Finding Device IP Address

**Method 1: Serial Monitor**
```bash
idf.py monitor
# Look for: "Connected to WiFi. IP: 192.168.1.xyz"
```

**Method 2: Network Scanner**
```bash
# Linux/Mac
nmap -sn 192.168.1.0/24 | grep -B 2 "Espressif"

# Or use arp
arp -a | grep -i "espressif\|esp32"
```

**Method 3: Router DHCP Leases**
- Check your router's web interface
- Look for hostname "lumigrid-node-XX"
- Note the assigned IP address

**Method 4: mDNS (Bonjour)**
```bash
# If mDNS is enabled
ping lumigrid-node-01.local

# Or use avahi-browse (Linux)
avahi-browse -rt _http._tcp
```

---

## UDP Multicast Sync Protocol

The sync protocol enables multiple LED nodes to synchronize timing for coordinated light shows.

### Overview

- **Protocol:** UDP multicast
- **Multicast Address:** `239.10.7.42`
- **Port:** `45454`
- **Frequency:** 20 Hz (tick rate)
- **Roles:** Master (broadcasts) or Slave (receives)

### Packet Format

```c
struct sync_packet {
    uint32_t timestamp_ms;  // Milliseconds since boot
    uint8_t type;           // 1=TICK, 2=CUE
    uint8_t pad[3];         // Reserved/padding
} __attribute__((packed));
```

**Total packet size:** 8 bytes

### Packet Types

#### TICK Packet (Type 1)
- Sent by Master at 20 Hz
- Contains timestamp for synchronization
- Used for clock drift calculation

**Example:**
```json
{
  "type": "tick",
  "timestamp_ms": 123456
}
```

#### CUE Packet (Type 2)
- Sent by Master to trigger events
- Contains preset/effect information
- Slaves execute cue at specified time

**Example:**
```json
{
  "type": "cue",
  "timestamp_ms": 123800,
  "track": "ALEDch3",
  "preset": "ocean",
  "t0": 123800,
  "t1": 127000
}
```

### Master Mode Configuration

**Enable Master mode:**
```json
{
  "node_type": "led-node",
  "role": "Master",
  "sync": {
    "enabled": true,
    "mode": "master",
    "tick_rate_hz": 20,
    "multicast_addr": "239.10.7.42",
    "port": 45454
  }
}
```

**Via REST API:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "role": "Master",
    "sync": {"enabled": true}
  }'
```

**Behavior:**
- Broadcasts TICK packets at 20 Hz
- Sends CUE packets when effects change
- Provides timing reference for slaves

### Slave Mode Configuration

**Enable Slave mode:**
```json
{
  "node_type": "led-node",
  "role": "Slave",
  "sync": {
    "enabled": true,
    "mode": "slave",
    "multicast_addr": "239.10.7.42",
    "port": 45454
  }
}
```

**Via REST API:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "role": "Slave",
    "sync": {"enabled": true}
  }'
```

**Behavior:**
- Listens for TICK packets
- Calculates clock drift using EWMA
- Executes CUE packets at specified time
- Target: <5ms drift over 60 seconds

### Clock Discipline

**EWMA (Exponential Weighted Moving Average):**
```c
// Pseudo-code
float alpha = 0.1;  // Smoothing factor
drift_ms = alpha * (received_ts - local_ts) + (1 - alpha) * drift_ms;
```

**Drift Compensation:**
- Adjusts local timeline based on master
- Gradual correction to avoid jumps
- Target accuracy: ±5ms

### Network Requirements

**Multicast Support:**
- Router must support IGMP (Internet Group Management Protocol)
- Multicast packets must be allowed
- Some routers block multicast by default

**Bandwidth:**
- TICK packets: 20 Hz × 8 bytes = 160 bytes/sec
- CUE packets: Occasional, ~100 bytes each
- Total: <1 kbps per node

**Latency:**
- LAN: <5ms typical
- Wi-Fi: <20ms typical
- Internet: Not recommended (too much jitter)

### Testing Sync Protocol

**Test Master broadcasting:**
```bash
# Install tcpdump or wireshark
sudo tcpdump -i any -n 'host 239.10.7.42 and port 45454'
```

**Send manual CUE (for testing):**
```bash
curl -X POST http://192.168.1.100/api/cue \
  -H "Content-Type: application/json" \
  -d '{
    "track": "ALEDch1",
    "preset": "rainbow",
    "t0": 0,
    "t1": 5000
  }'
```

### Troubleshooting Sync

**Problem:** Slaves not receiving packets
- Check multicast is enabled on router
- Verify all nodes on same subnet
- Test with `ping 239.10.7.42` (some routers respond)
- Check firewall rules (allow UDP 45454)

**Problem:** Large drift (>50ms)
- Check network latency (ping master)
- Reduce Wi-Fi interference
- Use 5GHz band if available
- Check for packet loss

**Problem:** Sync stutters or jumps
- Reduce EWMA alpha (smoother but slower)
- Check for Wi-Fi power saving mode
- Verify stable power supply
- Monitor heap usage (may cause timing issues)

---

## MQTT Integration

MQTT enables cloud integration, monitoring, and remote control.

### Broker Configuration

**Configure MQTT broker:**
```json
{
  "mqtt": {
    "enabled": true,
    "host": "broker.hivemq.com",
    "port": 1883,
    "ssl": false,
    "user": "",
    "pass": "",
    "client_id": "lumigrid-node-01",
    "keepalive": 60
  }
}
```

**Via REST API:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "mqtt": {
      "enabled": true,
      "host": "broker.local",
      "port": 1883
    }
  }'
```

### SSL/TLS Configuration

**Enable secure connection:**
```json
{
  "mqtt": {
    "enabled": true,
    "host": "broker.hivemq.com",
    "port": 8883,
    "ssl": true,
    "user": "username",
    "pass": "password"
  }
}
```

**Certificate validation:**
- Uses system CA certificates
- For self-signed certificates, add to firmware build

### Topic Structure

#### Command Topics (Subscribe)
- `lumigrid/cmd/led-node/+` - Commands for all LED nodes
- `lumigrid/cmd/led-node/{node_id}` - Commands for specific node
- `lumigrid/cmd/led-node/{node_id}/set` - Set effect/config
- `lumigrid/cmd/led-node/{node_id}/get` - Request status

#### Telemetry Topics (Publish)
- `lumigrid/tele/led-node/{node_id}/status` - Status updates
- `lumigrid/tele/led-node/{node_id}/metrics` - Performance metrics
- `lumigrid/tele/led-node/{node_id}/error` - Error messages

### Message Format

**Status Message:**
```json
{
  "node_id": "lumigrid-node-01",
  "timestamp": 1678886400,
  "uptime_ms": 9234567,
  "heap_free_kb": 112,
  "fps": {
    "aled_ch1": 60,
    "aled_ch2": 60
  },
  "effect": "rainbow_flow"
}
```

**Command Message (Set Effect):**
```json
{
  "action": "set_effect",
  "channel": 1,
  "effect": "rainbow_flow",
  "speed": 1.0,
  "intensity": 0.8
}
```

**Command Message (Apply Preset):**
```json
{
  "action": "apply_preset",
  "preset_name": "ocean_wave",
  "channels": [1, 2, 3]
}
```

### Publishing Telemetry

**Frequency:**
- Status: 1 Hz (configurable)
- Metrics: 0.1 Hz (every 10 seconds)
- Errors: On occurrence

**Configuration:**
```json
{
  "mqtt": {
    "telemetry_interval_ms": 1000,
    "metrics_interval_ms": 10000
  }
}
```

### Subscribing to Commands

**Python Example:**
```python
import paho.mqtt.client as mqtt
import json

def on_message(client, userdata, message):
    payload = json.loads(message.payload)
    print(f"Command received: {payload}")
    
    if payload['action'] == 'set_effect':
        # Handle set_effect command
        print(f"Setting effect: {payload['effect']}")

client = mqtt.Client()
client.on_message = on_message
client.connect("broker.local", 1883)
client.subscribe("lumigrid/cmd/led-node/+")
client.loop_forever()
```

**Node.js Example:**
```javascript
const mqtt = require('mqtt');

const client = mqtt.connect('mqtt://broker.local:1883');

client.on('connect', () => {
  client.subscribe('lumigrid/cmd/led-node/+');
  console.log('Subscribed to command topics');
});

client.on('message', (topic, message) => {
  const payload = JSON.parse(message.toString());
  console.log('Command received:', payload);
  
  if (payload.action === 'set_effect') {
    console.log(`Setting effect: ${payload.effect}`);
  }
});
```

### Publishing Commands

**Publish command via MQTT:**
```bash
# Using mosquitto_pub
mosquitto_pub -h broker.local -t 'lumigrid/cmd/led-node/node-01/set' \
  -m '{"action":"set_effect","channel":1,"effect":"rainbow_flow"}'

# Request status
mosquitto_pub -h broker.local -t 'lumigrid/cmd/led-node/node-01/get' \
  -m '{"request":"status"}'
```

**Subscribe to telemetry:**
```bash
# Subscribe to all status updates
mosquitto_sub -h broker.local -t 'lumigrid/tele/led-node/+/status'

# Subscribe to specific node
mosquitto_sub -h broker.local -t 'lumigrid/tele/led-node/node-01/#'
```

### MQTT Brokers

**Popular MQTT Brokers:**
- **Mosquitto** - Open-source, self-hosted
- **HiveMQ** - Cloud and self-hosted
- **AWS IoT Core** - Fully managed, scalable
- **Azure IoT Hub** - Microsoft cloud
- **CloudMQTT** - Hosted service

**Local Mosquitto Setup:**
```bash
# Install Mosquitto (Ubuntu/Debian)
sudo apt-get install mosquitto mosquitto-clients

# Start broker
sudo systemctl start mosquitto
sudo systemctl enable mosquitto

# Test
mosquitto_pub -h localhost -t test -m "hello"
mosquitto_sub -h localhost -t test
```

---

## Network Security

### Best Practices

1. **Use strong Wi-Fi passwords**
   - Minimum 12 characters
   - Mix of letters, numbers, symbols
   - Change default passwords

2. **Enable WPA2/WPA3**
   - Avoid WEP (insecure)
   - WPA2-PSK minimum
   - WPA3 when available

3. **Isolate IoT network**
   - Separate VLAN for LED nodes
   - Firewall rules to limit access
   - No internet access if not needed

4. **MQTT Security**
   - Use TLS/SSL (port 8883)
   - Authentication with username/password
   - Consider client certificates

5. **Firmware Updates**
   - Verify OTA signatures
   - Use HTTPS for firmware downloads
   - Keep firmware up to date

### Firewall Rules

**Allow necessary ports:**
- **80/TCP** - HTTP (web UI, REST API)
- **1883/TCP** - MQTT (unsecured)
- **8883/TCP** - MQTTS (SSL/TLS)
- **45454/UDP** - Sync protocol (multicast)

**Example iptables rules:**
```bash
# Allow HTTP
iptables -A INPUT -p tcp --dport 80 -j ACCEPT

# Allow MQTT
iptables -A INPUT -p tcp --dport 1883 -j ACCEPT

# Allow sync protocol
iptables -A INPUT -p udp --dport 45454 -j ACCEPT

# Allow multicast
iptables -A INPUT -d 239.10.7.42 -j ACCEPT

# Drop all other traffic
iptables -A INPUT -j DROP
```

### VPN Access

**For remote access:**
- Set up WireGuard or OpenVPN
- Access devices through VPN tunnel
- No port forwarding on router

---

## Troubleshooting Network Issues

### Wi-Fi Connection Problems

**Can't connect to AP:**
- Verify SSID and password
- Check 2.4 GHz radio enabled on client
- Try forgetting network and reconnecting
- Check serial monitor for actual SSID

**Can't connect to STA:**
- Verify network credentials
- Check signal strength (≥-70 dBm)
- Ensure 2.4 GHz network (5 GHz not supported)
- Try restarting router
- Check MAC address filtering

**Frequent disconnects:**
- Check power supply (brownouts cause resets)
- Reduce distance to access point
- Change Wi-Fi channel (avoid interference)
- Disable power saving mode on router

### UDP Sync Issues

**Slaves not syncing:**
- Verify multicast enabled on router
- Check all nodes on same subnet
- Test with packet capture (Wireshark)
- Verify firewall allows UDP 45454

**Large drift:**
- Check network latency (ping master)
- Monitor packet loss
- Reduce Wi-Fi interference
- Consider wired Ethernet (requires adapter)

### MQTT Problems

**Can't connect to broker:**
- Verify broker IP/hostname
- Check port (1883 or 8883)
- Test with mosquitto_sub/pub
- Verify credentials
- Check SSL/TLS settings

**Messages not received:**
- Verify subscription topic pattern
- Check QoS level
- Monitor broker logs
- Verify network connectivity

### Network Diagnostics

**ESP32 network info:**
```bash
# Get via REST API
curl http://192.168.1.100/api/status | jq '.network'

# Or serial monitor
idf.py monitor
# Look for: "IP: ...", "Signal: ... dBm"
```

**Network scan from ESP32:**
- Future feature: Wi-Fi scanner in web UI
- Currently: Use router or laptop scanner

---

## Advanced Configuration

### Custom DNS

```json
{
  "wifi": {
    "dns_primary": "8.8.8.8",
    "dns_secondary": "8.8.4.4"
  }
}
```

### mDNS Hostname

```json
{
  "mdns": {
    "enabled": true,
    "hostname": "lumigrid-living-room"
  }
}
```

**Access via:**
```bash
curl http://lumigrid-living-room.local/api/status
```

### Network Time Protocol (NTP)

```json
{
  "ntp": {
    "enabled": true,
    "server": "pool.ntp.org",
    "timezone": "PST8PDT,M3.2.0,M11.1.0"
  }
}
```

### Bandwidth Optimization

**Reduce network traffic:**
```json
{
  "mqtt": {
    "telemetry_interval_ms": 5000,  // 5 seconds instead of 1
    "metrics_interval_ms": 30000    // 30 seconds instead of 10
  },
  "sse": {
    "update_interval_ms": 2000      // 2 seconds instead of 1
  }
}
```

---

## Integration Examples

### Home Assistant

```yaml
# configuration.yaml
mqtt:
  broker: localhost
  port: 1883

light:
  - platform: mqtt
    name: "LumiGrid Node 1"
    state_topic: "lumigrid/tele/led-node/node-01/status"
    command_topic: "lumigrid/cmd/led-node/node-01/set"
    brightness_state_topic: "lumigrid/tele/led-node/node-01/status"
    brightness_command_topic: "lumigrid/cmd/led-node/node-01/set"
    payload_on: '{"action":"set_effect","effect":"rainbow_flow"}'
    payload_off: '{"action":"blackout"}'
```

### Node-RED

```json
[
  {
    "id": "mqtt-in",
    "type": "mqtt in",
    "topic": "lumigrid/tele/+/status",
    "broker": "local-broker"
  },
  {
    "id": "parser",
    "type": "json",
    "property": "payload"
  },
  {
    "id": "dashboard",
    "type": "ui_gauge",
    "name": "Heap Free",
    "value": "{{payload.heap_free_kb}}"
  }
]
```

---

## Support and Resources

- **Network Documentation:** [ESP-IDF Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html)
- **MQTT Documentation:** [Paho MQTT](https://www.eclipse.org/paho/)
- **Sync Protocol Source:** [/firmware/components/sync_protocol](/firmware/components/sync_protocol)
- **MQTT Source:** [/firmware/components/mqtt_wrapper](/firmware/components/mqtt_wrapper)

---

**Last Updated:** 2026-01-08  
**Network Protocol Version:** 1.0  
**Compatible Firmware:** v1.0.0+
