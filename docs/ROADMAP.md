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
- **Live Desk v1 / Manual Override (LG-024):** Non-persistent live manual fader and RGB matrix override across 16 physical channels (A1–A8 and P1–P8) with lease protection, group targeting, scene capture, and safety pipeline compliance. Hardened with server-authoritative `ManualSession` state machine, strict selection-versus-controlled scope decoupling, fail-closed telemetry & heartbeat reconciliation, verified channel isolation, replacement semantics, and runtime preemption test coverage (A–H).
- **Fleet Sync Studio & Show Deployment v1 (LG-025):** Multi-node cluster orchestration and show distribution. Provides real-time fleet visibility (leader election, follower topology, bounded microsecond clock offset, RTT latency), show manifest CRC32 checksum generation and parity comparison (`showHash`), real Scene slot deletion (`DELETE /api/v1/presets?index=N`), follower timeline transient transport synchronization, sequential multi-node show deployment engine with verification, and synchronized Scene and Timeline playback controls directed to the cluster leader.
- **Show Transport v2 & Synchronized Cue Playback (LG-026):** Unifies Scene, Timeline, and Cue List playback under a single explicit `ShowTransportCoordinator` state machine. Implements NodeSync protocol v4 (packed 60-byte UDP broadcast) with leader-authoritative random progression, hold/fade crossfades, manual crossfades, and monotonic generation counters. Active followers suppress local conflicting playback and enforce read-only HTTP guards (`409 sync_follower_read_only`). Schema 2 show manifests decouple `playbackHash` from optional `scheduleHash`.
- **Node-Backed Authentication, Sessions & Credential Provisioning (LG-029):** Replaces browser-trusted and universal-token authentication with node-authoritative authentication and authorization. Implements dedicated `AuthStore` in dual-slot NVS (`node_auth`), cryptographic salt + SHA-256 digest credential storage with constant-time comparison, first-boot bootstrap commissioning (`POST /api/v1/auth/bootstrap`, 16–64 chars), bounded RAM-only session table (256-bit entropy, 30 min idle / 8 hr absolute expiry), single-use WebSocket tickets (`Sec-WebSocket-Protocol: lumigrid-ticket.<ticket>`), per-node browser session state in memory and `sessionStorage`, backup v3 secret exclusion, credential rotation and disabling with session invalidation, and complete elimination of universal development backdoor credentials.
- **Fleet Discovery, Presence & Immutable Identity (LG-030):** Transforms fleet management from a manual IP list into automated self-discovering fleet operations. Enforces bootstrap trust boundary via runtime-owned `CommissioningState` (blocking bootstrap on ordinary LANs outside factory or physical recovery context); fixes `esp_err_t-as-boolean` truthiness bugs; eliminates hardcoded version constants via ESP-IDF app description metadata; stabilizes mDNS hostnames to permanent MAC-derived IDs (`lg-<mac>.local`); advertises dedicated `_lumigrid._tcp.local.` Zeroconf service with bounded TXT records; implements node-side `FleetDiscoveryService` maintaining bounded table of 32 nodes with automatic DHCP/IP address reconciliation under immutable IDs, self-exclusion, and presence lifecycle (`online`, `stale`, `offline`); and integrates operator dashboard with focused-node high-rate WebSocket control.
- **Canonical Runtime State & API v1 Cutover (LG-031):** Unifies firmware runtime telemetry into one single authoritative Schema 3 `RuntimeSnapshot` shared identically by `GET /api/v1/status` and authenticated WebSocket state streaming. Collection and serialization are purely observational and side-effect free; serialization uses `FixedJsonWriter` with an 8192-byte budget; status broadcasts coalesce at max 5 Hz via atomic CAS; static capabilities and raw DMX frames are decoupled from generic status; and `nodeStore` serves as the centralized live-state authority with automatic stream-gap recovery via HTTP.

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
