# Design System & UI Architecture

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LumiGrid ecosystem UI (both the embedded web dashboard and the external SvelteKit lander) shares a single, portable component language.

## Shared Tokens

A centralized Apple-inspired design system (`design/tokens.json`) generates styling values for both platforms:
- **Lander:** Consumes generated TypeScript objects (`tokens.ts`).
- **Embedded Dashboard:** Consumes generated CSS variables (`variables.css`).

> **Philosophy:** Use glass backgrounds for visual hierarchy (background → section → interactive control), avoiding overuse of generic "glass boxes". The strongest visual identity stems from actual light simulations in the interface.

## Web UI Architecture

The operator interface is organized into primary destinations, visually separating operation from maintenance:

### Operate
1. **Overview:** System status, confidence layer, and quick actions.
2. **Sync (Sync Studio v2):** Multi-node cluster orchestration and Show Transport v2 coordination:
   - **Cluster Health & Topology Truth:** Real-time visibility into authoritative cluster leader (`activeRole === 'master'`), active follower topology, bounded clock discipline (offset, jitter, RTT), and topology health evaluation (`VALID_LEADER`, `NO_LEADER`, `SPLIT_BRAIN`, `LEADER_DISAGREEMENT`). Transport controls are automatically disabled on topology conflicts.
   - **Group Filtering:** Dedicated group filter by `groupLabel`. Readiness gate and parity calculations are scoped exclusively to nodes within the active group.
   - **Playback Parity vs. Schedule Parity:** Clear visual differentiation between real-time playback assets (`playbackHash`: scenes, motions, timeline, cues) and optional calendar schedules (`scheduleHash`). `playbackHash` matching is required for group synchronization readiness; schedule divergence does not block playback.
   - **Show Diff Inspector:** Manifest-based drilldown of differences across Scene presets, motions, timeline sequences, cue lists, and calendar schedules.
   - **Show Deployment Engine:** Admin workflow to push Show assets sequentially across targets with verification gating and exact mirror cleanup (including stale scene motion tracks).
   - **Synchronized Show Transport Controls:**
     - **Scene Transport:** Single-click preset activation on group via Show Transport coordinator.
     - **Timeline Transport:** Coordinated timeline playback start/stop with shared BPM and clock lock.
     - **Synchronized Cue Playback:** Complete cue list selection, live hold/fade telemetry, transport advance/pause/resume/stop/jump, and interactive manual crossfade (T-Bar) throttled to 10 Hz targeting leader only.
   - **Follower Protection Banners:** Follower nodes display prominent info banners on Scenes, Timeline, and Cues pages ("Playback Controlled by Sync Leader") with links to Sync Studio, disabling local playback/activate buttons while preserving local content editing.
3. **Live (Live Desk v1):** Real-time operator surface for transient, channel-scoped output control and live arbitration:
   - **Hardened Session Model (`ManualSession`):** All manual overrides operate through an explicit, server-authoritative state machine (`idle` → `acquiring` → `active` → `lost` / `releasing` / `error`). Heartbeats (5 s repeat, 15 s lease) only start after firmware confirms acquisition and matches the requested channel mask. Heartbeat failures or telemetry reporting an inactive lease immediately fail closed and transition the session to `lost`/`error`, clearing local control assumptions without auto-reacquiring.
   - **Selection vs. Controlled Scope Split:** UI selection (`selectedIndices`) is strictly decoupled from active hardware ownership (`controlledIndices`). Selecting or deselecting channels in the matrix while active never silently leaks or expands the hardware lease mask.
   - **Explicit Target Updates:** Modifying channel ownership while active requires an explicit "Update Targets" action, which sends a replacement snapshot and updates the active mask atomically. Updating with an empty target set cleanly releases the session to background scene playback.
   - **Telemetry-Authoritative Ownership UI:** Per-channel buttons display telemetry truth for output arbitration. When manual control is armed for a channel but preempted by higher priority (e.g., `LiveDmx`), the UI clearly distinguishes `Manual armed · DMX owns` rather than assuming exclusive output. Telemetry badges display active lease channels and countdowns directly from node status.
   - **Controls:** A1–A8 addressable controls (color wheel, RGB inputs, palette swatches, master brightness) and P1–P8 PWM faders (0–100%, preset step buttons), throttled at 10 Hz with immediate trailing flush, plus single-click scene capture into any available preset slot.
4. **Channels:** Physical output (A1–A8 addressable and P1–P8 PWM) status, live control, and group assignment with Admin-only configuration persistence.
5. **Scenes:** Scene Studio (motion, transitions, effect parameters). Follower nodes disable direct activation.
6. **Timeline:** 16-channel visual step sequencer. Follower nodes disable local playback.
7. **Cues:** Cue Studio (timed playlist show builder). Follower nodes disable local transport controls.
8. **Schedules:** Day/time calendar automation.
9. **Fleet Devices & Network Commissioning (LG-028):** Multi-node device registry and integrated Network Commissioning Card (`NetworkConfigCard`):
   - **Connection State Pill:** Real-time visibility into `Connected`, `Connecting`, `Recovery AP`, or `Offline`.
   - **Mode Selection:** Toggle between `Access Point Only`, `Join Wi-Fi with Recovery AP` (`sta_with_ap_fallback`), and `Wi-Fi Only`.
   - **Credential Hygiene:** Stored Wi-Fi passwords are never returned to or pre-filled in the browser. Blank submissions preserve existing credentials; an explicit "Clear saved password" option clears credentials for open networks.
   - **Lifecycle & Reconnect Handling:** Triggering network changes presents a clear asynchronous apply state (`Applying network settings… Connection may move to the new network. Reconnecting…`) rather than a false "Node offline" alarm. Automatic polling verifies reconnection against authoritative runtime telemetry and surfaces recovery access point state if station credentials fail.

### Service
- **Diagnostics & System:** Unified maintenance hub featuring live health telemetry, real-time node system logs, security/config audit trails, firmware OTA flashing, and backup/restore. Includes a dedicated section for Factory Output Testing (e.g., Burn-in, Lane Chase, Color Wash) time-bounded by the firmware.
- **Configuration:** Persistence and system settings.

