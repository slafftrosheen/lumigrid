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
- **HTTP (`PUT` / `DELETE` / `POST`):** Authoritative transport for state mutations and control leases, including acquiring, updating targets for, renewing (heartbeating), and releasing manual control (`/api/v1/manual`). HTTP provides synchronous acknowledgement, error status codes (`400`, `401`, `403`, `409`, `503`), and atomic contract negotiation (lease duration, verified acquired channel mask).
- **WebSocket (`ws://<ip>/ws`):** Strictly read-only event telemetry and state broadcast stream. Transmits periodic node telemetry (`status`, `health`, per-channel arbitration owners, and manual lease state).
- **Manual State Non-Modeling:** Manual control state must **never** be modeled as a command channel or mutated over WebSocket. WebSockets only reflect the authoritative runtime state currently resolved on the hardware node. Clients must never infer control lease ownership from WebSocket broadcast messages; leases are established exclusively through synchronous HTTP responses and reconciled against read-only telemetry.

## Sync Protocol (UDP port 6455)

The LumiGrid sync protocol ensures coordinated playback across multiple nodes using UDP broadcasts on port 6455.
- **Leader Election:** Nodes in `auto` role elect a leader using priority and MAC address tie-breakers. Explicit `master` and `worker` roles bypass election.
- **Clock Discipline:** Followers discipline local microsecond clocks against leader timestamps, maintaining bounded clock offset (< 5 ms) and tracking round-trip latency (RTT).
- **Scene Synchronization:** When Timeline sequence is inactive (`sequence_enabled == 0`), the leader broadcasts the active Scene slot number. Followers automatically activate their matching local preset in that slot.
- **Timeline Synchronization:** When Timeline sequence is active (`sequence_enabled == 1`), the leader broadcasts shared clock, BPM, and Timeline enabled state. Followers do **not** fight the scheduler with scene slot activations; instead, followers apply transient transport state to evaluate their local identical Timeline against the synchronized clock.
- **Show Parity vs. Clock Synchronization:** Clock synchronization (`clockLocked`) guarantees synchronized timing, but does **not** guarantee identical content. Full cluster `READY` state requires both clock lock and show manifest checksum parity (`showHash`).
- **Cue Lists Transport Exclusion:** Cue Lists are deployed as part of Show content, but live Cue transport is not synchronized over UDP in Sync v1. UI and operators must use synchronized Scene or Timeline transport for multi-node coordinated playback.

## DMX Input

The node accepts incoming DMX streams over Art-Net and sACN.
- DMX source expiry is tracked actively.
- Incoming DMX color data is injected across configured channels into a configurable layer (0-2).
