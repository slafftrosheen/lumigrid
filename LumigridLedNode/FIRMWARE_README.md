# LumiGrid LED Node Firmware

ESP32/ESP8266 firmware for professional LED signage, built as a fork of [WLED](https://github.com/wled/WLED).

## What This Is

A WLED fork customized for **Reclame Fabriek's** commercial signage hardware. Adds:

- **Lumigrid branding** — dark red glassmorphism theme throughout the web UI
- **PCA9685 PWM controller usermod** — 8-channel PWM output (LED0–LED7, single PCA9685 at 0x40) with per-channel mode selection, emergency button support, and optional DS18B20 temperature monitoring
- **Content scheduler usermod** — time-based animation playback from JSON schedule files
- **Telemetry usermod** — heartbeat pings to a monitoring endpoint + configurable daily auto-reboot

## Architecture

```
┌─────────────────────────────────────────────┐
│  LumiGrid Lander (SvelteKit web app)        │
│  Marketing site, docs, content management   │
└──────────┬──────────────────────────────────┘
           │ HTTP API / MQTT / SSE
           ▼
┌─────────────────────────────────────────────┐
│  LumiGrid LED Node (this firmware)          │
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │  WLED Core                          │    │
│  │  - WiFi AP/STA, OTA, Web Server     │    │
│  │  - Effects engine, segments         │    │
│  │  - JSON API, MQTT, DMX              │    │
│  └─────────────────────────────────────┘    │
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │  Lumigrid Usermods                  │    │
│  │                                    │    │
│  │  PCA9685 Controller                │    │
│  │  - 16-channel PWM (2x PCA9685)     │    │
│  │  - Per-channel: Off/PWM/CCT/RGB    │    │
│  │  - Emergency stop button (GPIO0)   │    │
│  │  - DS18B20 temp monitoring         │    │
│  │                                    │    │
│  │  Content Scheduler                 │    │
│  │  - Time-based animation playback   │    │
│  │  - JSON schedule (animations.json) │    │
│  │  - Fallback presets               │    │
│  │                                    │    │
│  │  Telemetry                         │    │
│  │  - Heartbeat to api.lumigrid.io    │    │
│  │  - Daily auto-reboot              │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

## Hardware

### Target Board: ESP32-WROOM-32U

| Pin | Function | Notes |
|---|---|---|
| GPIO16 | ALEDch1 data | via SN74HCT245 A1 |
| GPIO4  | ALEDch2 data | via SN74HCT245 A2 |
| GPIO17 | ALEDch3 data | via SN74HCT245 A3 |
| GPIO18 | ALEDch4 data | via SN74HCT245 A4 |
| GPIO19 | ALEDch5 data | via SN74HCT245 A5 |
| GPIO23 | ALEDch6 data | via SN74HCT245 A6 |
| GPIO26 | ALEDch7 data | via SN74HCT245 A7 |
| GPIO27 | ALEDch8 data | via SN74HCT245 A8 |
| GPIO21 | I2C SDA | PCA9685 |
| GPIO22 | I2C SCL | PCA9685 |
| GPIO25 | PCA9685 OE# | active-low output enable |
| GPIO32 | Emergency button | active-low, internal pull-up |
| GPIO33 | DS18B20 (recommended) | free pin — do NOT use GPIO4/16–27 |

### Peripherals

- **SN74HCT245DWR** — 8-channel 3.3 V → 5 V level shifter for addressable LED data lines (DIR + OE# hardware-tied)
- **PCA9685** × 1 — 8-channel PWM driver (LED0–LED7, I2C address 0x40)
- **DS18B20** *(optional)* — temperature sensor on a free GPIO (recommended: GPIO33)
- Addressable LED strips: WS2812B, SK6812, etc.

## Build Instructions

### Prerequisites

- Node.js 20+ (`node --version`)
- Python 3.x with PlatformIO (`pip install -r requirements.txt`)

### Build Steps

```bash
# 1. Install Node.js dependencies (web UI build tools)
npm ci

# 2. Build web UI → generates wled00/html_*.h and wled00/js_*.h
npm run build

# 3. Build firmware for Lumigrid target
pio run -e lumigrid_node
```

### Watch Mode (development)

```bash
npm run dev    # auto-rebuilds web UI on file changes in wled00/data/
```

### Upload to ESP32

```bash
# After building, upload via USB
pio run -t upload -e lumigrid_node

# Or OTA (if previously flashed with OTA enabled)
pio run -t upload -e lumigrid_node --upload-port <device-ip>
```

## Project Structure

```
LumigridLedNode/
├── wled00/                          # WLED core firmware (C++)
│   ├── data/                        # Web UI source files (HTML/CSS/JS)
│   │   ├── index.htm                # Main control interface
│   │   ├── settings*.htm            # Settings pages
│   │   └── *.js/*.css               # Frontend resources
│   ├── html_*.h, js_*.h             # Auto-generated — DO NOT EDIT
│   └── *.cpp, *.h                   # Core firmware source
├── usermods/                        # Usermod addons
│   ├── lumigrid_pca9685/            # PCA9685 PWM controller (Lumigrid)
│   ├── lumigrid_content_scheduler/  # Time-based content scheduler (Lumigrid)
│   ├── lumigrid_telemetry/          # Health telemetry + auto-reboot (Lumigrid)
│   └── ...                          # Other community usermods from WLED
├── platformio.ini                   # Build configuration (all environments)
├── platformio_override.ini          # Local overrides — Lumigrid target defined here
├── tools/                           # Node.js build tools
│   ├── cdata.js                     # Web UI → C header packer
│   └── cdata-test.js                # Test suite for cdata.js
├── pio-scripts/                     # PlatformIO build scripts (Python)
├── docs/                            # Developer documentation
├── patch_index.py                   # Script to apply Lumigrid branding to index.htm
├── generate_settings_leds.py        # Standalone settings page generator
└── package.json                     # Node.js dependencies and scripts
```

## Custom Usermods

### PCA9685 Controller (`usermods/lumigrid_pca9685/`)

Drives 8 PWM channels (LED0–LED7) via a single PCA9685 at I2C address 0x40. Each channel supports:

- **Off** — disabled
- **Dumb PWM** — single-channel PWM (brightness = max of RGBW components)
- **CCT** — 2 channels: warm white + cool white
- **RGB** — 3 channels: R, G, B
- **RGBW** — 4 channels: R, G, B, W

Features:
- Per-channel label and calibration cap (0–100 %)
- Configurable PWM frequency (24–1526 Hz, default 1000 Hz)
- Emergency-off button (configurable GPIO, active-low, default GPIO32)
- DS18B20 temperature monitoring with thermal throttle + overheat shutdown (optional, pin configurable — must NOT conflict with addressable LED pins)
- I2C watchdog: auto-reinit if chip stops responding
- All settings fully configurable from the WLED Usermods settings UI

### Content Scheduler (`usermods/lumigrid_content_scheduler/`)

Plays animations based on a time schedule stored in `animations.json` on the filesystem:

```json
{
  "animations": [
    {
      "id": "morning_greeting",
      "file": "greeting.gif",
      "start_time": "06:00",
      "duration_minutes": 120,
      "fallback_preset": 1
    }
  ]
}
```

Features:
- Time-based scheduling (HH:MM format)
- Duration-limited playback blocks
- Fallback presets when no animation is scheduled
- Uses WLED's Pixelforge for GIF decoding

### Telemetry (`usermods/lumigrid_telemetry/`)

Health monitoring and reliability features:

- **Heartbeat** — HTTP GET to configurable endpoint every 5 minutes (includes MAC address and firmware version)
- **Daily auto-reboot** — configurable time (default 03:00), prevents memory fragmentation over long uptimes
- Configurable via WLED settings JSON

## Configuration

### Build Flags (`platformio_override.ini`)

```ini
[env:lumigrid_node]
extends = env:esp32dev
build_flags =
  ${common.build_flags} ${esp32_idf_V4.build_flags}
  -D WLED_RELEASE_NAME=\"LumiGrid_Node\"
  -D DATA_PINS=16,4,17,18,19,23,26,27   ; 8 addressable channels
  -D DEFAULT_LED_COUNT=30
  -D DEFAULT_LED_TYPE=22                 ; WS2812_RGB
  -D I2CSDAPIN=21
  -D I2CSCLPIN=22
  -D PCA9685_OE=25
  -D BTNPIN=32
  -D IRPIN=-1
  -D WIFI_TX_POWER=78
  -D WLED_ENABLE_DMX
lib_deps =
  ${esp32_idf_V4.lib_deps}
  adafruit/Adafruit PWM Servo Driver Library @ ^2.4.1
  paulstoffregen/OneWire @ ^2.3.8
  milesburton/DallasTemperature @ ^3.11.0
custom_usermods =
  lumigrid_pca9685
  lumigrid_telemetry
  lumigrid_content_scheduler
```

### Disabled Features (for signage use case)

- Alexa, Hue Sync, Adalight, Loxone, ESP-NOW — consumer features not needed for commercial signage

## Web UI Branding

The web UI uses a dark red glassmorphism theme. Branding is applied via:

1. **`patch_index.py`** — replaces favicon, app icon, logo, and text in `wled00/data/index.htm`
2. **Inline CSS** — custom styles injected into the settings pages
3. **`logos_b64.json`** — base64-encoded brand assets

## Upstream WLED

This is a fork of [WLED](https://github.com/wled-dev/WLED). The strategy is to:

- **Keep core changes minimal** — only branding and usermod additions
- **Avoid renaming internal classes** — makes upstream merges easier
- **Use feature flags** — `USERMOD_LUMIGRID_*` defines control custom code paths

### Syncing with Upstream

```bash
# Add upstream remote (one-time setup)
git remote add upstream https://github.com/wled-dev/WLED.git

# Pull and merge upstream changes
git fetch upstream main
git merge upstream/main --no-edit
```

## Related Projects

- **LumiGrid Lander** — SvelteKit web app: `../lumigrid/lumigrid-lander-main/`
- **WLED Documentation** — https://kno.wled.ge
- **WLED Discord** — https://discord.gg/QAh7wJHrRM

## License

EUPL v1.2 (inherited from WLED). See [LICENSE](LICENSE) for details.
