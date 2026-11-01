# PWM precision, Timeline admission and operator acceptance — 2026-11-01

Source baseline: `4cac2d33f402af30d8c311155fa050c877803026` on `main`. This fourth November 1 batch follows [output recovery, cue admission and verified updates](2026-11-01-RECOVERY-UPDATES.md). It improves firmware behavior and the operator workflow; no board was flashed or physically qualified.

## PWM configuration and color precision

PCA mode verification now checks all stable bits relevant to the configured output path: `(MODE1 & 0x7f) == 0x20` and `(MODE2 & 0x1f) == 0x04`. The masks require internal clock, auto-increment, no shared-call addresses, non-inverted output, update on STOP, the configured output driver, and LOW when `/OE` is HIGH. RESTART is excluded because it self-clears. These settings follow the [NXP PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf), Rev. 4, Tables 5 and 6.

A proven register mismatch invalidates initialization and frame readiness, requests output inhibition and prevents a later acknowledged zero write from being announced as a confirmed safe frame. Successful register verification clears that mismatch. Recoverable configuration errors are repaired; persistent incompatible modes remain a recovery failure. No bus-wide reset was added. A requested `/OE HIGH` cannot prove physical darkness when the hardware mode or circuit is incompatible.

Color-derived monochrome PWM now promotes an 8-bit color value into the full 12-bit range before brightness scaling and gamma. Previously a color value of 255 became a duty of 255 out of 4095 before gamma. Explicit nonzero 12-bit PWM remains authoritative. Linear gamma preserves every scaled input duty; nonlinear gamma interpolates the existing lookup table instead of discarding the low bits. This uses no larger lookup table or per-frame floating-point calculation.

The corrected color path can drive substantially brighter output for the same color command. The final power guard remains in place, and its production host scenarios pass; actual load/current/brightness must be rechecked on the candidate hardware. Tests preserve compound PWM RGBW, explicit addressable white, brightness scaling and blackout behavior. Existing DMX paths supplying explicit 12-bit duty did not have the 8-bit promotion defect.

## Timeline content and clock continuity

Local starts admit the exact newly loaded sequence before changing ownership. Remote starts reuse an active immutable sequence whose checksum matches the leader, or load and validate matching saved content before replacing it. A saved edit no longer invalidates the active matching copy, and a content change can replace an active remote copy even when its transport generation is unchanged.

Admission checks track bounds, step references and every enabled referenced Scene. Missing content, unavailable Scenes, invalid data, checksum mismatches and invalid tempo preserve the current Cue or Timeline. Accepted remote takeover clears preempted local/calendar flags, preventing the remote sequence from restarting under local ownership when sync ends. Failed reloads preserve the saved-content cache; storage reads happen outside the scheduler spinlock. Playback evaluates a captured sequence rather than an unlocked mutable reference.

Explicit API tempo must be an integer from 10 through 500 BPM. Missing overrides use the admitted sequence's stored tempo. Timeline step selection and beat telemetry use integer arithmetic over the original millisecond clock, avoiding cumulative drift from truncated milliseconds per beat and the reproduced division-by-zero path. Effect beat-period output keeps its existing integer-millisecond interface. Every accepted Start advances the transport generation because it creates a new playback anchor.

This is content admission, not a new cross-component transaction that prevents a Scene from being deleted afterward. Host clocks and substituted RTOS boundaries do not establish physical multi-node timing or task latency.

## Live Control ownership and release

The desk follows the authoritative selected node, exact authentication session and connection generation, including A→B→A changes. Old replies cannot restore a canceled lease, publish foreign state or select the target for a later write. Changed nodes/sessions clear foreign drafts and dialogs; a reconnect within the same workspace may retain draft values while stopping renewal and reloading metadata.

Acquisition, target updates, edits and heartbeats share a serialized request path. Pending edits coalesce to the newest submitted values. Accepted masks and returned lease duration are validated, and accepted values remain distinct from newer browser edits. Configuration failure leaves ports unavailable with an explicit retry instead of assuming every output is enabled. Touch selection no longer requires a physical modifier key.

Cancellation invalidates local ownership immediately and stops queued edits and renewals. A PUT already sent to a device is allowed to settle within its request timeout before ordered cleanup; canceling a browser request cannot retract that server-side write. Release requires an actual `released: true` receipt. Negative receipts and HTTP failures remain visible with Retry release, including cleanup after cancellation. An old cleanup result cannot change a replacement acquisition or authentication session. Cleanup does not borrow replacement credentials.

