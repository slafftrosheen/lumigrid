# LumiGrid Next Phase — Operator Platform v2

Baseline: `6afdf52876d31d793c1bab4780e311c69c8b0e89`.

LG-036 firmware core complete.
LG-036.1 frontend/system integration complete.

This phase is deliberately **integration-first**. The runtime/lifecycle architecture is frozen unless a concrete defect requires change. The objective is to make the existing firmware and dashboard behave like one coherent lighting-control product before adding another large feature family.

## Reference patterns

The direction follows proven patterns from mature/open-source lighting projects:

- WLED: keep the device API explicit and automation-friendly; expose presets/effects/segments without making the UI the source of truth.
- WLEDashboard: treat fleet state, groups/scenes, schedules, and live previews as first-class operator concepts.
- ASLS Studio: separate fixture/group state from scene composition, timeline sequencing, and visualisation.
- Aurora: use HTTP for commands and a push channel for live state propagation.

These references are inspiration for architecture and UX, not code dependencies.

## Phase order

### 19. Configuration, Recovery, Backup/Restore & OTA Hardening (LG-036 & LG-036.1 Complete)

**LG-036 firmware core: Complete.**

**LG-036.1 operator/system UI integration: Complete.**

Firmware endpoints delivered and fully integrated:

```text
GET  /api/v1/recovery/status
GET  /api/v1/config
POST /api/v1/config/validate
PUT  /api/v1/config
GET  /api/v1/backup
POST /api/v1/restore/validate
POST /api/v1/restore
POST /api/v1/ota
GET  /api/v1/ota/status
```

UI features delivered by LG-036.1:
- First-Install Commissioning Wizard (`CommissioningWizard`)
- Two-Stage Restore UI (`RestorePanel` with preflight validation and rollback alerts)
- System & Recovery Hub (`RecoveryStatusCard`, `DeviceConfigPanel`, `ConfigImpactDialog`, etc.)
- Config checksum, limits, write-only sync token management, and 409 conflict resolution
- Global persistent Recovery Mode banner across dashboard
- Canonical single OTA uploader (`OtaPanel` & `FleetOtaPanel`) with canary rollback protection
- Streaming `.lgbak` NDJSON backup & raw-bytes restore preflight

> **Note on firmware image signing:** OTA accepts an ESP-IDF application image. Secure Boot and flash image signing are NOT currently enabled. Do not describe the image as "signed" in documentation or UI. Use "ESP-IDF application image".

Backup endpoint returns `application/x-lumigrid-backup` (streaming NDJSON `.lgbak`). It is NOT JSON. Restore endpoints accept raw `.lgbak` bytes, not JSON.

## Explicit non-goals for this phase

- No new node families.
- No broad runtime rewrite.
- No cloud service.
- No proliferation of new effects before the existing scene/effect system is excellent.
- No second independent dashboard architecture.
- No Secure Boot or flash encryption in this batch.
- No CertificationService (LG-037) until LG-036.1 is merged.

## Definition of Done (Items 1–37 Satisfied)

All 37 verification items in the LG-036.1 specification are verified:
1. `GET /api/v1/recovery/status` consumed and displayed across operator dashboard.
2. Prominent recovery banner shown on any node in SafeMode or Recovery mode.
3. Safe-mode actions visible and clearly documented (physical reset documented, no software factory reset).
4. `GET /api/v1/config` typed, validating `schema`, `checksum`, `limits`, and `syncTokenSet`.
5. `POST /api/v1/config/validate` preflight dialog before saving.
6. `PUT /api/v1/config` sends `expectedChecksum`.
7. HTTP 409 conflict handled with `currentChecksum` and diff/reload prompt.
8. `pendingSyncToken` write-only handling, never echoed on GET.
9. `clearSyncToken` handling.
10. `GET /api/v1/backup` streamed and downloaded as `.lgbak` without JSON parse.
11. Backup preserves `.lgbak` NDJSON format without mutation.
12. Preflight restore via `POST /api/v1/restore/validate` with raw bytes.
13. Restore preflight displays package origin, plan, and preserved secrets/credentials.
14. Commit restore via `POST /api/v1/restore` with raw bytes and `application/x-lumigrid-backup`.
15. Post-restore operator guidance and reboot polling.
16. Consolidated canonical `uploadFirmware` hitting `POST /api/v1/ota`.
17. Legacy `/update` removed from active frontend paths.
18. OTA URL never derived with HTTPS from page origin.
19. `GET /api/v1/ota/status` polling with state machine support.
20. Cross-reboot OTA verification checking version and partition.
21. Rollback detection (`isOtaRolledBack`) alerting operator immediately.
22. Fleet OTA deployment using immutable node IDs.
23. Canary rollback halts fleet rollout immediately.
24. `CommissioningWizard` implemented with 7-step guided flow.
25. `CommissioningWizard` triggered on first-boot / unprovisioned state.
26. Decomposed components in `src/lib/components/system/`: `RecoveryStatusCard`, `DeviceConfigPanel`, `ConfigImpactDialog`, `BackupPanel`, `RestorePanel`, `OtaPanel`, `FleetOtaPanel`, `CommissioningWizard`.
27. Monolithic firmware page replaced with clean composed views.
28. Navigation updated to "System & Recovery".
29. Embedded dashboard updated to use canonical endpoints and `.lgbak`.
30. `build:embedded` passing cleanly with updated byte array headers.
31. `scripts/check_api_surface.py` passing with 0 route-level legacy violations.
32. `LumigridLedNode/scripts/check_invariants.py` passing with all invariants valid.
33. `LumigridLedNode/scripts/verify_runtime_math.py` passing all 21 runtime math tests.
34. Vitest unit test suite passing with 0 failures (35 test files, 347 tests).
35. `svelte-check` passing with 0 errors and 0 warnings.
36. Vite production build passing cleanly.
37. No LG-037 / `CertificationService` code introduced on `main`.
