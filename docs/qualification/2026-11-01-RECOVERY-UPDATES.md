# Output recovery, cue admission and verified updates — 2026-11-01

Source baseline: `1af8c80af630cc2eeeb3975a0aea7e78f85332c5` on `main`. This is the third November 1 implementation batch, following [power accounting and editor continuity](2026-11-01-POWER-CONTINUITY.md).

## Output recovery and frame admission

If an addressable RMT frame fails, the controller no longer writes that rejected frame's new PWM duty while `/OE` may still be enabled from the previous frame. It enters the existing zero-output recovery path, preserving the original error. This prevents the specific rejected PWM command from being exposed; it does not make RMT and PWM physically atomic. Addressable data already transmitted before a later failure still depends on blackout recovery.

Before staging any buffers, the scheduler validates enabled PWM values, pixel count and color order, then holds the output transaction while comparing the rendered lane/channel topology with the current driver configuration. Invalid hardware and unavailable PWM remain output faults. A healthy configuration change returns the internal `kErrStaleOutputFrame` value (`0x6A03`); the runtime drops that frame without acknowledging its scene generation, then retries with fresh configuration on the next tick. A stale frame no longer silently truncates or misroutes output or permanently latches a hardware fault.

Move-assigned output transactions now transfer their owning controller pointer and mutex responsibility together. Completed and moved-from transactions cannot commit again. Five new regressions fail against the previous source baseline and pass with the changes.

## Cue timing and content admission

Pause uses the caller's absolute clock value directly. Resume rebases the phase anchor while preserving the original phase duration and cumulative elapsed time. This removes uptime from the elapsed calculation and keeps repeated hold/fade pauses consistent for synchronized followers.

Remote Cue admission validates the actual list that will run, its checksum, cue indices and required scenes before changing ownership or the active immutable content. A new checksum in the same slot loads the matching saved edit; an already matching active copy remains usable even when that saved slot was subsequently edited. Content changes trigger activation even if incoming generation/index values are unchanged. Inactive cleanup still releases remote Cue mode when saved content is absent.

Remote Scene and local Scene/Cue/Timeline admission now validates content before preempting the current show. Rejected requests preserve prior playback and transport generation. Runtime diagnostics retain the existing failure meanings through a small internal admission detail; NodeSync wire version and packet layout are unchanged.

The harness compiles the full production CueRunner/ShowTransport and the exact current remote callback and local HTTP handlers with real cJSON. Persistence, rendering and Scheduler remain recording host boundaries. This is an admission guarantee; a later runtime failure during a multi-step visual transition, or content changing between validation and activation, is not a new cross-component atomic transaction.

## Firmware update continuity

The upload endpoint optionally binds the request to `X-LumiGrid-Node-Id`, rejecting malformed or mismatched identity before reading the application image or writing flash. Full accepted receipts include immutable node ID and target partition. Status advertises `targetBinding: true`, its identity, running/next/target partitions and capacity. No stored OTA schema or signature-provisioning settings change.

The dashboard accepts only a complete positive receipt. Non-JSON, partial or malformed successful HTTP responses remain unconfirmed; connection loss does not become synthetic acceptance. A specific update is confirmed only after fresh identity and status match its original node, accepted version and accepted target partition, the running partition is that target, and the node reports confirmed without rollback. An earlier same-version update's status cannot satisfy the new candidate.

Upload and verification capture the exact session, node and address. Old unauthorized responses cannot clear replacement credentials. Single-node panels are replaced on selection/session changes, and outstanding timers, uploads and confirmation work are cancelled or ignored. The System & Recovery page follows the authoritative selected route, invalidates older identity/recovery reads, and reports unavailable metadata explicitly. A recoverable preflight failure retains the selected file. Firmware progress uses a labelled progress bar and the existing dashboard control/theme conventions.

Fleet rollout captures IDs, addresses, sessions and staging limits. Every node needs readable capabilities, matching identity/family, explicit OTA support, healthy memory/output and valid slot capacity. Those checks run again immediately before each upload. Canaries verify sequentially; a failed or unconfirmed canary stops later uploads, and any failed batch stops subsequent batches. Already active batch members may finish. Verification has a real deadline, retries ordinary per-request reboot timeouts and never substitutes generic health/version for unavailable OTA status.

For legacy firmware, the client sends the new header only when target binding is advertised, preserving older CORS behavior. It still verifies identity before and after, but legacy firmware cannot enforce identity at the upload boundary. Missing receipt/partition metadata remains unconfirmed. Cancellation prevents later browser requests; an update already accepted by a node may still complete. Reconnection follows the original address; automatic DHCP rediscovery during an update is not implemented.

## Timeline Studio

Timeline reads, saves, imports, navigation dialogs and playback continuations belong to their original node/session/generation. Failed new-node loads cannot leave another node's document editable under the new target. Saving retains the submitted baseline while later edits remain dirty. Malformed input cannot normalize into an apparently valid empty saved timeline. Failed scene-library reads block edits requiring that library instead of fabricating an empty result.

Change Scene replaces the selected block. Discard resumes the actual requested destination. Import and conflict dialogs use the shared operator dialog and focus conventions. The lane grid can scroll through every channel, including P8. Playback distinguishes accepted commands from confirmation in the authoritative transport generation/checksum. Known remote conflicts keep Run/Restart blocked after Keep My Draft until the saved baseline is reconciled, preventing a saved sequence from starting at a stale tempo; Stop remains available.

## Validation and build identity

