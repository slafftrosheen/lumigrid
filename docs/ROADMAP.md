# LumiGrid Roadmap

> **Status:** **PLANNED / CONCEPTUAL**

This document outlines the aspirational ecosystem vision for LumiGrid beyond the current LED Node.

> [!IMPORTANT]
> The features and node types listed below are strictly conceptual and do not represent the current `main` branch implementation. Do not design API or UI features against these concepts until they enter active development.

## Implemented Firmware & Operator Capabilities

- **Scene Morphing / Transition Engine v2:** Dual-buffer crossfading between scenes with independent color, brightness, effect, and layer interpolation (`SceneTransition`).
- **Effect Parameter Automation / Scene Motion:** Real-time parameter morphing and multi-track motion curves (`AutomationEngine`).
- **Cue Lists / Show Execution:** Structured playlist-style cue execution with automatic and trigger-based step advances (`CueRunner`).
- **Manual Cue Crossfade:** Operator manual fader crossfade between arbitrary cue scenes.
- **Live Desk v1 / Manual Override (LG-024):** Non-persistent live manual fader and RGB matrix override across 16 physical channels (A1–A8 and P1–P8) with lease protection, group targeting, scene capture, and safety pipeline compliance.

## Node Family (Conceptual)

- **Kinetic Node:** Motorized control, winches.
- **Sensor Node:** Environment reactive triggers.
- **Hub:** Dedicated coordination logic device.
- **Light Bar:** Integrated strip fixture.
- **Logic Node:** Headless automation triggers.
- **Display Node:** Status monitors.
- **Audio Node:** Sound-reactive translation.
- **DMX Bridge:** Dedicated DMX output translator.
- **Power Monitor:** System power telemetry.
