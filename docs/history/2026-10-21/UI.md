> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

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
7. **Cues:** Cue Studio v2 (LG-034, timed playlist show builder over Scene presets):
   - **Editor & Library:** Schema-2 Cue List documents (name ≤ 31 UTF-8 bytes, ≤ 16 cues with per-cue label ≤ 23 bytes, hold/fade/curve) with free-slot library cards, duplication, JSON import/export, and a storyboard timeline with cue inspector (end action only for single-cue lists).
   - **Save vs. Run Separation:** "Save" persists the draft with optimistic checksum concurrency; "Save & Run" / "Save & Restart" persist then start/restart transport. Creating a list never auto-saves or auto-runs.
   - **Conflict & Stale Workflow:** `409 cue_list_conflict` surfaces a keep-draft/reload dialog; while a running show diverges from the saved list, a stale-content banner offers one-click "Run saved version" (restart).
   - **Live Transport:** Run/Stop/Pause/Resume/Prev/Next/Jump and the manual T-Bar (throttled to 10 Hz with pointer-drag local override and trailing flush) via `/api/v1/show/control`; a runtime monitor renders hold/fade phase, paused-from state, checksums, and stop-reason-aware state from `RuntimeSnapshot.transport.cue`.
   - **Dependency-Safe Deletion:** Deleting a Cue List referenced by the active transport or a schedule surfaces the exact references instead of failing silently. Follower nodes disable local transport controls while preserving editing.
8. **Schedules:** Day/time calendar automation.
9. **Fleet Devices & Network Commissioning (LG-028):** Multi-node device registry and integrated Network Commissioning Card (`NetworkConfigCard`):
   - **Connection State Pill:** Real-time visibility into `Connected`, `Connecting`, `Recovery AP`, or `Offline`.
   - **Mode Selection:** Toggle between `Access Point Only`, `Join Wi-Fi with Recovery AP` (`sta_with_ap_fallback`), and `Wi-Fi Only`.
   - **Credential Hygiene:** Stored Wi-Fi passwords are never returned to or pre-filled in the browser. Blank submissions preserve existing credentials; an explicit "Clear saved password" option clears credentials for open networks.
   - **Lifecycle & Reconnect Handling:** Triggering network changes presents a clear asynchronous apply state (`Applying network settings… Connection may move to the new network. Reconnecting…`) rather than a false "Node offline" alarm. Automatic polling verifies reconnection against authoritative runtime telemetry and surfaces recovery access point state if station credentials fail.
10. **Node-Backed Authentication & Access Control (LG-029):** Node-authoritative session gate and credential management:
   - **Node Authority:** The local browser PIN wall (`DEFAULT_OP_PIN`, `DEFAULT_AD_PIN`) and browser-manufactured credentials are eliminated. Dashboard access and role elevation require valid node-backed authentication.
   - **First-Boot / Factory Provisioning:** Connecting to an unprovisioned node surfaces the "Setup Admin Access" modal, allowing initial 16–64 character credential entry or cryptographic generation via `crypto.getRandomValues()`.
   - **Node Login & Session Lifecycle:** Provisioned nodes prompt for the configured node access key. On successful authentication, the node returns an ephemeral session token with server-dictated role and expiration.
   - **Session Storage:** Active sessions are tracked per-node address and ID in memory and `sessionStorage`. No access credentials or tokens are ever placed in `localStorage`.
   - **Admin Access Control Surface:** Dedicated credential management panel for Admin sessions displaying configured/disabled status for Administrator, Installer, and Operator roles, with one-time copy credential generation, confirmation, and session invalidation on rotation.
11. **Canonical Live State & Focused Node Store (LG-031):**
   - **Single Authoritative Live Store (`nodeStore`):** The focused node is backed by one authoritative `RuntimeSnapshotV3` store feeding all views. Overview, Scenes, Timeline, Cues, Factory, Diagnostics, and Network eliminate redundant polling and observe `nodeStore.snapshot`.
   - **Deterministic Initial Recovery & Stream Gap Healing:** Upon connecting or focusing a node, `GET /api/v1/status` immediately seeds initial snapshot truth before the authenticated WebSocket opens. Stream sequence gaps (`streamSeq N -> N+k`) transition connection status to `degraded`, fetch an HTTP snapshot recovery, and heal the state back to `online`.
   - **High-Rate Pixel Preview Isolation:** High-frequency binary pixel rendering (`nodePixelStore`, `nodeFrameCount`) is decoupled from snapshot updates to prevent full-dashboard rerendering at 10 Hz.
   - **Hardware-Truth Diagnostics:** Live indicators consume `nodeStore.snapshot` directly; heavy engineering diagnostics (NVS inspection, logs, security audit) are fetched on-demand.
   - **Identity vs. Local Alias Separation:** Fleet Devices separates device hardware name from user-defined browser-local aliases. Setting a local alias persists locally across mDNS refreshes.
