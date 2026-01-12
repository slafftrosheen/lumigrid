# LumiGrid Node Family

Comprehensive technical reference for all LumiGrid node types, including hardware specifications, firmware architecture, and integration patterns.

## Overview

The LumiGrid ecosystem is built around a family of specialized nodes that work together to create distributed lighting and control systems. Each node type shares a common firmware foundation while providing specialized capabilities for different use cases.

## Common Architecture

All LumiGrid nodes share these core components:

### Software Foundation

1. **Sync Protocol** — UDP multicast for distributed timing
2. **Trigger Engine** — Event routing and action dispatch
3. **Effect Engine** — Real-time rendering pipeline
4. **Scheduler** — Time-based automation
5. **Web UI** — Browser-based control interface
6. **REST + MQTT APIs** — External control integration

### Hardware Base

- ESP32-based microcontrollers (ESP32, ESP32-S3, or compatible)
- LittleFS for configuration persistence
- I²C bus for sensor and driver communication
- UDP/TCP networking over Wi-Fi or Ethernet

---

## Node Types

### LED Node

**Purpose**: High-performance LED control for both PWM and addressable LED fixtures.

#### Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-WROOM-32U (4MB Flash, 520KB SRAM) |
| **PWM Driver** | PCA9685 (16-channel, 12-bit, 1 kHz) |
| **Level Shifter** | SN74HCT245DWR (8-channel, 3.3V → 5V) |
| **MOSFET Channels** | 8 × high-power (up to 5A per channel) |
| **Addressable Outputs** | 8 × WS2812/SK6812 compatible |
| **I²C Bus** | SDA: GPIO 21, SCL: GPIO 22 @ 400 kHz |
| **Output Enable** | GPIO 25 (active-low blackout) |
| **ALED GPIOs** | 16, 4, 17, 18, 19, 23, 26, 27 |
| **Power Input** | 5V DC, fused, reverse-polarity protected |
| **Expansion** | 8 additional PWM channels available |

#### Firmware Features

- **Dual Output Modes**: PWM and addressable LEDs simultaneously
- **Effect Library**: 15+ built-in effects (gradients, chase, fire, rainbow, etc.)
- **Power Budget**: Automatic brightness limiting based on thermal/current sensors
- **Soft-Start**: Prevents inrush current on power-up
- **Fade Engine**: Smooth transitions with configurable curves
- **Beat Sync**: Phase-lock to global clock with <5ms jitter

#### Effect Categories

| Category | Effects | Notes |
|----------|---------|-------|
| **Base** | Solid, Gradient, Chase, Twinkle | Foundational patterns |
| **Spectral** | Rainbow, Palette Cycle, Noise Flow | Color-based animations |
| **Organic** | Fire, Water Waves, Lava | Procedural, physics-inspired |
| **Utility** | Blackout, Flash, Strobe, Pulse | Cue-driven transitions |
| **PWM** | Breath, Candle Flicker, Warm-Dim | Analog dimming curves |

#### Configuration Example

```json
{
  "node_id": "led-node-001",
  "role": "participant",
  "pwm_channels": 8,
  "aled_channels": 8,
  "aled_pixels_per_channel": [60, 60, 30, 30, 144, 144, 0, 0],
  "power_limit_watts": 150,
  "thermal_limit_celsius": 75,
  "default_preset": "ambient-glow"
}
```

#### API Endpoints

```bash
# Get node status
GET /api/status

# Set PWM channel (0-7) to brightness (0-4095)
POST /api/trigger
{
  "action": "set_pwm",
  "channel": 0,
  "value": 2048
}

# Play preset on addressable LEDs
POST /api/trigger
{
  "action": "play_preset",
  "preset": "rainbow-chase"
}

# Emergency blackout
POST /api/trigger
{
  "action": "blackout"
}
```

---

### Kinetic Node

**Purpose**: Servo and motor control synchronized to the global clock.

#### Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-WROOM-32 |
| **Motor Driver** | DRV8833 dual H-bridge or PCA9685 servo expander |
| **Servo Outputs** | Up to 16 channels (12-bit position) |
| **DC Motors** | Up to 4 channels with speed/direction control |
| **Encoder Support** | Optional quadrature encoder inputs |
| **Power** | 5-12V DC depending on motor requirements |

#### Firmware Features

- **Motion Profiles**: Linear, ease-in/out, bezier curves
- **Position Sync**: Lock servo positions to global clock phase
- **Collision Detection**: Software-based limit detection
- **Homing Sequence**: Auto-calibration on startup
- **Smooth Acceleration**: Anti-jerk ramping

#### Typical Applications

- Kinetic signage elements (rotating logos, moving panels)
- Stage automation (curtains, props, lighting fixtures)
- Interactive sculptures
- Mechanical displays

#### Configuration Example

```json
{
  "node_id": "kinetic-001",
  "role": "participant",
  "servos": [
    {
      "channel": 0,
      "min_pulse_us": 500,
      "max_pulse_us": 2500,
      "center_position": 1500,
      "max_speed_deg_s": 180
    }
  ],
  "motion_profiles": {
    "swing": {
      "type": "sine_wave",
      "amplitude_deg": 45,
      "period_ms": 2000
    }
  }
}
```

