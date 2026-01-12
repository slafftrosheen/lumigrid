# LumiGrid LED Node - Quick Reference

**Version:** 1.0.0  
**Target:** ESP32-WROOM-32U  
**Last Updated:** 2026-01-08

---

## 🚀 Quick Links

| What do you want to do? | Go here |
|-------------------------|---------|
| **Set up the device for the first time** | [SETUP_GUIDE.md](SETUP_GUIDE.md) |
| **Use the REST API** | [API_REFERENCE.md](API_REFERENCE.md) |
| **Wire up hardware safely** | [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md) |
| **Configure Wi-Fi, MQTT, or sync** | [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md) |
| **Contribute or develop** | [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) |
| **Understand the architecture** | [ARCHITECTURE.md](ARCHITECTURE.md) |

---

## 📡 REST API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Node health, uptime, stats |
| `/api/config` | GET/POST | Configuration (JSON) |
| `/api/presets` | GET/POST | Preset CRUD |
| `/api/presets/{name}` | GET/DELETE | Specific preset |
| `/api/fx` | GET | List available effects |
| `/api/trigger` | POST | One-shot action |
| `/api/cue` | POST | Schedule cue |
| `/api/discover/needs_sensor_data` | GET | Node's need for sensor data |
| `/api/sensor_update` | POST | Receive sensor data |
| `/api/ota/upload` | POST | Upload firmware binary |
| `/api/ota/url` | POST | Download firmware from URL |
| `/events` | GET | SSE live stream |

**Full API Documentation:** [API_REFERENCE.md](API_REFERENCE.md)

---

## 🔌 GPIO Pinout

### Addressable LED Channels (WS2812B/SK6812)
- **ALEDch1-8:** GPIO 16, 4, 17, 18, 19, 23, 26, 27

### I²C Bus (PCA9685 PWM Controller)
- **SDA:** GPIO21
- **SCL:** GPIO22

### PWM Control
- **PCA9685 OE#:** GPIO25 (active-low)

### Serial Console
- **TX:** GPIO1
- **RX:** GPIO3

**Full Pinout and Wiring:** [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md)

---

## ⚡ Power Requirements

- **Voltage:** 5V DC ±5%
- **Current:** 10A minimum (50W)
- **Recommended PSU:** Mean Well LRS-50-5 or equivalent
- **Addressable LED limit:** 8A (configurable in firmware)
- **Controller allocation:** 0.5A

**Safety Guidelines:** [HARDWARE_GUIDE.md#safety-guidelines](HARDWARE_GUIDE.md#safety-guidelines)

---

## 🛠️ Build Commands

```bash
# Activate ESP-IDF environment
. ~/esp/esp-idf/export.sh

# Navigate to firmware directory
cd firmware

# Build
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Combined flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Exit monitor: Ctrl+]
```

**Complete Build Guide:** [SETUP_GUIDE.md](SETUP_GUIDE.md)

---

## 📶 Network Configuration

### Wi-Fi Access Point (Default)
- **SSID:** `LumiGrid-Setup`
- **Password:** `lumigrid-setup`
- **IP:** `192.168.4.1`

### Configure Station Mode (Connect to Your Network)

**Via REST API:**
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi": {
      "ssid": "YourNetwork",
      "password": "YourPassword"
    }
  }'
```

**Via Web UI:**
1. Connect to `LumiGrid-Setup`
2. Navigate to http://192.168.4.1
3. Go to Settings → Wi-Fi
4. Enter credentials and save

**Full Networking Guide:** [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)

---

## 🔄 UDP Sync Protocol

- **Multicast Address:** `239.10.7.42`
- **Port:** `45454`
- **Tick Rate:** 20 Hz
- **Roles:** Master (broadcasts) or Slave (receives)

**Configure as Master:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -d '{"role": "Master", "sync": {"enabled": true}}'
```

**Configure as Slave:**
```bash
curl -X POST http://192.168.1.100/api/config \
  -d '{"role": "Slave", "sync": {"enabled": true}}'
```

