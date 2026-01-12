# AGENTS.md — LumiGrid

## Project Overview
LumiGrid: ESP32 (ESP-IDF v5.3+) firmware + SvelteKit web UI (LittleFS). Targets ESP32-WROOM-32 class SoCs. Build uses CMake+Ninja via `idf.py`.

## Repository Structure
```
lumigrid_led_node/
├── firmware/              # ESP-IDF project
│   ├── main/              # Application code
│   ├── components/        # Reusable components (rest_api, led_effects, etc.)
│   ├── assets/webapp/     # Web UI files (LittleFS)
│   └── schemas/           # JSON schemas
├── webapp/                # SvelteKit web UI source
│   └── src/
│       ├── lib/           # API wrappers, components
│       ├── routes/        # Pages (dashboard, effects, config, sequencer)
│       └── stores/        # Svelte stores
├── Docs/                  # Documentation
│   ├── API_REFERENCE.md   # REST API docs
│   ├── SETUP_GUIDE.md     # Setup instructions
│   └── api_spec.yaml      # OpenAPI specification
└── scripts/               # Build scripts
```

## Setup Commands (Linux)
```bash
# 1) System deps
sudo apt-get update && sudo apt-get install -y \
  git wget flex bison gperf cmake ninja-build ccache libffi-dev libssl-dev \
  python3 python3-venv python3-pip python3-setuptools \
  dfu-util libusb-1.0-0

# 2) Fetch ESP-IDF v5.3 and tools
git clone --depth=1 --branch v5.3 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
bash ~/esp/esp-idf/install.sh esp32
. ~/esp/esp-idf/export.sh

# 3) Node.js for webapp (optional)
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
```

## Build Commands
```bash
# Build webapp (SvelteKit)
cd webapp
npm install
npm run build

# Build firmware
cd firmware
idf.py set-target esp32
idf.py build
```

## Flash & Monitor
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Tests
```bash
# Component tests live under firmware/components/*/test and firmware/main/test
cd firmware
idf.py build
idf.py test
```

## Coding Style
- Treat `-Wformat`, `-Wint-conversion`, `-Wsizeof-array-div` as errors
- No single-byte buffers; always use `char buf[N];` where N > 1
- Use cJSON getters with type checks
- Follow the existing code patterns in each file

## API Alignment
The webapp (Svelte) uses `webapp/src/lib/api.js` which wraps all REST API calls:
- `/api/status` - System status
- `/api/config` - Configuration CRUD
- `/api/effects` - List effects
- `/api/effect/apply` - Apply effect
- `/api/presets` - Preset CRUD
- `/api/sequences` - Sequencer CRUD
- `/api/trigger` - One-shot actions
- `/api/ota/*` - OTA updates

Backend endpoints are in `firmware/components/rest_api/`.

## Tasks Good for Agents
- Implement missing REST endpoint functionality (match /Docs/api_spec.yaml)
- Add/extend unit tests (Unity) for FX & PWM groups
- Fix build warnings across components
- Improve webapp components to use API properly
- Add new effects to led_effects component
- Document new features in Docs/