---

### Sensor Node

**Purpose**: Environmental and user-interaction sensing with event publishing.

#### Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-WROOM-32 |
| **Sensor Hub** | I²C multiplexer (TCA9548A) for up to 64 sensors |
| **Common Sensors** | PIR motion, ultrasonic distance, light level, temperature |
| **Sample Rate** | 1-1000 Hz configurable |
| **Event Publishing** | MQTT or UDP multicast |

#### Firmware Features

- **Multi-Sensor Fusion**: Combine inputs for complex triggers
- **Threshold Detection**: Configurable high/low water marks
- **Debouncing**: Software filtering for mechanical inputs
- **Event Aggregation**: Reduce network traffic with smart batching

#### Sensor Types

| Type | Model Examples | Use Cases |
|------|---------------|-----------|
| **Motion** | HC-SR501 PIR | Occupancy detection, interactive triggers |
| **Distance** | HC-SR04 Ultrasonic, VL53L0X ToF | Proximity sensing, gesture control |
| **Light** | BH1750 Lux Meter | Ambient light compensation |
| **Temperature** | BME280, DS18B20 | Thermal monitoring, HVAC integration |
| **Gesture** | APDS-9960 | Swipe controls, presence |

#### Event Publishing Example

```json
{
  "node_id": "sensor-lobby",
  "timestamp": 1704723456789,
  "events": [
    {
      "sensor": "motion_pir_1",
      "type": "motion_detected",
      "value": true
    },
    {
      "sensor": "distance_us_1",
      "type": "range_cm",
      "value": 45.3
    }
  ]
}
```

---

### Hub Node / Master

**Purpose**: Global clock source, playlist server, and cloud bridge.

#### Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-S3 (8MB Flash, 2MB PSRAM) or Raspberry Pi 4 |
| **Network** | Wi-Fi + Ethernet for redundancy |
| **Storage** | microSD card for playlists and logs |
| **RTC** | Battery-backed real-time clock |
| **Display** | Optional OLED for status |

#### Firmware Features

- **Master Clock**: Sub-millisecond precision via NTP sync
- **Playlist Manager**: Schedule shows, transitions, seasonal content
- **Node Discovery**: Auto-detect and register participants
- **Cloud Bridge**: Optional MQTT uplink for remote monitoring
- **Failover**: Automatic master re-election if primary fails

#### Master Clock Protocol

The Hub broadcasts UDP multicast packets at 10 Hz:

```json
{
  "type": "tick",
  "master_id": "hub-001",
  "timestamp_ms": 1704723456789,
  "bpm": 120,
  "beat_phase": 0.25
}
```

#### Playlist Format

```json
{
  "playlist_id": "winter-2024",
  "scenes": [
    {
      "start_time": "18:00",
      "duration_s": 3600,
      "cue": "sunset-fade",
      "target_nodes": ["led-*"]
    },
    {
      "start_time": "19:00",
      "duration_s": 7200,
      "cue": "evening-ambient",
      "target_nodes": ["all"]
    }
  ]
}
```

---

### Light Bar Module

**Purpose**: Slim form-factor LED Node variant for architectural strips.

#### Hardware Specifications

- **Form Factor**: 200mm × 30mm × 15mm PCB
- **MCU**: ESP32-C3 (RISC-V, compact footprint)
- **Outputs**: 4 × addressable LED channels, 200+ pixels each
- **Power**: Screw terminals for 5V/12V LED strips
- **Mounting**: Magnetic or adhesive backing

#### Firmware Features

Same as LED Node, optimized for:
- Linear gradient mapping
- Segment independence
- Low-power idle mode

#### Typical Applications

- Under-cabinet lighting
- Shelf accent lighting
- Stair lighting with motion triggers
- Retail display edges

---

### Logic Node

**Purpose**: Headless compute module for choreography, AI inference, or data processing.

#### Hardware Specifications

| Component | Specification |
|-----------|--------------|
| **Platform** | Jetson Nano, Raspberry Pi 5, or x86 edge compute |
| **OS** | Linux (Ubuntu, Raspberry Pi OS) |
| **Language** | Python 3.11+, C++17, or Rust |
| **Connectivity** | Ethernet recommended for low latency |

#### Firmware Features

- **Custom Logic**: User-defined Python/C++ scripts
- **AI Inference**: TensorFlow Lite, ONNX Runtime
- **Data Aggregation**: Multi-sensor fusion, analytics
- **External APIs**: Integrate weather, calendar, IoT platforms

#### Example Use Cases

1. **Adaptive Lighting**: Adjust colors based on weather, time, or occupancy
2. **Beat Detection**: Real-time audio analysis for music-reactive lighting
3. **Gesture Recognition**: ML-based gesture control from camera input
4. **Anomaly Detection**: Alert on unusual sensor patterns

#### Python Example