| Gate | Result |
| --- | --- |
| ESP-IDF factory and diagnostic environments | Both compiled and passed application-slot checks |
| Output driver host scenarios | 36 plus the separate PM-quarantine scenario passed with ASan/UBSan |
| Production lighting host scenarios | 24 passed with ASan/UBSan, including stale-topology retry and invalid/unavailable hardware rejection |
| CueRunner, remote callback and local admission host scenarios | 29 passed with ASan/UBSan; the old baseline fails 24 of those regression scenarios |
| Production OTA upload/status host scenarios | 28 passed with ASan/UBSan, including bound identity before body/flash mutation |
| Combined web unit gate | 329 tests passed in 23 files |
| Timeline focused subset | 77 tests passed, including preserved prior API/model coverage |
| Chromium checks | 10 cases passed: 3 Timeline flows, 6 System/OTA/Fleet flows and 1 Fleet button render/contrast case in both color schemes |
| Svelte diagnostics | Zero errors and zero warnings after integration corrections |
| Public copy, marketing and continuity checks | Passed |
| Web production build | Passed |
| Documentation generation | 65 guides, 88 routes and 10 source-backed capacities synchronized; current links valid |
| Runtime mathematics and architecture invariants | Passed |
| Strict API surface audit | Passed |
| Candidate packaging | Both bootloaders/application images passed integrity checks; merged image, partition layout and staged hashes verified |

Host fault injection exercises production sources with controlled boundaries. The page-function harness executes current production functions with deferred promises but does not emulate Svelte reactivity. Chromium checks exercise actual file selection, reactive documents, dialogs, mobile tabs, node switches, pending receipts, candidate confirmation and fleet teardown against mocked device endpoints. The enabled Fleet primary button uses an intentional fixed dark foreground on the existing accent: its computed opaque contrast is 5.76:1 in both light and dark media preferences. This individual check is not a full accessibility audit. These are distinct from device or network measurements.

Independent review caught and resolved the Timeline conflict/tempo admission gap, an unsupported snapshot field, the OTA retry file-loss path and mobile tab overlap. The final type gate also caught a real contract mismatch: capabilities has family/model/version metadata but no immutable node ID. Fleet preflight now obtains identity through fresh `/identity` and `/ota/status` reads; tests use the actual capabilities shape. An ordinary per-request timeout during reboot retries within the overall verification deadline, and ownership is checked immediately before a fleet write.

### Build identity and packaging

Pinned firmware tooling: Espressif32 6.7.0, ESP-IDF 5.2.1, GCC 13.2.0, PlatformIO 6.2.0 and esptool 4.5.1. Both environments target ESP32 with 8 MB flash, DIO, 40 MHz and partition layout v2. The diagnostic image contains development flags and is not the production image. Web validation used the available Node 24.22.1/npm 11.9.0 environment; hosted CI remains configured for Node 20.

All **634 build input files**, including the restored managed dependency tree, matched before and after compilation. The inventory SHA-256 is:

`eff44f45d693f73b08f769c085798b84d5bbe50af8c35346322d91a727aa450d`

The initial factory/diagnostic invocation passed factory and exposed an old diagnostic CueRunner object/archive at linking. Invalidating those generated files rebuilt the required signature. A verbose retry then stopped in SCons command display before image generation; the normal completion run passed. No source or build setting was changed to bypass those failures. The diagnostic provenance review checked 56 project objects, 17 archives and 891 recorded dependency edges (208 unique hashed files), with no current-file/signature mismatches. The retained evidence establishes a stale incremental artifact, but does not establish its original cause or constitute an independent clean SDK rebuild.

The local candidate manifest truthfully records synthetic checkout snapshot `2a354994a7b99b482fc6dec15b61397d8d61c1d5` with a dirty working tree. That snapshot is not an upstream build-commit claim. The upstream baseline above and the input inventory identify this reviewed source set; a release build should record its clean final Git commit and retain its build log.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Factory application | 1,388,320 | `79f9314481b129a5122a5384e77a498260e3afb943d7d7f622343dc903218ff9` |
| Diagnostic application | 1,589,696 | `49c8725c6493d0e9a1e712b08c93b0b60eb3378daf8fe5fa49c6614a3ebc52de` |
| Merged factory image | 1,453,856 | `8748edadfae8a02230ff3871468c345260b4fe7c5c0a1bad730446a16cb03503` |

Each application slot is 2,424,832 bytes. Factory usage is 57.3%, with 1,036,512 bytes remaining; diagnostic usage is 65.6%, with 835,136 bytes remaining. The factory static acyclic direct-call boot-stack report remains 5,648 bytes; indirect, ROM and interrupt use still requires target measurement. The merged image is intended for fresh installation; preserve existing NVS/OTA metadata through the approved application-only OTA or migration procedure for installed devices.

The committed workflow adds the CueRunner host harness and the focused Timeline, OTA, deployment and System page unit gates. Browser interactions are recorded local evidence and are not newly claimed as hosted-CI jobs.

## Readiness boundary

No board was flashed or physically measured in this session. Source review, sanitizers, target compilation and mocked browser interactions do not establish electrical/RF/EMC/thermal acceptance, measured synchronization accuracy or physical recovery behavior. The candidate's exact hardware/output topology still needs the [continuity qualification plan](CONTINUITY_QUALIFICATION.md), including same-version OTA, rollback, mixed-output fault recovery and multi-node pause/resume observations.

Signed OTA capability already follows the effective build setting and is disabled by default. The roadmap now records the remaining production signing/provisioning work instead of an already resolved advertised-flag mismatch. No signing keys, secure boot or irreversible eFuse settings are provisioned by this batch.

The latest hosted run observed on the source baseline, [41288861129](https://github.com/slafftrosheen/lumigrid/actions/runs/41288861129), ended in `startup_failure` before any jobs or check runs existed. Its cause was unavailable through the connector. Local evidence is separate; the published commit's hosted result must be checked independently.
