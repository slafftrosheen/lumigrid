# Design System & UI Architecture

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LumiGrid ecosystem UI (both the embedded web dashboard and the external SvelteKit lander) shares a single, portable component language.

## Shared Tokens

A centralized Apple-inspired design system (`design/tokens.json`) generates styling values for both platforms:
- **Lander:** Consumes generated TypeScript objects (`tokens.ts`).
- **Embedded Dashboard:** Consumes generated CSS variables (`variables.css`).

> **Philosophy:** Use glass backgrounds for visual hierarchy (background → section → interactive control), avoiding overuse of generic "glass boxes". The strongest visual identity stems from actual light simulations in the interface.

## Web UI Architecture

The operator interface is organized into five primary destinations, visually separating operation from maintenance:

### Operate
1. **Overview:** System status, confidence layer, and quick actions.
2. **Live:** Live Desk v1 operator surface (A1–A8 and P1–P8 live faders, RGB matrix, group targeting, 10 Hz throttle, 5s lease heartbeat, scene capture).
3. **Channels:** Physical output (A1–A8 addressable and P1–P8 PWM) status, live control, and group assignment with Admin-only configuration persistence.
4. **Scenes:** Scene Studio (motion, transitions, cue lists, effect parameters).
5. **Timeline:** 16-channel visual step sequencer.
6. **Schedules:** Day/time calendar automation.
7. **Devices:** Sync and network configuration.

### Service
- **Diagnostics & System:** Unified maintenance hub featuring live health telemetry, real-time node system logs, security/config audit trails, firmware OTA flashing, and backup/restore. Includes a dedicated section for Factory Output Testing (e.g., Burn-in, Lane Chase, Color Wash) time-bounded by the firmware.
- **Configuration:** Persistence and system settings.
