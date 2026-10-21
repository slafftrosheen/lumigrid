# Architecture

`main/main.cpp` enters the application; `main/LumigridRuntime.cpp` coordinates services. `RuntimeServices.hpp` computes the service plan from safe mode, commissioning, network and device configuration. Observed state is reported separately from requested state.

| Component | Ownership |
| --- | --- |
| board | Pin map, early inhibit, reset forensics |
| config / storage | Schemas, validation, migration, NVS |
| auth / api | Credentials, sessions, authorization, audit |
| factory | HTTP routes, WS snapshots, memory diagnostics, test runner |
| signage | Source priority, scenes, effects, composition, color, power |
| output | Output controller/executor, RMT, PCA9685 |
| scheduler / show | Clock, schedules, timelines, cues, transport |
| protocols | Art-Net/sACN reception, DMX routing |
| network / discovery / sync | Wi-Fi, MQTT, mDNS, fleet presence, peer playback |
| ota | Storage contract, image preflight, boot verification |
| control / certification | Commands, jobs, bounded certification evidence |

Sources enter through manual commands, scene/schedule/cue playback, DMX or factory operations. Priority determines ownership. Rendering/composition produce frames; color processing and modeled power limits precede the output controller. Driver success is distinct from command acceptance.

Memory admission governs scratch/output allocation. Factory and network-apply workers are lazy. First-boot/recovery plans keep access services while disabling lighting/protocol work. Discovery and sync require connected station LAN access.

## Web ownership

The SvelteKit project contains public content, docs and the external operator dashboard. `src/lib/api/` owns adapters; `src/lib/state/nodeStore.ts` owns live WS/snapshots; `src/lib/auth/storage.ts` owns browser storage.

Storage consolidation is incomplete: `auth/nodeAuth.ts` and `fleet/registry.ts` also access browser storage directly. See the [source audit](SOURCE_OF_TRUTH.md) for this existing boundary-check failure.

`embedded/dashboard.html` is a separate device interface. `scripts/build-embedded.mjs` minifies/compresses it into `LumigridLedNode/generated/dashboard_assets.hpp`.

Root `docs/` is the current documentation source. `scripts/sync_documentation.py` extracts route registrations/limits and bundles guides for the site, avoiding deployment-working-directory file reads.
