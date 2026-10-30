# Sync worker ownership and continuity — implementation and review record

**Goal:** Never replace a sync worker or tear down its callback dependencies before its completion is acknowledged.
**Baseline:** `06eb19f21f7a7a0e163c863ee8699c243efad5c6`.
**Approach:** Retain the existing cooperative worker/socket model, packet layout, output configuration and public product presentation. Preserve bounded stop calls and use a completion token as the join boundary. Reject overlapping lifecycle calls instead of blocking a callback behind itself. Keep current callback wiring; no new synchronization framework.

## Work completed in this source package

- [x] Reproduce timeout ownership loss, stale completion tokens, early worker exit, allocation failure, reentrant lifecycle operations and post-stop packet admission against the original production NodeSync source.
- [x] Retain worker ownership until an acknowledged stop; permit retry without admitting a replacement worker. Join on destruction rather than deleting instance resources beneath a live worker.
- [x] Keep socket close in the owning worker, require bounded send/receive waits, and reject oversized datagrams without changing protocol 6 or its 76-byte packet.
- [x] Make status collection observational, cache the master's provider snapshot, and report the actual transport generation.
- [x] Propagate stop errors through the legacy configuration callback. Preserve callback dependencies after rollback timeout. Stop sync before scheduler restart, including a timezone-only change with unchanged sync settings.
- [x] Exercise simultaneous start/stop, blocked callbacks, polling across 100 restart cycles and destruction with real host threads under UBSan and ThreadSanitizer.
- [x] Add a required on-board sync shutdown/recovery evidence record; retain NOT_RUN defaults.

## Verification boundaries and review findings

The full NodeSync.cpp and ClockDiscipline.cpp are compiled in the host suites. The runtime callback and rollback method are exact source excerpts compiled with adapter services; the restart-order excerpt is tested in a wrapper. Four full-file integration targets remain hash-gated recipes rather than reconstructed files. Their application to the entire repository and full C++/Svelte compilation remain mandatory.

The lifecycle lock only serializes start/stop. Callback setters remain a configure-before-start API, with readers quiesced. No claim is made that live callback replacement is supported. A stop already inside an application callback must wait for that callback; a timeout is not proof that it has stopped. Destruction can wait indefinitely for a permanently wedged callback rather than risk use-after-free. Normal callers should retry the bounded stop and retain the owner.

A successful start means task admission, not a socket-ready handshake. A socket setup failure exits the worker and remains owned until stop reaps it. Real FreeRTOS/lwIP scheduling, error recovery, stack usage, electrical outputs, timing and RF/EMC are not established by host adapters. Full UDP semantic/security validation remains outside this change.

No new effect IDs, pixel protocols, chip assignments, output counts or radio settings are introduced. This is not a firmware release candidate or compliance approval.