Fresh matching status and a locally accepted lease are shown separately. Stale status, a changed mask or a different output owner stops renewal. The existing protocol has no per-browser lease identifier: matching runtime mask/owner cannot prove that another client has not acquired the same mask. Missing manual fields currently normalize to inactive/zero in the shared snapshot adapter; this batch does not redesign that adapter or the device lease protocol.

Save as Scene captures the accepted values, node/session and name before reading the library. Later selection changes or edits cannot redirect that save. The result must include `saved: true`; failed reads and saves preserve honest error feedback.

## Hardware acceptance evidence

The page is presented as the Hardware Acceptance Bench, with internal checks passed rather than a regulatory certification badge. It keeps node identity, freshness, active execution and unavailable metadata explicit. Requests, dialogs, reports and polling belong to their original selected node/session; late responses are ignored, polls do not overlap, and background success does not erase a command failure.

Current firmware advertises `evidenceBinding: "session_attempt_v1"`. New session/test/abort requests carry the observed certification ID; result submissions also carry the test's start uptime and attempt count. A changed session or attempt fails with HTTP 409 before mutation. Session IDs use a random 64-bit suffix instead of uptime alone. Legacy requests omitting all bindings retain active-test guards, but only bound clients receive the new stale-attempt protection. The current bench keeps older firmware read-only with update guidance.

Only the currently executing manual test waiting for the operator accepts external evidence. Automatic probes cannot be replaced with an observation, completed attempts cannot be overwritten, and physical evidence requires the matching runner owner/session. Starting a retry clears its previous measurement and note. Attempts count executions and do not wrap; the limit requires aborting the current session and beginning a new one.

Finite instrument readings and exact plan units are required for both pass and fail. Zero remains a real reading. The entered method and observation are stored together within the existing 95-byte UTF-8 note limit; oversized notes, units and serial numbers are rejected rather than silently truncated. A requested pass outside tolerance becomes failed evidence, and the page reads back the result instead of inferring success from its request.

Physical confirmation now has an acknowledged completion phase. The worker rejects stale, empty and duplicate confirmations without affecting another session, disables test/stream/loop producers, performs blackout and only then publishes terminal state/error. Failed blackout produces failed evidence and a failed command receipt. The service retains its active test and runner ownership while `confirmationPending` is true, blocks new work, and persists the attempt as running until acknowledgment. Reboot therefore cannot load that unfinished stop as a pass. Cancellation and persistence failure supersede confirmation; configuration/image invalidation cannot be undone by a late success.

The page uses only the bound acceptance abort for its owned session. HTTP 202 means cancellation was requested; it is not output completion. Shared OperatorDialog provides the existing keyboard/focus behavior, and the report matrix has a named keyboard-reachable scroll region. A pending evidence receipt is invalidated when its attempt changes, so it cannot block later tests. Reports retain zero values, evidence notes and stale/incomplete status. Execution-error results label numeric values as unconfirmed stored values: the durable schema has no independent measurement-presence bit, so an initialized zero must not be presented as proof of a measurement. Actual submitted zero values and the raw report fields remain available. Device timestamps are uptime, not Unix dates; the UI shows uptime/duration. Reporting firmware version/hash identify the exporting image and do not retrospectively prove which image collected older evidence.

The API details and limits are in [Hardware acceptance](../HARDWARE_CERTIFICATION.md).

## Verification

| Gate | Result |
| --- | --- |
| Production output drivers | 38 groups plus the separate PM quarantine case passed with ASan/UBSan |
| Production effects/color pipeline | 14 groups passed with ASan/UBSan; gamma checks sweep all 4096 duties for five gamma settings |
| Production lighting integration | 24 scenarios passed with ASan/UBSan |
| Production Scheduler, Timeline callbacks and API admission | 30 scenarios passed with ASan/UBSan |
| Existing CueRunner regression suite | 29 scenarios passed with ASan/UBSan |
| Production acceptance service | 48 scenarios passed with ASan/UBSan |
| Production acceptance HTTP handlers | 45 scenarios passed with ASan/UBSan |
| Production FactoryRunner jobs and confirmation | 14 scenarios passed with ASan/UBSan |
| Architecture invariants, runtime mathematics and strict API boundary | Passed |
| Factory and diagnostic firmware | Both compiled and passed application-slot checks |
| Combined web unit gate | 449 tests passed in 28 files, including 68 focused Live and 52 focused acceptance cases |
| Svelte diagnostics | Zero errors and zero warnings |
| Chromium operator flows | 6 passed: 3 Live and 3 acceptance flows, with additional independent cancellation/focus/stale-status checks |
| Public copy, marketing and continuity checks | Passed |
| Web production build | Passed |
| Documentation | 66 guides, 88 routes and 10 source-backed capacities synchronized; current links valid |

