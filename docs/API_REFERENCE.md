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
verification. See [Flash layout](FLASH_LAYOUT.md), including the effective
signed-OTA setting. Backup and restore are separate from credential
recovery; inspect validation/scope before applying a restore.

## Registered routes

Role values below are extracted from direct `api::authorize` calls. Entries
marked handler-specific can use bespoke credential/ticket checks or be public;
the label is **not** a claim of anonymous access. The source handler remains
authoritative, including state-dependent guards.

| Method | Path | Direct role guard | Handler | Source line |
| --- | --- | --- | --- | --- |
| GET | `/api/v1/factory/status` | Installer | `get_status` | 8250 |
| POST | `/api/v1/factory/effect` | Installer | `post_effect` | 8251 |
| POST | `/api/v1/factory/lane` | Installer | `post_lane` | 8252 |
| POST | `/api/v1/factory/pwm` | Installer | `post_pwm` | 8253 |
| POST | `/api/v1/factory/abort` | Installer | `post_abort` | 8254 |
| POST | `/api/v1/factory/auto-loop` | Installer | `post_auto_loop` | 8255 |
| POST | `/api/v1/factory/test/start` | Installer | `post_factory_test_start` | 8256 |
| POST | `/api/v1/factory/test/confirm` | Installer | `post_factory_test_confirm` | 8257 |
| POST | `/api/v1/factory/test/abort` | Installer | `post_factory_test_abort` | 8258 |
| GET | `/api/v1/factory/test/report` | Operator | `get_factory_test_report` | 8259 |
| POST | `/api/v1/factory/burn-in/start` | Installer | `post_factory_burn_in_start` | 8260 |
| POST | `/api/v1/factory/burn-in/stop` | Installer | `post_factory_burn_in_stop` | 8261 |
| GET | `/api/v1/factory/boot-safe` | Installer | `get_boot_safe_verify` | 8262 |
| GET | `/ws` | Handler-specific; inspect source | `ws_handler` | 8267 |
| GET | `/` | Handler-specific; inspect source | `serve_dashboard` | 8269 |
| GET | `/index.html` | Handler-specific; inspect source | `serve_dashboard` | 8270 |
| GET | `/dashboard` | Handler-specific; inspect source | `serve_dashboard` | 8271 |
| GET | `/favicon.ico` | Handler-specific; inspect source | `get_favicon` | 8272 |
| OPTIONS | `/api/v1/*` | Handler-specific; inspect source | `options_handler` | 8273 |
| GET | `/api/v1/info` | Handler-specific; inspect source | `get_info` | 8276 |
| GET | `/api/v1/identity` | Operator | `get_identity` | 8277 |
| GET | `/api/v1/fleet/nodes` | Operator | `get_fleet_nodes` | 8278 |
| GET | `/api/v1/auth/status` | Handler-specific; inspect source | `get_auth_status` | 8281 |
| POST | `/api/v1/auth/bootstrap` | Handler-specific; inspect source | `post_auth_bootstrap` | 8282 |
| POST | `/api/v1/auth/session` | Handler-specific; inspect source | `post_auth_session` | 8283 |
| GET | `/api/v1/auth/session` | Operator | `get_auth_session` | 8284 |
| DELETE | `/api/v1/auth/session` | Operator | `delete_auth_session` | 8285 |
| POST | `/api/v1/auth/ws-ticket` | Operator | `post_auth_ws_ticket` | 8286 |
| GET | `/api/v1/auth/credentials` | Admin | `get_auth_credentials` | 8287 |
| POST | `/api/v1/auth/credentials` | Admin | `post_auth_credentials` | 8288 |
| DELETE | `/api/v1/auth/credentials` | Admin | `delete_auth_credentials` | 8289 |
| GET | `/api/v1/capabilities` | Operator | `get_capabilities` | 8292 |
| GET | `/api/v1/jobs/current` | Operator | `get_current_job` | 8293 |
| GET | `/api/v1/channels` | Operator | `get_channels` | 8294 |
| POST | `/api/v1/channel/test` | Operator | `post_channel_test` | 8295 |
| GET | `/api/v1/presets/references` | Operator | `get_preset_references` | 8296 |
| GET | `/api/v1/presets` | Operator | `get_presets` | 8297 |
| PUT | `/api/v1/presets` | Operator | `put_preset` | 8298 |
| DELETE | `/api/v1/presets` | Operator | `delete_preset` | 8299 |
| POST | `/api/v1/activate` | Operator | `post_activate` | 8300 |
| GET | `/api/v1/config` | Operator | `get_device_config` | 8301 |
| PUT | `/api/v1/config` | Admin | `put_device_config` | 8302 |
| POST | `/api/v1/config/validate` | Admin | `post_config_validate` | 8303 |
| GET | `/api/v1/network/config` | Operator | `get_network_config` | 8304 |
| PUT | `/api/v1/network/config` | Admin | `put_network_config` | 8305 |
| GET | `/api/v1/network/status` | Operator | `get_network_status` | 8306 |
| GET | `/api/v1/status` | Operator | `get_signage_status` | 8307 |
| GET | `/api/v1/dmx/frame` | Installer | `get_dmx_frame` | 8308 |
| PUT | `/api/v1/manual` | Operator | `put_manual` | 8309 |
| DELETE | `/api/v1/manual` | Operator | `delete_manual` | 8310 |
| GET | `/api/v1/effects` | Operator | `get_effects` | 8311 |
| GET | `/api/v1/palettes` | Operator | `get_palettes` | 8312 |
| GET | `/api/v1/health` | Installer | `get_health` | 8313 |
| GET | `/api/v1/diagnostics/output` | Installer | `get_diagnostics_output` | 8314 |
| GET | `/api/v1/diagnostics/storage` | Installer | `get_diagnostics_storage` | 8315 |
| GET | `/api/v1/diagnostics/memory` | Installer | `get_diagnostics_memory` | 8316 |
| GET | `/api/v1/logs` | Installer | `get_logs` | 8317 |
| GET | `/api/v1/sequence` | Operator | `get_sequence` | 8318 |
| PUT | `/api/v1/sequence` | Operator | `put_sequence` | 8319 |
| GET | `/api/v1/schedules` | Operator | `get_calendar_schedule` | 8320 |
| PUT | `/api/v1/schedules` | Operator | `put_calendar_schedule` | 8321 |
| GET | `/api/v1/scene-motion` | Operator | `get_scene_motion` | 8322 |
| PUT | `/api/v1/scene-motion` | Operator | `put_scene_motion` | 8323 |
| DELETE | `/api/v1/scene-motion` | Operator | `delete_scene_motion` | 8324 |
| GET | `/api/v1/cue-lists` | Operator | `get_cue_lists` | 8325 |
| GET | `/api/v1/cue-list` | Operator | `get_cue_list` | 8326 |
| PUT | `/api/v1/cue-list` | Operator | `put_cue_list` | 8327 |
| DELETE | `/api/v1/cue-list` | Operator | `delete_cue_list` | 8328 |
| POST | `/api/v1/cue/control` | Operator | `post_cue_control` | 8329 |
| GET | `/api/v1/cue/status` | Operator | `get_cue_status` | 8330 |
| GET | `/api/v1/show/status` | Operator | `get_show_status` | 8331 |
| POST | `/api/v1/show/control` | Operator | `post_show_control` | 8332 |
| GET | `/api/v1/sync` | Operator | `get_sync_status` | 8333 |
| GET | `/api/v1/show-manifest` | Operator | `get_show_manifest` | 8334 |
| GET | `/api/v1/audit` | Admin | `get_audit_log` | 8335 |
| POST | `/api/v1/ota` | Admin | `post_ota` | 8336 |
| GET | `/api/v1/ota/status` | Operator | `get_ota_status` | 8337 |
| GET | `/api/v1/recovery/status` | Operator | `get_recovery_status` | 8338 |
| GET | `/api/v1/backup` | Admin | `get_backup` | 8339 |
| POST | `/api/v1/restore/validate` | Admin | `post_restore_validate` | 8340 |
| POST | `/api/v1/restore` | Admin | `post_restore` | 8341 |
| GET | `/api/v1/certification/plan` | Operator | `get_certification_plan` | 8342 |
| GET | `/api/v1/certification/status` | Operator | `get_certification_status` | 8343 |
| POST | `/api/v1/certification/start` | Admin | `post_certification_start` | 8344 |
| POST | `/api/v1/certification/test/start` | Admin | `post_certification_test_start` | 8345 |
| POST | `/api/v1/certification/test/result` | Admin | `post_certification_test_result` | 8346 |
| POST | `/api/v1/certification/abort` | Admin | `post_certification_abort` | 8347 |
| GET | `/api/v1/certification/report` | Operator | `get_certification_report` | 8348 |
