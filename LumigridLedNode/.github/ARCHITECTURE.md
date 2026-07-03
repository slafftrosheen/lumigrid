# LumiGrid System Architecture

## Overview

LumiGrid is a distributed lighting control system for professional LED signage, consisting of:

1. **LED Node Firmware** — ESP32 firmware (this repo) driving LEDs and PWM fixtures
2. **Lander Web App** — SvelteKit marketing site + documentation (`lumigrid/lumigrid-lander-main/`)

## System Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                     LumiGrid Lander                          │
│  ┌─────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐   │
│  │ Landing │  │ API Docs │  │ Style    │  │ Content     │   │
│  │ Page    │  │ Reference│  │ System   │  │ Management  │   │
│  └─────────┘  └──────────┘  └──────────┘  └─────────────┘   │
│                                                              │
│  Tech: SvelteKit, Vite, Playwright tests                     │
│  Deploy: Vercel / static hosting                             │
└──────────────────────────┬───────────────────────────────────┘
                           │ HTTP API / MQTT / SSE
                           ▼
┌──────────────────────────────────────────────────────────────┐
│                   LumiGrid LED Node                          │
│  ┌──────────────────────────────────────────────────────┐    │
│  │                    WLED Core                         │    │
│  │                                                      │    │
│  │  WiFi AP/STA  │  Web Server  │  JSON API  │  MQTT   │    │
│  │  OTA Updates  │  Effects     │  DMX/E1.31 │  Segments│   │
│  └───────────────┴──────────────┴────────────┴──────────┘    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │              Lumigrid Usermods                       │    │
│  │                                                      │    │
│  │  PCA9685 Controller                                  │    │
│  │  ├── 16-channel PWM (expandable to 32)               │    │
│  │  ├── Per-channel: Off / Dumb PWM / CCT / RGB / RGBW │    │
│  │  ├── Emergency stop button                           │    │
│  │  └── DS18B20 temperature monitoring                  │    │
│  │                                                      │    │
│  │  Content Scheduler                                   │    │
│  │  ├── Time-based animation playback                   │    │
│  │  ├── JSON schedule (animations.json)                 │    │
│  │  └── Fallback presets                                │    │
│  │                                                      │    │
│  │  Telemetry                                           │    │
│  │  ├── Heartbeat to api.lumigrid.io                    │    │
│  │  └── Daily auto-reboot                               │    │
│  └──────────────────────────────────────────────────────┘    │
│                                                              │
│  Hardware: ESP32 + PCA9685 × (1-2) + DS18B20                │
└──────────────────────────────────────────────────────────────┘
```

## Firmware Layers

### Layer 1 — LED Control (WLED Core)

Unmodified WLED core providing:
- WiFi connectivity (AP + STA modes, Multi-WiFi)
- Web server with branded UI
- Effects engine (200+ effects, 50+ palettes)
- Segments and presets
- JSON API for remote control
- MQTT integration
- DMX/E1.31/ArtNet support
- OTA firmware updates

### Layer 2 — Hardware Bridge (PCA9685 Usermod)

Bridges WLED's color output to physical PWM fixtures:
- Maps WLED color/brightness to PCA9685 channels
- Supports multiple fixture types per channel (PWM, CCT, RGB, RGBW)
- Handles emergency stop via GPIO button
- Monitors temperature and throttles if overheating

### Layer 3 — Signage Features (Scheduler + Telemetry)

Professional signage capabilities:
- Time-based content scheduling for animated displays
- Health monitoring with heartbeat pings
- Automatic daily reboot for reliability
- Configurable via WLED's JSON settings API

## Communication Protocols

| Protocol | Direction | Purpose |
|---|---|---|
| HTTP (Web UI) | Browser → ESP32 | Configuration, live control |
| JSON API | Any → ESP32 | Programmatic control (`/json/state`) |
| MQTT | ESP32 ↔ Broker | Home Assistant, automation |
| DMX/E1.31 | Console → ESP32 | Professional lighting control |
| HTTP (Heartbeat) | ESP32 → api.lumigrid.io | Health monitoring |

## Filesystem Layout (ESP32)

```
/
├── config.json          # WLED persistent settings
├── macros.json          # Time macros and schedules
├── animations.json      # Content scheduler schedule
└── *.gif                # Animation files for Pixelforge
```

## Build System

### Web UI → C Headers

```
wled00/data/ (HTML/CSS/JS source)
    ↓  node tools/cdata.js
wled00/html_*.h, wled00/js_*.h (C header files with embedded data)
```

### Firmware Compilation

```
platformio.ini + platformio_override.ini
    ↓  pio run -e lumigrid
build_output/firmware/ (binary .bin files for flashing)
```

## Workspace Layout

```
lumigrid/                          # Monorepo root
├── README.md                      # This architecture doc
├── Makefile                       # Monorepo task runner
└── lumigrid-lander-main/          # SvelteKit web app
    ├── src/lib/components/        # UI components
    ├── src/routes/                # Pages
    └── docs/                      # API reference, integration guides

LumigridLedNode/                   # Firmware repo (separate)
├── wled00/                        # WLED core + branded web UI
├── usermods/lumigrid_*/           # Custom Lumigrid usermods
├── platformio.ini                 # Build configuration
└── platformio_override.ini        # Lumigrid-specific overrides
```
