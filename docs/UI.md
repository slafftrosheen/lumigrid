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
2. **Scenes:** Scene Studio (Editor for zones, layers, effects).
3. **Timeline:** Visual step sequencer.
4. **Schedules:** Day/time calendar automation.
5. **Devices:** Sync and network configuration.

### Service
- **Diagnostics & System:** Unified maintenance hub featuring live health telemetry, real-time node system logs, security/config audit trails, firmware OTA flashing, and backup/restore. Includes a dedicated section for Factory Output Testing (e.g., Burn-in, Lane Chase, Color Wash) time-bounded by the firmware.
- **Configuration:** Persistence and system settings.