The four new PWM/color regression groups fail against the previous source. The new Scheduler suite reproduces 26 failures against the baseline, including UBSan division by zero. Eleven new service scenarios were run before its admission patch and all reproduced the original defects. All six new worker confirmation cases fail against the captured pre-fix worker. The current suites pass without changing the hardware boundary between comparisons.

Host tests execute production sources with controlled SDK, RTOS, storage and HTTP boundaries. Page-function tests execute current page functions but cannot establish Svelte reactivity; Chromium uses the rendered app and mocked devices. Independent review identified the physical-confirmation lifetime and invalidation gaps, canceled-lease cleanup reporting, mobile value clipping, stale attempt blocking, initialized-zero labeling and keyboard access to the report matrix. Browser testing identified report focus restoration. These were corrected within the affected workflows.

### Build identity

Pinned tooling remains Espressif32 6.7.0, ESP-IDF 5.2.1, GCC 13.2.0, PlatformIO 6.2.0 and esptool 4.5.1. Both environments target ESP32 with 8 MB flash, DIO, 40 MHz and partition layout v2. The diagnostic image is a development configuration. Web validation uses the available Node 24.22.1/npm 11.9.0; hosted CI remains on Node 20.

All **639 firmware input files**, including managed dependencies, matched before and after the successful build. Input inventory SHA-256:

`07c5341552accdfaf33fcb150a354124bbc4d5b1f7e80e42d84094fc3cc97c91`

| Application | Bytes | SHA-256 |
| --- | ---: | --- |
| Factory | 1,391,920 | `47398dbc00600f7f33c2ad21dbfaf633e199ec79cefb76f551dc08a99a1cc262` |
| Diagnostic | 1,593,456 | `678bd34b39dbe5834c65ce397148619d1bcd927e29f41d84e03276e324ddadfc` |

Each application slot is 2,424,832 bytes. Factory leaves 1,032,912 bytes; diagnostic leaves 831,376 bytes. The successful local build uses synthetic checkout snapshot `f6d846f51fc336dfbd7d7a69d57bd33ded1805d5` with reviewed working changes; that local snapshot is not an upstream build-commit claim. The baseline and full input inventory identify this candidate. A release build should record its clean final source commit and retain the build log.

Candidate packaging validated both bootloader/application image digests, flash configuration, partition layout, merged-image offsets and staged hashes. The merged factory image is 1,457,456 bytes with SHA-256 `3067160ebe7b0ae6f630da7bfe91a4b6abf2433678351861c6410ba06223d478`. The manifest explicitly records hardware validation as `NOT_RUN`. A merged image is for fresh installation; preserve installed-device NVS and OTA metadata through the approved application-only OTA or migration path.

The committed workflow now runs the Scheduler host suite and the Live/acceptance unit suites alongside the existing gates. Browser interactions are retained local evidence and are not newly claimed as hosted-CI jobs. The web integration workflow also builds the production SvelteKit application after its unit/type gates.

## Remaining readiness work

Electrical output behavior, measured current, RF/EMC, thermal endurance, physical recovery and multi-node timing remain **NOT_RUN** for this candidate. The [continuity qualification plan](CONTINUITY_QUALIFICATION.md) still requires observations tied to the actual image, PCB, wiring, supply, load, antenna and enclosure. In particular, check full-range color-derived PWM, low-level fades, faulted PCA modes, Timeline takeover with missing content, and confirmation/cancellation while blackout fails.

No signing keys, secure boot, eFuses or physical hardware were provisioned. Internal accepted evidence and software test success do not constitute CE approval.

The latest hosted workflow observed on the source baseline, [41291057549](https://github.com/slafftrosheen/lumigrid/actions/runs/41291057549), ended in `startup_failure` with zero jobs/checks. Its cause was not available through the connector. The new source commit's hosted outcome is checked separately after publication; local results are not reported as hosted CI success.
