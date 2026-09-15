# Protocols and Connectivity

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

LumiGrid supports multiple networking protocols for integration and synchronization.

## WebSocket API

The node serves a WebSocket endpoint at `ws://<ip>/ws`.
- **Event-Driven:** The WebSocket broadcasts factory status and state changes in real-time.
- **Constraints:** Max frame size `< 4 KB`.
- **Usage:** Clients should treat WebSocket events as a read-only stream for updating UI state (removing the need to continuously poll HTTP endpoints). State mutations must still occur via HTTP commands.

## Sync Protocol

The LumiGrid sync protocol ensures coordinated playback across multiple nodes using UDP broadcasts on port 6455.
- Nodes utilize a leader election mechanism (`auto` configuration) resolving ties using MAC addresses.
- Sync health exposes clock lock, epoch state, miss counts, jitter, and leader acknowledgement latency.

## DMX Input

The node accepts incoming DMX streams over Art-Net and sACN.
- DMX source expiry is tracked actively.
- Incoming DMX color data is injected across all 8 zones into a configurable layer (0-2).
