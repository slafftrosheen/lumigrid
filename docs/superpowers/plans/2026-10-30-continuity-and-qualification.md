# Continuity and qualification implementation plan

**Goal:** Close testable clock, sync-readiness and FX-selection defects before adding more output behavior.
**Architecture:** Preserve current hardware pins, wire protocol and saved effect IDs. Harden the existing clock implementation; use pure dashboard policy functions for readiness and FX suitability; integrate those functions at existing call sites. Qualification tooling must distinguish missing evidence from passing evidence.
**Baseline:** `06eb19f21f7a7a0e163c863ee8699c243efad5c6`.
**Constraints:** No hardware attached. No repository write actions available. No full source checkout or npm/ESP-IDF download network in this environment. New tests compile actual retrieved production sources with explicit SDK/transport adapters; these are host tests, not board validation.

## Selected approach
An incremental continuity-first patch is preferable to expanding the effect catalogue before its output compatibility and synchronization readiness are dependable. No visual redesign or electrical configuration changes.

## Tasks
1. Reproduce clock arithmetic, epoch-domain and lock-age failures with real ClockDiscipline.cpp; fix checked arithmetic, time-zero samples, freshness and stale reacquisition without changing public signatures.
2. Add fail-closed sync health policy: protocol 6, fresh follower packets, correct leader identity, explicit transport application, same group, readiness explanations. Correct manifest compatibility/parity and dashboard integration. Preserve existing packet layout.
3. Fix effect metadata handling: unknown does not imply dimmer support; sanitize UI metadata; expose safe effect selection guidance without hiding or rewriting saved IDs.
4. Add read-only lab evidence preflight with source/binary hashes, explicit missing-evidence gates and regression tests; no CE certification or measured timing claims.
5. Package source-verified changes, exact-base integration patches, red/green logs and one-command local validation. Verify archive contains the actual payload. Do not claim remote publication.

## Review focus
- Conflicting or missing protocol fields must not unlock playback.
- Leader local-clock operation must work without an external epoch lock; follower lock must be fresh.
- Saved unavailable FX remain visible and unchanged; compatibility hints cannot invent capabilities.
- Missing measurement files, wrong binary identity, dirty/partial data must block a readiness pass.
- The prior archive contained no patch; this package must include code, tests and verifiable inventories.
