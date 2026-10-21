> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# Protocols and Connectivity

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

LumiGrid supports multiple networking protocols for integration and synchronization.

## WebSocket API

The node serves a WebSocket endpoint at `ws://<ip>/ws`.
- **Event-Driven:** The WebSocket broadcasts node telemetry, factory status, and state changes in real-time.
- **Constraints:** Max frame size `< 4 KB`.
- **Usage:** Clients should treat WebSocket events as a read-only stream for updating UI state (removing the need to continuously poll HTTP endpoints). State mutations must still occur via HTTP commands.

## Transport Responsibilities: HTTP vs WebSocket

LumiGrid strictly separates control mutation from state propagation:
- **HTTP (`PUT` / `DELETE` / `POST`):** Authoritative transport for state mutations and control leases, including acquiring, updating targets for, renewing (heartbeating), and releasing manual control (`/api/v1/manual`), activating scenes, and controlling Show Transport. HTTP provides synchronous acknowledgement, error status codes (`400`, `401`, `403`, `409`, `503`), and atomic contract negotiation (lease duration, verified acquired channel mask).
- **WebSocket (`ws://<ip>/ws`):** Strictly read-only event telemetry and state broadcast stream. Transmits periodic node telemetry (`status`, `health`, per-channel arbitration owners, show transport state, and manual lease state).
- **Manual State Non-Modeling:** Manual control state must **never** be modeled as a command channel or mutated over WebSocket. WebSockets only reflect the authoritative runtime state currently resolved on the hardware node. Clients must never infer control lease ownership from WebSocket broadcast messages; leases are established exclusively through synchronous HTTP responses and reconciled against read-only telemetry.

## Sync Protocol (UDP port 6455) — NodeSync Protocol v6

The LumiGrid sync protocol ensures frame-accurate, coordinated show playback across multiple nodes using UDP broadcasts on port 6455.
- **Leader Election:** Nodes in `auto` role elect a leader using priority and MAC address tie-breakers. Explicit `master` and `worker` roles bypass election. Cluster topology truth is evaluated by verifying exactly one active master node (`activeRole === 'master'`).
- **Packet Structure (76 Bytes Packed):**
  - Header: Magic (`0x4C475359`), `version = 6`, `type` (1=Heartbeat, 2=Ack), `sequence`, `flags`, `reserved`, `global_clock_ms`, `sent_monotonic_ms`, `sender_mac`, `token`.
  - Show Transport Payload:
    - `transport`: `0 = Standalone`, `1 = Scene`, `2 = Timeline`, `3 = Cue`.
    - Scene mode: `active_scene`.
    - Timeline mode: `bpm`, `transport_generation`, `timeline_anchor_ms`, `timeline_checksum`.
    - Cue mode: `cue_list`, `cue_index`, `cue_next`, `cue_state`, `cue_generation`, `cue_elapsed_ms`, `manual_fade_q15`, `cue_list_checksum`, `cue_transition_progress_q15`, `cue_paused_from`, `cue_source_scene`.
- **Clock Discipline:** Followers discipline local microsecond clocks against leader timestamps, maintaining bounded clock offset (< 5 ms) and tracking round-trip latency (RTT).
- **Scene Synchronization:** When transport mode is `Scene`, followers acquire `ControlSource::ScenePlayback` on mask `0xFFFF` and display the synchronized scene preset.
- **Timeline Synchronization (v5 Anchored Transport):**
  - When transport mode is `Timeline`, the leader broadcasts `transport_generation`, `bpm`, `timeline_anchor_ms`, and `timeline_checksum`.
  - Followers compare their local persisted Timeline checksum against `timeline_checksum`. If different, the follower **fails closed** (`transportError = "timeline_mismatch"`), refusing to execute divergent content.
  - When a new generation begins, followers snapshot their local sequence, apply the leader's anchor, and begin running from Beat 0.
  - Repeated sync packets with the same generation do **not** restart playback.
  - Late-joining followers calculate elapsed time as `current_clock_ms >= anchor_ms ? current_clock_ms - anchor_ms : 0` and immediately phase-lock to the running show.
- **Cue Synchronization (v6 Content Parity):**
  - When transport mode is `Cue`, the leader broadcasts its active Cue List checksum (`cue_list_checksum`) alongside the cue snapshot.
  - Followers compare the leader checksum against their local persisted list. Missing content fails with `cue_list_missing`; divergent content **fails closed** (`transportError = "cue_list_mismatch"`), refusing to execute divergent content.
  - Followers reconstruct the current visual phase (including paused fades) from the leader's transition progress and paused-from phase, so a follower that joins mid-fade — or mid-pause — locks to the exact leader visual without a snap.
- **Synchronized Cue Playback:** When transport mode is `Cue`, the leader is authoritative for cue sequencing, crossfade progression, hold durations, random cue choice, and manual crossfade progress. Followers apply incoming cue snapshots into their local `CueRunner` and render frame-locked crossfades without running independent timers.
- **Follower Suppression & Guard:** Followers enforce read-only playback (`409 sync_follower_read_only` on `/api/v1/activate`, `/api/v1/cue-control`, and `/api/v1/show/control`) and release competing local owners when remote transport is active.
- **Playback Parity vs. Schedule Parity:** Cluster synchronization readiness requires `playbackHash` match (Scenes, Motions, Timeline, Cue Lists). `scheduleHash` (calendar events) is evaluated independently and does not block group playback.
- **Version Fallback:** None — packets whose `version` differs from the local `kProtocolSync` are silently dropped at the receive gate, and mixed-size frames fail the length check. A cluster must therefore run a homogeneous protocol version; protocol divergence is detectable beforehand via the manifest `protocol` field and `GET /api/v1/capabilities`.

## DMX Input

The node accepts incoming DMX streams over Art-Net and sACN.
- DMX source expiry is tracked actively.
- Incoming DMX color data is injected across configured channels into a configurable layer (0-2).
