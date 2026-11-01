# Power accounting and editor continuity — 2026-11-01

Source baseline: `7579f28d2860f69b7a9557cb51b0d4a39fdb30a7` on `main`. This is the second November 1 implementation batch, following [the earlier hardware/playback report](2026-11-01-POLISH.md).

## Changes and resulting behavior

### Output power and effect rendering

The renderer, final output guard and configuration preflight now share the conservative model already used by the final guard: 5 mW idle per configured pixel, 295 mW per full RGB component, and 380 mW per full white component. This resolves the model disagreement recorded in the earlier report without claiming calibration against the installed strips.

Fractional RGB/RGBW/PWM loads remain exact through channel, supply and global limits, soft start and transient limits. Each stage re-estimates the quantized commands. Only active demand scales; fixed idle remains. Reporting rounds upward after aggregation. The mixed-limit regression checks transmitted RGB/RGBW bytes and PWM duties: **5,665 mW requested becomes 1,193 mW final**, split into 706 mW and 487 mW supply estimates, with no second dimming by the final guard.

An enabled supply with zero usable capacity extinguishes assigned active outputs, including unknown-rated PWM. This covers explicit zero derating and positive settings that round to zero capacity. Disabled supplies remain disabled tiers. If a budget is below fixed addressable idle, active demand becomes zero while the estimate retains that idle floor.

Runtime/API power snapshots come from the final confirmed output command. A failed commit followed by a failed blackout preserves the previous estimate and marks it incomplete/unknown. Confirmed blackout and PCA recovery refresh the estimate. Lighting and output locks are acquired separately, avoiding an inverted nested lock order. Existing JSON field names remain unchanged; the dashboard carries the existing completeness flag through normalization and qualifies incomplete estimates.

### Synchronization continuity

A follower must successfully release the previous remote transport before accepting a preferred or replacement leader. Rejected release retains the previous source, sequence and snapshot; it does not discipline the clock, feed the watchdog, apply the packet or send an acknowledgement for the replacement. The worker rechecks shutdown ownership after release. This also handles a rebooted leader reusing prior generation values. The wire protocol remains version 6 with 76-byte packets.

### Dashboard and editing

HTTP responses, stream snapshots, command receipts, login/bootstrap work and network applies belong to the node and session where they began. Switching A → B → A cannot revive old requests. Caller cancellation and deadlines cover JSON body reads as well as response headers. An old unauthorized response cannot clear newer credentials.

Preview packets do not refresh status freshness. The overview and sidebar distinguish current, stale and unavailable state; missing values do not become fabricated healthy/normal/zero-power results. Network edits preserve drafts after denied saves or failed reads, and apply polling has a real deadline plus ownership for each apply attempt.

Scene and cue reads preserve the loaded document when a replacement fails. Saves retain the submitted version as the saved baseline while edits made during the request remain dirty. Save/run/delete/import continuations stay with their original node/session and selection. Reconnects preserve the same session's drafts while cancelling superseded work. Failed initial metadata reads block creation and saves; failed scene-dependency reads block deletion.

Scene conflict reload fetches the current node scene and motion. Selecting a scene while continuing to edit the visible draft preserves those edits. A delayed delete cannot clear a newer draft in the same slot; edits made during deletion remain recoverable as an unsaved draft. New, Delete and Unsaved Changes dialogs now open and place keyboard focus inside the shared dialog. A persistent Cancel/Close button retains focus as dependency reads complete. Scene loading and duplication snapshot reactive state before cloning it; actual browser checks caught the original proxy-cloning failure. Cue saves use the editor's actual busy state, and subscriptions/timers are released on teardown.

### Candidate packaging

The candidate packager checks the ESP32 image target, segment count and bounds, segment alignment, checksum padding, payload XOR checksum and appended SHA-256 for both bootloader and application images. It rejects truncated payloads, corrupt checksums/digests, missing required digests and unexpected trailing data. The accepted format is the pinned build's unsigned, unpadded image contract; signing or padded formats require an explicit packaging update.

Fingerprints are calculated from the exact bytes checked. Replacing an application, bootloader, partition table or merged image between validation and staging blocks packaging. The staged copies are checked again against those fingerprints. These checks detect corruption and replacement during packaging; they are not secure-boot authentication or build attestation.

## Validation

