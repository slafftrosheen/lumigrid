# LumiGrid LED Node Firmware

[![ESP-IDF CI](https://github.com/algorhythmicdev/lumigrid_led_node/actions/workflows/esp-idf-ci.yml/badge.svg)](https://github.com/algorhythmicdev/lumigrid_led_node/actions/workflows/esp-idf-ci.yml)

**Version:** 1.0.0  
**Platform:** ESP32-WROOM-32U  
**Framework:** ESP-IDF v5.3+

## Overview

LumiGrid is a professional-grade LED controller firmware for ESP32, supporting:
- 8× Addressable LED channels (WS2812B/SK6812 via RMT)
- 8× PWM channels (PCA9685 via I²C @ 1kHz)
- REST API + Modern Web UI (SvelteKit)
- MQTT telemetry & control
- Master/Slave synchronization via UDP multicast
- OTA firmware updates
- Time-based scheduling

## Quick Start

### Prerequisites
```bash
# Install ESP-IDF v5.3
# See: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

# Verify installation
idf.py --version

# Optional: Node.js for webapp development
node --version  # v20+
```

### Build & Flash
```bash
# Option 1: Use build script (builds webapp + firmware)
./BUILD.sh

# Option 2: Manual build
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure

```
lumigrid_led_node/
├── firmware/                # ESP-IDF project
│   ├── main/                # Application code
│   ├── components/          # Reusable components
│   │   ├── rest_api/        # HTTP endpoints
│   │   ├── led_effects/     # Effect engine
│   │   ├── pca9685_driver/  # PWM controller
│   │   └── ...
│   └── assets/webapp/       # Web UI files
├── webapp/                  # SvelteKit web UI source
│   └── src/
│       ├── lib/             # API wrappers
│       └── routes/          # Pages
├── Docs/                    # Documentation
└── scripts/                 # Build scripts
```

## Hardware Pinout

| Function | GPIO | Notes |
|----------|------|-------|
| ALED ch1-8 | 16,4,17,18,19,23,26,27 | RMT/WS2812B |
| I²C SDA/SCL | 21/22 | PCA9685 @ 400kHz |
| PCA9685 OE | 25 | Output enable (active-low) |
| UART | 1/3 | Console |

## REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Node health & telemetry |
| `/api/config` | GET/POST | Configuration |
| `/api/presets` | GET/POST/DELETE | Effect presets |
| `/api/effects` | GET | List available effects |
| `/api/effect/apply` | POST | Apply an effect |
| `/api/trigger` | POST | One-shot actions |
| `/api/ota/upload` | POST | OTA firmware update |
| `/events` | GET | SSE event stream |

See [API_REFERENCE.md](Docs/API_REFERENCE.md) for complete documentation.

## Effects

Built-in effects:
- **Pattern**: Scanner, Comet, Theater Chase, Running Lights, etc.
- **Noise**: Perlin Flow, Plasma, Lava Lamp, Clouds
- **Classic**: Rainbow Flow, Twinkle, Meteor, Fire2012

## Documentation

**📚 Interactive Documentation:** Access comprehensive documentation directly from the web interface at `http://<node-ip>/docs`

### Quick Links

| Guide | Description |
|-------|-------------|
| [INDEX.md](Docs/INDEX.md) | Complete documentation index |
| [QUICK_REFERENCE.md](Docs/QUICK_REFERENCE.md) | Quick reference & common tasks |
| [SETUP_GUIDE.md](Docs/SETUP_GUIDE.md) | Complete setup guide |
| [API_REFERENCE.md](Docs/API_REFERENCE.md) | REST API documentation |
| [HARDWARE_GUIDE.md](Docs/HARDWARE_GUIDE.md) | Hardware wiring & safety |
| [NETWORKING_GUIDE.md](Docs/NETWORKING_GUIDE.md) | Wi-Fi, MQTT, sync |
| [TROUBLESHOOTING.md](Docs/TROUBLESHOOTING.md) | Common issues & solutions |
| [DEVELOPMENT_GUIDE.md](Docs/DEVELOPMENT_GUIDE.md) | Contributing guidelines |
| [api_spec.yaml](Docs/api_spec.yaml) | OpenAPI specification |

## Development

### Add New Effect
1. Edit `firmware/components/led_effects/effects.c`
2. Add `fx_<name>_init()` and `fx_<name>_render()`
3. Register in `EFFECTS[]` array

### Testing
```bash
cd firmware
idf.py build
idf.py test
```

### Code Quality
- Compiler warnings treated as errors (`-Werror=format`, etc.)
- Use cJSON getters with type checks
- No single-byte buffers

## License

TBD

## Support

- **Repository:** https://github.com/algorhythmicdev/lumigrid_led_node
- **Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues

---

**Built with ❤️ for the LED art community**
