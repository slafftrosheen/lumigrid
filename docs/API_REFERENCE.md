# HTTP API reference

This inventory is generated from `LumigridLedNode/components/factory/FactoryApi.cpp`.
It lists registered methods, routes and handler guards, not guessed endpoints.
For request/response fields, read the named handler and its matching adapter in
`lumigrid-lander-main/src/lib/api/`. The original detailed reference is retained
as a [historical document](history/2026-10-21/API_REFERENCE.md), not the current contract.

## Transport and authorization

The node serves HTTP and WebSocket `/ws`. There is no registered SSE endpoint.
Normal protected routes require provisioning and an appropriate role. Browser
sessions use `Authorization: Bearer <sessionToken>`; automation uses `X-API-Key`.
No persistent credential belongs in a URL. See [Access](ACCESS.md).

Create Admin access once with POST `/api/v1/auth/bootstrap` and JSON
`{"adminToken":"<16–64 character private key>"}`. Login through POST
`/api/v1/auth/session` with `{"token":"<configured key>"}`. Use the returned
`sessionToken` for protected requests. POST `/api/v1/auth/ws-ticket` obtains a
single-use ticket for the WebSocket handshake. The browser implementation is
`src/lib/state/nodeStore.ts`; do not open a second competing connection.

## Common workflows

| Task | Contract / client owner |
| --- | --- |
| Read identity/capabilities/status | info, identity, capabilities, status; api/node.ts |
| Configure output topology | config GET, config/validate POST, config PUT; api/config.ts |
| Change networking | network/config GET/PUT and network/status GET; api/config.ts |
| Author/activate scenes | presets GET/PUT/DELETE and activate POST; api/scenes.ts |
| Live control/release | manual PUT/DELETE; api/manual.ts |
| Timeline/calendar | sequence and schedules GET/PUT; api/timeline.ts |
| Cue lists and show transport | cue-list, cue/control, show/control; api/cues.ts and showTransport.ts |
| Inspect output/memory/storage | diagnostics routes; api/diagnostics.ts |
| Update firmware | ota POST with raw application bytes; api/firmware.ts |
| Backup/restore | backup GET, restore/validate POST, restore POST; api/recovery.ts |
| Physical acceptance | factory/certification families; api/factory.ts, certification.ts |

Read current data before constructing edits. Preserve schema/version and
revision fields expected by the handler. Command receipts and jobs distinguish
acceptance from completion; observe status/snapshots and failures before calling
an operation applied. Preview frames and estimated power are not measurements.

Typical errors include 400 invalid payload, 401 authorization/session expiry,
403 provisioning/role rejection, 409 conflicting operation/state, 413 image size,
429 authentication throttling and 507 runtime memory admission. Exact JSON
`error` values and additional fields are handler-specific; clients must retain
the server response rather than turning every error into a connectivity message.

OTA is Admin-only, checks image/storage conditions and requires post-reboot
verification. `GET /api/v1/ota/status` includes `nodeId`, `targetBinding`,
`runningPartition`, `nextPartition`, `targetPartition` and `maxFirmwareBytes`.
When `targetBinding` is true, send `X-LumiGrid-Node-Id` with the selected node's
immutable identity; an invalid or mismatched value is rejected before image
reads or flash writes. Successful full receipts include `nodeId` and
`targetPartition` alongside the accepted version and command/job IDs.

Confirm an upload only when fresh identity and OTA status match its accepted
node, version and target partition, the running partition equals that target,
the state is `confirmed`, and no rollback is reported. An earlier same-version
confirmation, missing status or an incomplete receipt is not confirmation of
the new candidate. Older nodes without `targetBinding` use identity preflight
and readback without the new header; they do not provide server-side address
binding. Cancelling the browser operation stops further requests but does not
undo an already accepted update. See [Flash layout](FLASH_LAYOUT.md), including
the effective signed-OTA setting. Backup and restore are separate from credential
recovery; inspect validation/scope before applying a restore.

Hardware acceptance status advertises `evidenceBinding: "session_attempt_v1"`.
Bind start/test-start/abort to the observed `certificationId`; a new session
uses the previous status ID (empty before the first session). Result submissions
also send `testStartedMs` and `testAttempt` from that test's `startedMs` and
`attempts`. Mismatched bindings return 409 before mutation. Legacy requests
omitting all binding fields retain active-test guards but cannot reject every
delayed request for a later attempt. Automatic checks remain firmware-owned.