| Gate | Result |
| --- | --- |
| ESP-IDF factory and diagnostic environments | Both built successfully in one integration invocation |
| Output driver host tests | 34 production scenarios plus 1 PM-quarantine scenario passed with ASan/UBSan |
| Lighting host tests | 15 existing scenarios and 6 new power scenarios passed with ASan/UBSan, including targeted corrections |
| NodeSync host tests | 39 deterministic cases and 6 real-thread cases passed with UBSan |
| Candidate packaging regressions | 19 tests passed, including five artifact-replacement variants |
| Scene page async regressions | 17 cases passed against the production page functions |
| Cue page/store/document/API regressions | 55 tests passed, including 28 page async cases |
| Combined web unit gate | 186 tests passed in 15 files, including the scene/cue cases above |
| Chromium interaction checks | 13 distinct cases passed: 7 core continuity, 3 existing setup/login, brightness draft preservation, and 2 Scene Studio cases |
| Svelte, public-copy and continuity checks | Zero errors or warnings; copy, marketing and continuity checks passed |
| Web production build | Passed |
| Documentation generation | 64 guides, 88 routes and 10 source-backed capacities synchronized; links validated |
| Runtime arithmetic and architectural invariants | Passed |
| Strict API surface audit | Passed |
| Actual candidate package | Both bootloaders and both application images passed integrity checks; merged image and partition layout verified |

The focused page harness executes actual production function declarations with controlled state and deferred promises. It does not substitute for rendered Svelte browser checks. The workflow includes the focused node ownership, transport, command tracking, scene/cue editor and network retry regressions.

## Build identity

Pinned firmware tooling: Espressif32 6.7.0, ESP-IDF 5.2.1, GCC 13.2.0, PlatformIO 6.2.0 and esptool 4.5.1. Both environments use ESP32, 8 MB flash, DIO and 40 MHz flash frequency with partition layout v2. The diagnostic image contains development flags and is not the production candidate.

The build input inventory contains **630 files**, including the actual restored managed dependency tree. Its SHA-256 was unchanged before and after the successful integration build:

`66c7efa4167dbf5e98ca96a46f623834cfd0e0da16842c03601bddaff2a4583c`

The local checkout uses synthetic snapshot history. Its candidate manifest truthfully records local snapshot `4e4d9f829edf5b0cf33a0f80e7efd96ca7affab4` with a dirty working tree; that value is not an upstream build-commit claim. The baseline above plus the recorded input inventory identifies the reviewed build inputs. A clean release build should record its own final Git commit and artifacts.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Factory application | 1,387,280 | `b622a0d83b7aebe38077ff2b4036011c0427248c8901698df8fea0bb88abaea6` |
| Diagnostic application | 1,588,544 | `27b4c943615abd1c3a2b790b5b05a12c4cf6343653da62fbea128f48b47e3095` |
| Merged factory image | 1,452,816 | `f0f09307a132c77789c29baa1c98e3e6b42b2aeccc8bda7a3869cb3a7e8f899f` |

Each application slot is 2,424,832 bytes. Factory usage is 57.2%, with 1,037,552 bytes remaining; diagnostic usage is 65.5%, with 836,288 bytes remaining. The factory static acyclic direct-call stack report is 5,648 bytes; indirect, ROM and interrupt usage still requires target validation.

The merged image is intended for fresh installation. Its erased gaps include NVS/OTA metadata; use the approved migration or application-only OTA procedure for existing devices rather than overwriting their configuration.

## Readiness boundary

This batch provides source review, host regressions, rendered browser checks, target compilation and candidate-byte integrity evidence. No board was flashed or measured in this session. Output-current calibration, all-channel/network coexistence, cold-start/reset captures, multi-node timing, injected hardware recovery, thermal endurance, OTA/rollback and the applicable conformity assessment remain physical acceptance work. Follow [the continuity qualification plan](CONTINUITY_QUALIFICATION.md) against the exact assembly and image.

Positive budgets cannot fully constrain unknown-rated PWM loads. Brightness scaling cannot remove fixed addressable idle. An unconfirmed I/O operation cannot establish physical consumption. The dashboard therefore presents command estimates and their completeness, not current measurements or a CE result.

The latest hosted run observed on the source baseline, [41286129799](https://github.com/slafftrosheen/lumigrid/actions/runs/41286129799), ended in `startup_failure` before any jobs or check runs existed. The connector did not expose a cause. Local build/test passes above are separate evidence; the newly published commit's hosted result must be checked independently.