```python
# logic_node.py - Weather-adaptive lighting controller

import requests
import paho.mqtt.client as mqtt

MQTT_BROKER = "hub-001.local"
WEATHER_API = "https://api.weather.com/..."

def get_weather():
    response = requests.get(WEATHER_API)
    return response.json()

def publish_cue(cue_name):
    client = mqtt.Client()
    client.connect(MQTT_BROKER, 1883)
    client.publish("lumigrid/cue", cue_name)
    client.disconnect()

if __name__ == "__main__":
    weather = get_weather()
    
    if weather['condition'] == 'rain':
        publish_cue('rainy-day-blues')
    elif weather['condition'] == 'sunny':
        publish_cue('bright-energetic')
    else:
        publish_cue('neutral-ambient')
```

---

## Common Configuration

### Network Setup

All nodes support:

1. **Access Point Mode**: Create temporary Wi-Fi for setup (SSID: `LumiGrid-<node_id>`)
2. **Station Mode**: Connect to existing Wi-Fi network
3. **Static IP**: Fixed addressing for production deployments
4. **mDNS**: Auto-discovery via `<node_id>.local`

### Firmware Update Methods

1. **USB Serial**: Esptool.py or Arduino IDE
2. **OTA (Over-The-Air)**: Web UI upload or MQTT trigger
3. **SD Card**: For hub/master nodes with storage

### Security

- **WPA2/WPA3**: Secure Wi-Fi encryption
- **HTTPS**: Optional for web UI (self-signed or Let's Encrypt)
- **Access Control**: IP whitelist, basic auth, or OAuth
- **Firmware Signing**: Prevent unauthorized updates

---

## Integration Patterns

### Standalone Installation

Single node acting as master + participant:

```
[LED Node] (Master + Participant)
  ↓ Controls
[8× PWM Channels] + [8× Addressable Outputs]
```

### Small Deployment (2-10 Nodes)

One master, multiple participants:

```
[Hub Node] (Master)
  ↓ Multicast (10 Hz sync)
[LED Node 1] [LED Node 2] [Sensor Node 1]
```

### Large Installation (10-100+ Nodes)

Master with redundancy, segmented networks:

```
[Hub Primary] ←→ [Hub Backup]
  ↓ VLAN 10
[LED Nodes × 20]
  ↓ VLAN 20
[Kinetic Nodes × 10]
  ↓ VLAN 30
[Sensor Nodes × 15]
  ↓ VLAN 40
[Logic Nodes × 3]
```

### Cloud-Connected Deployment

Remote monitoring and control:

```
[Cloud MQTT Broker]
  ↑ Uplink
[Hub Node]
  ↓ Local Multicast
[All Nodes]
```

---

## Cross-Platform Design Language

All nodes use the same design tokens (colors, spacing, effects) defined in this landing repository:

- **Colors**: `--mag`, `--cherry`, `--cya`, `--amb` (LED brand palette)
- **Glassmorphism**: Cards with `backdrop-filter: blur(20px)`
- **Halo Effects**: Glowing borders with `data-halo` levels 0-4
- **Typography**: SF Pro-inspired system fonts
- **Motion**: Respects `prefers-reduced-motion`

This ensures visual consistency from web UI to embedded node interfaces.

---

## Development Resources

### Firmware Repositories

- **LED Node**: `lumigrid-node-led` (ESP-IDF v5 + FreeRTOS)
- **Kinetic Node**: `lumigrid-node-kinetic`
- **Sensor Node**: `lumigrid-node-sensor`
- **Hub**: `lumigrid-hub` (ESP32-S3 or Pi Python)

### Toolchain

- **ESP-IDF**: v5.1+ for ESP32 nodes
- **PlatformIO**: Alternative build system
- **VS Code**: Recommended IDE with ESP-IDF extension
- **GitHub Actions**: CI/CD for firmware builds

### Testing

- **Unit Tests**: Unity test framework (ESP-IDF)
- **Integration Tests**: Pytest for Python nodes
- **Hardware-in-Loop**: Automated testing with real fixtures

---

## Roadmap

### Upcoming Node Types

| Node | Status | ETA |
|------|--------|-----|
| **Display Node** | Planning | Q2 2026 |
| **Audio Node** | Concept | Q3 2026 |
| **DMX Bridge** | Planning | Q4 2026 |
| **Power Monitor** | Concept | Q1 2027 |

### Display Node (Preview)

- **Purpose**: Drive LED matrices, HDMI outputs, e-ink displays
- **Hardware**: Raspberry Pi 5, HUB75 interface, HDMI output
- **Software**: Python + OpenGL for GPU-accelerated graphics
- **Use Cases**: Signage, status boards, art installations

---

## Support

- **Documentation**: [lumigrid.dev/docs](https://lumigrid.dev/docs)
- **Community Forum**: [forum.lumigrid.dev](https://forum.lumigrid.dev)
- **GitHub Issues**: Node-specific repositories
- **Email**: hello@lumigrid.dev

---

**LumiGrid Node Family** — Building blocks for intelligent light and motion systems.
