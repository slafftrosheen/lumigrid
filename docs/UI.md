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
2. **Sync (Sync Studio v1):** Multi-node cluster orchestration and show deployment:
   - **Cluster Health & Topology:** Real-time visibility into cluster leader, active follower topology, bounded clock discipline (offset, jitter, RTT), and content parity.
   - **Show Parity vs. Clock Synchronization:** Clear visual differentiation between locked clocks (`clockLocked`) and content parity (`MATCHED` vs `DRIFT`).
   - **Show Diff Inspector:** Manifest-based drilldown of differences across Scene presets, motions, timeline sequences, and cue lists without heavy full asset downloads.
   - **Show Deployment Engine:** Admin workflow to push Show assets sequentially across targets (max 2 concurrent nodes) with verification gating and exact mirror cleanup.
   - **Synchronized Operator Playback:** Single-click Scene and Timeline playback actions directed exclusively to the Leader node, using NodeSync for frame-locked cluster derivation.
   - **Cue List Disclaimer:** Clear messaging that Cue Lists can be mirrored across the cluster, but live Cue transport is not synchronized in Sync v1.
3. **Live (Live Desk v1):** Real-time operator surface for transient, channel-scoped output control and live arbitration:
   - **Hardened Session Model (`ManualSession`):** All manual overrides operate through an explicit, server-authoritative state machine (`idle` → `acquiring` → `active` → `lost` / `releasing` / `error`). Heartbeats (5 s repeat, 15 s lease) only start after firmware confirms acquisition and matches the requested channel mask. Heartbeat failures or telemetry reporting an inactive lease immediately fail closed and transition the session to `lost`/`error`, clearing local control assumptions without auto-reacquiring.
   - **Selection vs. Controlled Scope Split:** UI selection (`selectedIndices`) is strictly decoupled from active hardware ownership (`controlledIndices`). Selecting or deselecting channels in the matrix while active never silently leaks or expands the hardware lease mask.
   - **Explicit Target Updates:** Modifying channel ownership while active requires an explicit "Update Targets" action, which sends a replacement snapshot and updates the active mask atomically. Updating with an empty target set cleanly releases the session to background scene playback.
   - **Telemetry-Authoritative Ownership UI:** Per-channel buttons display telemetry truth for output arbitration. When manual control is armed for a channel but preempted by higher priority (e.g., `LiveDmx`), the UI clearly distinguishes `Manual armed · DMX owns` rather than assuming exclusive output. Telemetry badges display active lease channels and countdowns directly from node status.
   - **Controls:** A1–A8 addressable controls (color wheel, RGB inputs, palette swatches, master brightness) and P1–P8 PWM faders (0–100%, preset step buttons), throttled at 10 Hz with immediate trailing flush, plus single-click scene capture into any available preset slot.
4. **Channels:** Physical output (A1–A8 addressable and P1–P8 PWM) status, live control, and group assignment with Admin-only configuration persistence.
5. **Scenes:** Scene Studio (motion, transitions, cue lists, effect parameters).
6. **Timeline:** 16-channel visual step sequencer.
7. **Schedules:** Day/time calendar automation.
8. **Devices:** Sync and network configuration.

### Service
- **Diagnostics & System:** Unified maintenance hub featuring live health telemetry, real-time node system logs, security/config audit trails, firmware OTA flashing, and backup/restore. Includes a dedicated section for Factory Output Testing (e.g., Burn-in, Lane Chase, Color Wash) time-bounded by the firmware.
- **Configuration:** Persistence and system settings.
