# LumiGrid Monorepo

LumiGrid is a distributed control architecture for modular, intelligent lighting and signage systems. This monorepo contains the web frontend and related tooling.

## Projects

| Project | Path | Description |
|---|---|---|
| **Lander** | [`lumigrid-lander-main/`](lumigrid-lander-main/) | SvelteKit landing page, documentation site, and design system |

### Planned Projects (not yet in repo)

The following are planned but not yet added to this repository:

- **Display Node** — ESP32 display controller firmware
- **Misc** — JSON schemas, example integrations (Node.js / Python)
- **Integration Tests** — cross-project test suite

> **LED Node Firmware** is maintained in a separate repository: [`LumigridLedNode`](../Documents/LumigridLedNode/)

## Quick Start

```bash
# Install dependencies
make install

# Run tests
make test

# Clean build artifacts
make clean
```

For project-specific commands, see the individual project READMEs.

## Architecture Overview

```
┌─────────────────────────────────────────────┐
│  LumiGrid Lander (SvelteKit)                │
│  - Marketing site / documentation           │
│  - Design system tokens                     │
│  - API reference docs                       │
│  - Embedded integration guides              │
└──────────────┬──────────────────────────────┘
               │ HTTP/MQTT/SSE
               ▼
┌─────────────────────────────────────────────┐
│  LumiGrid LED Node (ESP32 Firmware)         │
│  - WLED fork with Lumigrid branding         │
│  - PCA9685 PWM controller usermod           │
│  - Content scheduler for signage            │
│  - Telemetry & health monitoring            │
└─────────────────────────────────────────────┘
```

## Branding

LumiGrid is built by **Reclame Fabriek** for professional LED signage clients. The system targets commercial installations requiring:

- Day/night dimming schedules
- Emergency "all off" / panic controls
- Scene recall (brand colors, holiday themes)
- Content scheduling and remote management
- Status monitoring and health telemetry

## License

See individual project LICENSE files.
