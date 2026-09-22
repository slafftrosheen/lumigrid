# Configuration & Persistence

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

LumiGrid employs a robust persistence layer to store the node's settings, scheduling, and effect presets.

## Transactional Persistence

All configuration writes follow a strict transactional policy:
1. Prepare
2. Validate
3. Write Backup
4. Write Primary
5. Commit
6. Verify
7. Update Cached State
8. Publish State Event

Failure at any stage returns an error safely, guaranteeing consistency. The configuration layer manages checksums, backup slots, and version migration handling seamlessly.

## Safe Data Handling

Firmware is heavily validated to ensure memory bounds, preventing string-safety vulnerabilities (e.g. legacy name migrations strictly enforce null-termination bounds) and avoiding excessive heap allocations during serialization.