**Sync Protocol Details:** [NETWORKING_GUIDE.md#udp-multicast-sync-protocol](NETWORKING_GUIDE.md#udp-multicast-sync-protocol)

---

## 📨 MQTT Configuration

```bash
curl -X POST http://192.168.1.100/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "mqtt": {
      "enabled": true,
      "host": "broker.local",
      "port": 1883,
      "ssl": false
    }
  }'
```

### MQTT Topics

**Command (Subscribe):**
- `lumigrid/cmd/led-node/+` - Commands for all nodes
- `lumigrid/cmd/led-node/{node_id}` - Specific node

**Telemetry (Publish):**
- `lumigrid/tele/led-node/{node_id}/status` - Status updates
- `lumigrid/tele/led-node/{node_id}/metrics` - Performance metrics

**MQTT Guide:** [NETWORKING_GUIDE.md#mqtt-integration](NETWORKING_GUIDE.md#mqtt-integration)

---

## 🎨 Creating Presets

### Preset Schema
```json
{
  "preset_name": "ocean_wave",
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

### Create via API
```bash
curl -X POST http://192.168.1.100/api/presets \
  -H "Content-Type: application/json" \
  -d @preset.json
```

**Schema Reference:** [/firmware/schemas/preset-led.schema.json](/firmware/schemas/preset-led.schema.json)  
**Preset Examples:** [API_REFERENCE.md#preset-management](API_REFERENCE.md#preset-management)

---

## 🧪 Testing

### Pre-Commit Validation
```bash
./scripts/validate-code.sh
```

### Unit Tests
```bash
cd firmware
idf.py build test
```

### Manual API Testing
```bash
# Get status
curl http://192.168.4.1/api/status

# List effects
curl http://192.168.4.1/api/fx

# Monitor events
curl -N http://192.168.4.1/events
```

**Testing Guide:** [DEVELOPMENT_GUIDE.md#testing](DEVELOPMENT_GUIDE.md#testing)

---

## 🐛 Troubleshooting

### Build Errors

**"CMake Error: ... does not contain CMakeLists.txt"**
```bash
# You're in the wrong directory!
cd firmware
idf.py build
```

**"idf.py: command not found"**
```bash
# Activate ESP-IDF environment
. ~/esp/esp-idf/export.sh
```

### Flash Errors

**"Failed to connect"**
- Hold BOOT button while connecting
- Check USB cable (must be data cable, not charging-only)
- Verify port: `ls /dev/tty*`

**"Permission denied"**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Network Issues

**Can't connect to AP**
- Check SSID in serial monitor
- Verify 2.4GHz radio (ESP32 doesn't support 5GHz)
- Try forgetting network

**Can't access web UI**
- Verify IP address (192.168.4.1 in AP mode)
- Check firewall
- Try different browser

**More Troubleshooting:**
- [SETUP_GUIDE.md#troubleshooting](SETUP_GUIDE.md#troubleshooting)
- [HARDWARE_GUIDE.md#troubleshooting](HARDWARE_GUIDE.md#troubleshooting)
- [NETWORKING_GUIDE.md#troubleshooting-network-issues](NETWORKING_GUIDE.md#troubleshooting-network-issues)

---

## 📋 Configuration File Structure

**Location:** `firmware/main/config/default_config.json`

```json
{
  "node_type": "led-node",
  "node_name": "lumigrid-node-01",
  "role": "Slave",
  "control_mode": "Independent",
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
    "ssid": "",
    "password": ""
  },
  "mqtt": {
    "host": "",
    "port": 1883,
    "ssl": false
  }
}
```

**Full Configuration Reference:** [API_REFERENCE.md#configuration-endpoints](API_REFERENCE.md#configuration-endpoints)

---

## 🔒 Security Best Practices

1. **Wi-Fi Security**
   - Use WPA2/WPA3
   - Strong passwords (12+ characters)
   - Separate VLAN for IoT devices

2. **MQTT Security**
   - Enable TLS/SSL (port 8883)
   - Use authentication
   - Avoid public brokers for production

3. **Network Isolation**
   - Firewall rules to limit access
   - VPN for remote access
   - No direct internet exposure

**Security Guide:** [NETWORKING_GUIDE.md#network-security](NETWORKING_GUIDE.md#network-security)

---

## 📦 Built-in Effects

| ID | Name | Description |
|----|------|-------------|
| 1 | solid | Uniform color |
| 2 | gradient | Linear color interpolation |
| 3 | chase | Moving pixel with trail |
| 4 | twinkle | Randomized sparkle |
| 1105 | ocean_wave | Ocean wave simulation |
| 1104 | fire | Fire/flame effect |

**Get full list:**
```bash
curl http://192.168.4.1/api/fx
```

**Effect Development:** [DEVELOPMENT_GUIDE.md#adding-a-new-led-effect](DEVELOPMENT_GUIDE.md#adding-a-new-led-effect)

---

## 🔧 Common Tasks

### Change Node Name
```bash
curl -X POST http://192.168.4.1/api/config \
  -d '{"node_name": "living-room-leds"}'
```

### Set Brightness
```bash
curl -X POST http://192.168.4.1/api/config \
  -d '{"brightness": 0.75}'
```

### Trigger Flash
```bash
curl -X POST http://192.168.4.1/api/trigger \
  -d '{"action": "flash", "channel": 0, "duration_ms": 500}'
```

### Blackout (All Off)
```bash
curl -X POST http://192.168.4.1/api/trigger \
  -d '{"action": "blackout"}'
```

### Check Status
```bash
curl http://192.168.4.1/api/status | jq
```

---

## 📚 Documentation Index

### Getting Started
- [SETUP_GUIDE.md](SETUP_GUIDE.md) - Complete setup instructions
- [QUICK_START.md](QUICK_START.md) - Quick start for experienced users

### API & Integration
- [API_REFERENCE.md](API_REFERENCE.md) - Complete REST API documentation
- [API_EXAMPLES.md](API_EXAMPLES.md) - Additional code examples
- [api_spec.yaml](api_spec.yaml) - OpenAPI specification

### Hardware
- [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md) - Wiring, pinout, safety
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture

### Networking
- [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md) - Wi-Fi, MQTT, UDP sync

### Development
- [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) - Contributing guidelines
- [CONTRIBUTING.md](../CONTRIBUTING.md) - Contribution workflow

### Project Documentation
- [PROJECT_REPORT.md](PROJECT_REPORT.md) - Implementation report
- [PHASE2_3_COMPLETE.md](PHASE2_3_COMPLETE.md) - Latest features
- [OTA_GUIDE.md](OTA_GUIDE.md) - Over-the-air updates

---

## 🆘 Get Help

- **GitHub Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues
- **Documentation:** All guides in [/Docs](/Docs) directory
- **ESP-IDF Docs:** https://docs.espressif.com/projects/esp-idf/

---

## ⚡ Performance Targets

- Effect loop: ≤ 3 ms (8×180px @ 60fps)
- Free heap: ≥ 80 KB during playback
- REST response: < 50 ms
- Sync drift: < 5 ms over 60s

**Monitor performance:**
```bash
curl http://192.168.4.1/api/status | jq '{fps, heap_free_kb, uptime_ms}'
```

---

## 📄 License

TBD - See repository for license information

---

**Version:** 1.0.0  
**Last Updated:** 2026-01-08  
**Maintained by:** LumiGrid Development Team
