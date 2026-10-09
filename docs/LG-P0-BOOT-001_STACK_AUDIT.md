# Runtime Hardening Stack Audit

Compiler stack usage source: `LumigridLedNode\.pio\build-lgp0-final\test\main\RuntimeHardeningTests.cpp.su` (`-fstack-usage`, env:test).

Large fixtures listed below are `LG_TEST_HEAP` RAII-owned allocations. Test buffers use checked heap allocation and are included in peak fixture pressure.

## Fixture sizes from the diagnostic ELF

| Object type | `sizeof` (bytes) |
|---|---:|
| `OutputController` | 2936 |
| `LightingRuntime` | 29768 |
| `DmxRouter` | 46760 |
| `RenderFrame` | 16416 |
| `LiveDmxFrame` | 17452 |
| `DeviceConfig` | 1676 |
| `DeviceConfigV1` | 1024 |
| `DeviceConfigV2` | 1108 |
| `DeviceConfigV3` | 1320 |
| `SceneConfig` | 804 |
| `SceneMotionConfig` | 152 |
| `Sequence` | 568 |
| `CueList` | 616 |
| `CalendarSchedule` | 68 |
| `LumigridRuntime` | 100344 |
| `RuntimeSnapshot` | 1440 |

## Test function source/build audit

| Test function | Local large fixture type(s) | Estimated function stack (bytes) | Allocation ownership |
|---|---|---:|---|
| `test_batch11_phase0_truthful_control_contract` | `OutputController` | 1872 | checked heap + RAII |
| `test_batch4_config_validation` | `DeviceConfig` | 368 | checked heap + RAII |
| `test_batch4_dmx_timeout_clearing` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 944 | checked heap + RAII |
| `test_batch4_explicit_output_fault_recovery` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch4_physical_safety_race_model` | `OutputController` | 64 | checked heap + RAII |
| `test_batch4_router_lock_boundary` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 432 | checked heap + RAII |
| `test_batch4_sequence_protocol_semantics` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 432 | checked heap + RAII |
| `test_batch4_source_metrics` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 448 | checked heap + RAII |
| `test_batch5_config_reload_transactional_safety` | `DeviceConfig`, `LightingRuntime`, `OutputController` | 2064 | checked heap + RAII |
| `test_batch5_dmx_span_validation` | (none) | 224 | small locals / compiler frame |
| `test_batch5_live_frame_clear` | `LightingRuntime`, `OutputController` | 400 | checked heap + RAII |
| `test_batch5_output_fault_latch` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch5_output_mapping_validation` | `DeviceConfig`, `OutputController`, `RenderFrame` | 352 | checked heap + RAII |
| `test_batch5_patch_update_invalidation` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 464 | checked heap + RAII |
| `test_batch5_submit_live_frame_return_value` | `LightingRuntime`, `LiveDmxFrame`, `OutputController` | 384 | checked heap + RAII |
| `test_batch5_universe_timeout_and_patch_clear` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 480 | checked heap + RAII |
| `test_batch6_config_migration` | (none) | 208 | small locals / compiler frame |
| `test_batch6_configured_fixture` | (none) | 192 | small locals / compiler frame |
| `test_batch6_enabled_bad_output` | (none) | 192 | small locals / compiler frame |
| `test_batch6_htp_payload_lengths` | `DmxRouter`, `LightingRuntime`, `OutputController` | 1472 | checked heap + RAII |
| `test_batch6_live_frame_render_snapshot` | `LightingRuntime`, `LiveDmxFrame`, `OutputController` | 384 | checked heap + RAII |
| `test_batch6_ltp_newest_expires` | `DmxRouter`, `LightingRuntime`, `OutputController` | 1472 | checked heap + RAII |
| `test_batch6_output_fault_state_machine` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch6_patch_update_invalidation` | `DmxRouter`, `LightingRuntime`, `OutputController` | 944 | checked heap + RAII |
| `test_batch6_universe_stats_expiry` | `DmxRouter`, `LightingRuntime`, `OutputController` | 944 | checked heap + RAII |
| `test_batch6_unused_fixture` | (none) | 192 | small locals / compiler frame |
| `test_batch6_unused_output_slots` | (none) | 192 | small locals / compiler frame |
| `test_batch7_output_fault_state_machine` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch7_router_to_runtime_integration` | `DmxRouter`, `LightingRuntime`, `OutputController` | 1472 | checked heap + RAII |
| `test_batch7_shutdown_restart` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch7_topology_validation` | (none) | 192 | small locals / compiler frame |
| `test_batch8_dmx_ltp_same_ms_tie_and_expiry` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 1472 | checked heap + RAII |
| `test_batch8_dmx_partial_expiration_both_directions_and_reuse` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 1008 | checked heap + RAII |
| `test_batch8_dmx_source_capacity_policy` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 944 | checked heap + RAII |
| `test_batch8_double_start_and_cooperative_shutdown` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_batch8_factory_runner_abort_vs_estop` | `OutputController` | 1088 | checked heap + RAII |
| `test_batch8_observational_health_and_lifecycle_rollback` | (none) | 208 | small locals / compiler frame |
| `test_batch8_output_transaction_and_fault_unification` | `DeviceConfig`, `LightingRuntime`, `OutputController` | 416 | checked heap + RAII |
| `test_batch8_protocol_consistency_and_artnet_serialization` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 768 | checked heap + RAII |
| `test_batch9_burnin_blockers` | `DeviceConfig`, `LightingRuntime`, `OutputController` | 912 | checked heap + RAII |
| `test_batch9_checked_scene_activation_lifecycle_guard` | `LightingRuntime`, `OutputController` | 384 | checked heap + RAII |
| `test_color_pipeline` | (none) | 224 | small locals / compiler frame |
| `test_config_migration` | `DeviceConfig` | 240 | checked heap + RAII |
| `test_dmx_sequence` | `DeviceConfig`, `DmxRouter`, `LightingRuntime`, `OutputController` | 960 | checked heap + RAII |
| `test_dmx_source_isolation` | (none) | 2000 | small locals / compiler frame |
| `test_estop_hard_latch` | `OutputController` | 64 | checked heap + RAII |
| `test_factory_vs_runtime_arbitration` | `OutputController` | 224 | checked heap + RAII |
| `test_fixture_scoped_ownership` | (none) | 224 | small locals / compiler frame |
| `test_lg022_show_engine` | `CalendarSchedule`, `CueList`, `LightingRuntime`, `OutputController`, `SceneConfig`, `SceneMotionConfig` | 1264 | checked heap + RAII |
| `test_lg024_live_desk_and_manual_override` | `LightingRuntime`, `OutputController` | 896 | checked heap + RAII |
| `test_lg025_fleet_sync_and_show_deployment` | `CalendarSchedule`, `CueList`, `CueSyncSnapshot`, `DeviceConfig`, `LightingRuntime`, `OutputController`, `SceneConfig`, `SceneMotionConfig`, `Sequence` | 2160 | checked heap + RAII |
| `test_lg026_show_transport_and_cue_sync` | (none) | 2784 | small locals / compiler frame |
| `test_lg027_network_settings_and_commissioning` | (none) | 48 | small locals / compiler frame |
| `test_lg028_network_commissioning_and_lifecycle` | `LumigridRuntime`, `OutputController` | 1248 | checked heap + RAII |
| `test_lg029_node_backed_authentication` | `DeviceConfig` | 2880 | checked heap + RAII |
| `test_lg030_fleet_discovery_and_identity` | (none) | 8048 | small locals / compiler frame |
| `test_lg031_runtime_snapshot_and_api_cutover` | `RuntimeSnapshot`, `char[8192] (http_buf)`, `char[8192] (ws_buf)` | 8368 | checked heap + RAII |
| `test_lg032_scene_studio_and_persistence` | `SceneConfig`, `SceneMotionConfig` | 224 | checked heap + RAII |
| `test_lg033_timeline_studio_v2` | `LightingRuntime`, `OutputController`, `Sequence` | 2224 | checked heap + RAII |
| `test_lg034_cue_studio_and_runtime_hardening` | `CueList`, `CueSyncSnapshot`, `LightingRuntime`, `OutputController`, `SceneConfig` | 2128 | checked heap + RAII |
| `test_lg035_diagnostics_and_factory_validation` | `OutputController` | 1504 | checked heap + RAII |
| `test_lg036_configuration_recovery_and_ota` | `DeviceConfig` | 896 | checked heap + RAII |
| `test_lg037_hardware_certification` | `DeviceConfig` | 7392 | checked heap + RAII |
| `test_lg0383_fresh_protocol_defaults` | `DeviceConfig` | 64 | checked heap + RAII |
| `test_lg0383_lazy_workers` | `LumigridRuntime`, `OutputController` | 1152 | checked heap + RAII |
| `test_lg0383_runtime_memory_helpers` | `RuntimeMemorySnapshot` | 432 | checked heap + RAII |
| `test_lg0383_service_plan` | `DeviceConfig` | 80 | checked heap + RAII |
| `test_lg0386_physical_output_integrity_and_safety` | `DeviceConfig`, `OutputController`, `RenderFrame` | 1056 | checked heap + RAII |
| `test_lg0387_bootstrap_and_credentials` | (none) | 3680 | small locals / compiler frame |
| `test_lg038_physical_output_engine` | `CertificationProbeSnapshot`, `DeviceConfig`, `OutputController` | 7328 | checked heap + RAII |
| `test_lghw001_hardware_coexistence_foundation` | `OutputController` | 1472 | checked heap + RAII |
| `test_lghw002_transactional_output_and_wifi` | `DeviceConfig`, `OutputController` | 1056 | checked heap + RAII |
| `test_lghw003_pca_recovery_and_rmt_integrity` | `DeviceConfig`, `OutputController` | 1536 | checked heap + RAII |
| `test_lghw004_boot_safety_power_and_forensics` | `DeviceConfig`, `RenderFrame` | 1488 | checked heap + RAII |
| `test_lghw005_power_architecture` | `DeviceConfig`, `DeviceConfigV1`, `DeviceConfigV2`, `DeviceConfigV3`, `OutputController`, `RenderFrame` | 560 | checked heap + RAII |

`-fstack-usage` reports each compiled function's static frame; it does not include callees, interrupt use, or runtime library stack use.