12. **Scene Studio v2 (LG-032):**
   - **16-Channel Canvas:** Direct mapping of addressable strips A1–A8 and PWM dimmers P1–P8 with per-channel layer inspector and global brightness/transition controls.
   - **Visual Library & Thumbnails:** Dedicated `SceneThumbnail` component rendering color ribbons for pixel channels and intensity indicators for PWM outputs.
   - **Motion Curve Automation:** Multi-track curve editor (`AutomationTrackEditor`) supporting up to 4 tracks and 6 curve points per scene with human-readable value scaling.
   - **Optimistic Concurrency & Safe Persistence:** Strict CRC32 checksum verification guarding against concurrent edit overwrites (`409 Conflict`). Saving persists to NVS without mutating live output until explicitly activated.
   - **Dependency-Protected Deletion:** Preflight dependency checking surfacing exact timeline step and cue list references before deletion.
13. **Timeline Studio v2 (LG-033):**
   - **Visual Beat-Block Sequencer:** Variable-width blocks proportional to beat duration (1..64 beats) replacing rigid equal-width step tables.
   - **Permanent 16-Track Matrix:** Canonical physical track rows mapped permanently to A1–A8 and P1–P8 without ambiguous routing dropdowns.
   - **Separated Persistence & Transport:** "Save" persists the draft to hardware; "Run" snapshots the persisted sequence and begins anchored playback at Beat 0.
   - **Live Stale-Content Notification:** Clearly displays when saved changes differ from currently running playback snapshot, offering a single-click "Restart with Saved Version".
   - **Authoritative Live Playhead:** Live playhead derived directly from `RuntimeSnapshot.transport.timeline` with smooth client-side interpolation.
   - **Follower Mode Protection:** Active followers display "Playback Controlled by Sync Leader" with transport controls disabled, while permitting document editing and saving.
14. **Cue Transport on Embedded Dashboard (LG-034):** The device-hosted dashboard gains a Cue page (list selector, Run/Stop/Pause/Resume/Prev/Next/Restart, manual T-Bar when a manual-mode list runs) speaking the same canonical `/api/v1/show/control` transport, with live telemetry from the `transport.cue` status section and a fixed navigation rail exposing all operator pages.
15. **Factory Validation & Burn-In Console (LG-035):** Dedicated operator surface for hardware test sequences. Renders test suite selector (addressable lanes, PWM channels, visual color wash, pixel chase, full burn-in), live progress and elapsed time, abort controls, and formatted test reports.
16. **First-Install Commissioning Wizard (LG-036):** Guided 7-step onboarding flow after first Admin bootstrap (1. Node Identity, 2. Network Commissioning, 3. Output Channels, 4. Safety Limits, 5. Hardware Validation, 6. First Backup Download, 7. Ready).
17. **System & Recovery Hub (LG-036):**
    - **Safe Mode & Recovery Status:** Explicit banner when Safe Mode is active with root-cause diagnostics (invalid config, interrupted restore, critical fault).
    - **Device Configuration:** Structured panel organized by impact (Identity, Output & Safety, Protocols, Synchronization, Services) with checksum conflict detection (`409 Conflict`), preview dialog showing exact runtime consequences before save, and single source-of-truth limits.
    - **Two-Stage Restore:** Pre-flight upload validation (`POST /api/v1/restore/validate`) displaying source node provenance, domain plan, and preserved security settings before explicit user confirmation to restore and reboot.
    - **OTA Management:** Single canonical uploader (`POST /api/v1/ota`), exact candidate version verification, cross-reboot OTA status reporting (`confirmed` / `rolled_back`), and canary failure abort for fleet deployments.

18. **Hardware Certification Console (LG-037):**
    - **Physical Port Matrix:** Direct visualization of physical outputs A1–A8 (Addressable) and P1–P8 (PWM) with test status tiles (`NOT TESTED`, `TESTING`, `WAITING`, `PASS`, `FAIL`, `BLOCKED`, `STALE`).
    - **Profile Selector:** Toggle between `Development`, `ProductionUnit`, and `ReleaseCandidate` certification scopes with mandatory serial number enforcement on production runs.
    - **Evidence Submission Workflow:**
      - Operator observation verification for physical outputs with failure reason categorization (`no_output`, `wrong_port`, `wrong_color_order`, `partial_strip`, `flicker`, `crosstalk`, `unstable`, `other`) and mandatory note.
      - Instrument measurement entry for calibrated power limiting, PWM duty cycle, and optical skew with plan-owned tolerance evaluation.
    - **Staleness Banners:** Immediate visual indicator (`CERTIFICATION STALE`) if firmware, hardware revision, or configuration fingerprint drifts from certified state.
    - **Report Generation:** Printable view and machine-readable JSON export (`lumigrid-certification-<node-id>-<firmware>.json`) strictly excluding secrets.

### Service
- **Diagnostics:** Unified maintenance hub featuring live health telemetry, real-time node system logs, security/config audit trails, and hardware validation tests.
- **Certification:** Traceable physical hardware qualification and evidence collection (`/dashboard/certification`).
- **System / Firmware:** Device configuration, backup/restore, OTA update, and recovery status (`/dashboard/firmware`).