Only the active manual test waiting for evidence accepts an observation. Both
passed and failed instrument results need finite measurements and exact units;
zero is valid. Serial/note/unit limits are 31/95/7 UTF-8 bytes. Physical result
acceptance can leave `confirmationPending: true` until the owned runner stops
output; new work remains blocked and failed blackout becomes failed evidence.
`cancellationPending` similarly distinguishes accepted abort from completion.
Report timestamps are device uptime, and reporting-image metadata describes
the exporting image. See [Hardware acceptance](HARDWARE_CERTIFICATION.md).

## Registered routes

Role values below are extracted from direct `api::authorize` calls. Entries
marked handler-specific can use bespoke credential/ticket checks or be public;
the label is **not** a claim of anonymous access. The source handler remains
authoritative, including state-dependent guards.

| Method | Path | Direct role guard | Handler | Source line |
| --- | --- | --- | --- | --- |
| GET | `/api/v1/factory/status` | Installer | `get_status` | 8366 |
| POST | `/api/v1/factory/effect` | Installer | `post_effect` | 8367 |
| POST | `/api/v1/factory/lane` | Installer | `post_lane` | 8368 |
| POST | `/api/v1/factory/pwm` | Installer | `post_pwm` | 8369 |
| POST | `/api/v1/factory/abort` | Installer | `post_abort` | 8370 |
| POST | `/api/v1/factory/auto-loop` | Installer | `post_auto_loop` | 8371 |
| POST | `/api/v1/factory/test/start` | Installer | `post_factory_test_start` | 8372 |
| POST | `/api/v1/factory/test/confirm` | Installer | `post_factory_test_confirm` | 8373 |
| POST | `/api/v1/factory/test/abort` | Installer | `post_factory_test_abort` | 8374 |
| GET | `/api/v1/factory/test/report` | Operator | `get_factory_test_report` | 8375 |
| POST | `/api/v1/factory/burn-in/start` | Installer | `post_factory_burn_in_start` | 8376 |
| POST | `/api/v1/factory/burn-in/stop` | Installer | `post_factory_burn_in_stop` | 8377 |
| GET | `/api/v1/factory/boot-safe` | Installer | `get_boot_safe_verify` | 8378 |
| GET | `/ws` | Handler-specific; inspect source | `ws_handler` | 8383 |
| GET | `/` | Handler-specific; inspect source | `serve_dashboard` | 8385 |
| GET | `/index.html` | Handler-specific; inspect source | `serve_dashboard` | 8386 |
| GET | `/dashboard` | Handler-specific; inspect source | `serve_dashboard` | 8387 |
| GET | `/favicon.ico` | Handler-specific; inspect source | `get_favicon` | 8388 |
| OPTIONS | `/api/v1/*` | Handler-specific; inspect source | `options_handler` | 8389 |
| GET | `/api/v1/info` | Handler-specific; inspect source | `get_info` | 8392 |
| GET | `/api/v1/identity` | Operator | `get_identity` | 8393 |
| GET | `/api/v1/fleet/nodes` | Operator | `get_fleet_nodes` | 8394 |
| GET | `/api/v1/auth/status` | Handler-specific; inspect source | `get_auth_status` | 8397 |
| POST | `/api/v1/auth/bootstrap` | Handler-specific; inspect source | `post_auth_bootstrap` | 8398 |
| POST | `/api/v1/auth/session` | Handler-specific; inspect source | `post_auth_session` | 8399 |
| GET | `/api/v1/auth/session` | Operator | `get_auth_session` | 8400 |
| DELETE | `/api/v1/auth/session` | Operator | `delete_auth_session` | 8401 |
| POST | `/api/v1/auth/ws-ticket` | Operator | `post_auth_ws_ticket` | 8402 |
| GET | `/api/v1/auth/credentials` | Admin | `get_auth_credentials` | 8403 |
| POST | `/api/v1/auth/credentials` | Admin | `post_auth_credentials` | 8404 |
| DELETE | `/api/v1/auth/credentials` | Admin | `delete_auth_credentials` | 8405 |
| GET | `/api/v1/capabilities` | Operator | `get_capabilities` | 8408 |
| GET | `/api/v1/jobs/current` | Operator | `get_current_job` | 8409 |
| GET | `/api/v1/channels` | Operator | `get_channels` | 8410 |
| POST | `/api/v1/channel/test` | Operator | `post_channel_test` | 8411 |
| GET | `/api/v1/presets/references` | Operator | `get_preset_references` | 8412 |
| GET | `/api/v1/presets` | Operator | `get_presets` | 8413 |
| PUT | `/api/v1/presets` | Operator | `put_preset` | 8414 |
| DELETE | `/api/v1/presets` | Operator | `delete_preset` | 8415 |
| POST | `/api/v1/activate` | Operator | `post_activate` | 8416 |
| GET | `/api/v1/config` | Operator | `get_device_config` | 8417 |
| PUT | `/api/v1/config` | Admin | `put_device_config` | 8418 |
| POST | `/api/v1/config/validate` | Admin | `post_config_validate` | 8419 |
| GET | `/api/v1/network/config` | Operator | `get_network_config` | 8420 |
| PUT | `/api/v1/network/config` | Admin | `put_network_config` | 8421 |
| GET | `/api/v1/network/status` | Operator | `get_network_status` | 8422 |
| GET | `/api/v1/status` | Operator | `get_signage_status` | 8423 |
| GET | `/api/v1/dmx/frame` | Installer | `get_dmx_frame` | 8424 |
| PUT | `/api/v1/manual` | Operator | `put_manual` | 8425 |
| DELETE | `/api/v1/manual` | Operator | `delete_manual` | 8426 |
| GET | `/api/v1/effects` | Operator | `get_effects` | 8427 |
| GET | `/api/v1/palettes` | Operator | `get_palettes` | 8428 |
| GET | `/api/v1/health` | Installer | `get_health` | 8429 |
| GET | `/api/v1/diagnostics/output` | Installer | `get_diagnostics_output` | 8430 |
| GET | `/api/v1/diagnostics/storage` | Installer | `get_diagnostics_storage` | 8431 |
| GET | `/api/v1/diagnostics/memory` | Installer | `get_diagnostics_memory` | 8432 |
| GET | `/api/v1/logs` | Installer | `get_logs` | 8433 |
| GET | `/api/v1/sequence` | Operator | `get_sequence` | 8434 |
| PUT | `/api/v1/sequence` | Operator | `put_sequence` | 8435 |
| GET | `/api/v1/schedules` | Operator | `get_calendar_schedule` | 8436 |
| PUT | `/api/v1/schedules` | Operator | `put_calendar_schedule` | 8437 |
| GET | `/api/v1/scene-motion` | Operator | `get_scene_motion` | 8438 |
| PUT | `/api/v1/scene-motion` | Operator | `put_scene_motion` | 8439 |
| DELETE | `/api/v1/scene-motion` | Operator | `delete_scene_motion` | 8440 |
| GET | `/api/v1/cue-lists` | Operator | `get_cue_lists` | 8441 |
| GET | `/api/v1/cue-list` | Operator | `get_cue_list` | 8442 |
| PUT | `/api/v1/cue-list` | Operator | `put_cue_list` | 8443 |
| DELETE | `/api/v1/cue-list` | Operator | `delete_cue_list` | 8444 |
| POST | `/api/v1/cue/control` | Operator | `post_cue_control` | 8445 |
| GET | `/api/v1/cue/status` | Operator | `get_cue_status` | 8446 |
| GET | `/api/v1/show/status` | Operator | `get_show_status` | 8447 |
| POST | `/api/v1/show/control` | Operator | `post_show_control` | 8448 |
| GET | `/api/v1/sync` | Operator | `get_sync_status` | 8449 |
| GET | `/api/v1/show-manifest` | Operator | `get_show_manifest` | 8450 |
| GET | `/api/v1/audit` | Admin | `get_audit_log` | 8451 |
| POST | `/api/v1/ota` | Admin | `post_ota` | 8452 |
| GET | `/api/v1/ota/status` | Operator | `get_ota_status` | 8453 |
| GET | `/api/v1/recovery/status` | Operator | `get_recovery_status` | 8454 |
| GET | `/api/v1/backup` | Admin | `get_backup` | 8455 |
| POST | `/api/v1/restore/validate` | Admin | `post_restore_validate` | 8456 |
| POST | `/api/v1/restore` | Admin | `post_restore` | 8457 |
| GET | `/api/v1/certification/plan` | Operator | `get_certification_plan` | 8458 |
| GET | `/api/v1/certification/status` | Operator | `get_certification_status` | 8459 |
| POST | `/api/v1/certification/start` | Admin | `post_certification_start` | 8460 |
| POST | `/api/v1/certification/test/start` | Admin | `post_certification_test_start` | 8461 |
| POST | `/api/v1/certification/test/result` | Admin | `post_certification_test_result` | 8462 |
| POST | `/api/v1/certification/abort` | Admin | `post_certification_abort` | 8463 |
| GET | `/api/v1/certification/report` | Operator | `get_certification_report` | 8464 |
