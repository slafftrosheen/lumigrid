# LumiGrid LED Node Firmware

ESP32/ESP8266 firmware for professional LED signage, built as a fork of [WLED](https://github.com/wled/WLED).

## What This Is

A WLED fork customized for **Reclame Fabriek's** commercial signage hardware. Adds:

- **Lumigrid branding** — dark red glassmorphism theme throughout the web UI
- **PCA9685 PWM controller usermod** — 16-channel (expandable to 32) PWM output with per-channel mode selection, emergency button support, and temperature monitoring
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

### Target Board: ESP32 DevKit

| Pin | Function |
|---|---|
| GPIO 16,4,17,18,19,23,26,27 | LED data pins (8 strips) |
| GPIO 21 | I2C SDA |
| GPIO 22 | I2C SCL |
| GPIO 0 | Emergency button |

### Peripherals

- **PCA9685** × 1–2 — 16-channel PWM driver (I2C, addresses 0x40/0x41)
- **DS18B20** — temperature sensor (GPIO 4, OneWire)
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
pio run -e lumigrid
```

### Watch Mode (development)

```bash
npm run dev    # auto-rebuilds web UI on file changes in wled00/data/
```

### Upload to ESP32

```bash
# After building, upload via USB
pio run -t upload -e lumigrid

# Or OTA (if previously flashed with OTA enabled)
pio run -t upload -e lumigrid --upload-port <device-ip>
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

Drives 16–32 PWM channels via one or two PCA9685 I2C chips. Each channel supports:

- **Off** — disabled
- **Dumb PWM** — single-channel PWM output
- **CCT** — warm/cool white dimming from WLED color
- **RGB** — 3-channel RGB from WLED color
- **RGBW** — 4-channel RGB+white from WLED color

Features:
- Per-channel labels and calibration
- Emergency stop button (GPIO configurable)
- DS18B20 temperature monitoring with overheat protection
- Macro presets for scene recall
- I2C auto-detection of PCA9685 chips (addresses 0x40–0x4F)

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
[env:lumigrid]
extends = env:esp32dev
board = esp32dev
build_flags = ...
  -D WLED_MAX_BUSSES=12
  -D DATA_PINS="16,4,17,18,19,23,26,27"
  -D I2CSDAPIN=21
  -D I2CSCLPIN=22
  -D BTNPIN=0
  -D WIFI_TX_POWER=78
  -D WLED_ENABLE_DMX
  -D USERMOD_LUMIGRID_PCA9685
  -D USERMOD_LUMIGRID_CONTENT_SCHEDULER
  -D USERMOD_LUMIGRID_TELEMETRY
custom_usermods = ... lumigrid_pca9685 lumigrid_content_scheduler lumigrid_telemetry
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
