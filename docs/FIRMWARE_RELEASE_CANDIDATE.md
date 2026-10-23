# Firmware release candidate 1.0.0-rc2

Date: 2026-10-23. Source baseline: `e75c52c10c4b4dea87c2cdb72fe1915b7d638b26` (rc1 plus the CI environment correction).

This candidate continues the rc1 reliability work with fixes to backup/restore, persistent configuration, OTA verification, worker shutdown, frame consistency and factory-job cancellation. It is a software candidate for bench testing. Physical stability, RF/EMC, electrical safety and thermal acceptance have not been measured in this change. Record the final Git commit and the exact artifact hashes from the build manifest when testing; the version string alone is insufficient to identify a binary.

## Additional changes in rc2

| Area | Reproduced fault | Resulting behavior |
| --- | --- | --- |
| Backup integrity | A valid export could fail its own parser; missing terminal records, altered checksums and duplicate records could be accepted | A v4 package requires its header, complete record sequence, exact count and raw-stream CRC; invalid or incomplete content is rejected before mutation |
| Backup fidelity and JSON safety | Short JSON strings were over-read; unsigned checksums were narrowed; palettes, motion speed, labels and protocol values could change | Bounded string copies and typed numeric checks preserve supported values; scene/configuration round trips and allocation failures have production-source regressions |
| Restore transaction | Snapshot errors, partial writes, stale readback and marker failures could still produce success; rollback touched unrelated records | Read-only affected-record snapshots precede a verified marker; every written payload is checked; rollback is limited to attempted records and incomplete recovery retains the marker |
| Board identity | A restore could replace the target serial/revision and legacy tokens | Restore retains the target identity, authentication fields and sync secret; Wi-Fi credentials remain outside the package |
| Persistent records | Network save mislabeled the schema; missing/corrupt primaries could hide valid backups; mirror failures were ignored | Compatible old network records migrate with checked results; valid backups load without silent repair writes; corrupt, absent and I/O-error states remain distinct |
| Interrupted restore at boot | Default seeding or successful config validation could overwrite the recovery condition | Active restore markers suppress default seeding; failed marker reads fail closed; outputs stay inhibited until explicit recovery |
| OTA image acceptance | A truncated upload could rely on an old image tail still in flash; boot selection could precede durable journal intent | IDF verifies the image against the bytes actually received; intent commits before boot selection; status exposes actual and persistence errors |
| OTA boot verification | Matching version strings could imply a false confirmation/rollback; unavailable NVS could bypass failure handling | Running partition and bootloader state decide the outcome; healthy dwell and rollback are latched; early startup faults can request rollback even before normal verifier initialization |
| Runtime ownership | A timed-out stop could clear the ownership flag and allow re-entry | Stop errors propagate; live tasks, callbacks and synchronization objects remain owned until acknowledgement; failed shutdown does not rebind Wi-Fi underneath a live service |
| Lighting state | Concurrent manual/scene changes could produce mixed frames; short live frames retained pixel tails; an older reset could clear a newer logical stop | Composition uses a consistent state under the existing mutex; unused live pixels are cleared; deferred stop reset is tied to the controller generation; restart clears old manual/frozen transitions |
| Factory jobs | Cancelled could be published before the worker stopped output; a full queue could leave orphan job state | Cancellation remains pending until the worker processes it; queue failures retain ownership or fail the rejected start; terminal jobs cannot be revived by late progress; HTTP and dashboard messages report acceptance until completion |
| Certification cancellation | Evidence-save failures and aborts could release a live burn-in job; rejected physical starts retained nonexistent session IDs | The service retains and retries owned cancellation until worker acknowledgement, blocks competing manual starts during certification, and records rejected physical tests as Blocked |

Restore is a checked multi-record operation, not an atomic NVS transaction. A power loss can leave a partial set of records. The durable marker causes recovery on the next boot; it does not reconstruct the lost in-memory rollback snapshot. A second partial restore is rejected while that marker is active. Retain/export known-good backups and use explicit recovery/recommissioning for an interrupted restore. Admission preserves the existing 16 KB reserve and checks both total scratch memory and the largest individual allocation; low-memory restores can return 507 without mutation.

## Changes inherited from rc1 relevant to board qualification

| Area | Defect addressed | Intended behavior |
| --- | --- | --- |
| Early GPIO safety | Output mode could expose a low reset latch before `/OE` was raised; output-only pads could not provide meaningful readback | Preload safe levels, enable readable output pads, check all operations and refuse runtime startup if the early safety step fails |
| PWM output | A zero-frame prerequisite prevented normal nonzero output; recovery and gate failures could be reported incorrectly | Enable a valid frame only after initialization/recovery succeeds; keep faults inhibited and count gate failure as a failed commit |
| Stop during a commit | An interleaved commit could reopen the PWM gate or a clear operation could erase a newer stop | Serialize the final gate decision with the stop latch and retain newer stop generations |
| RMT transport | A timeout was treated as if hardware had released its frame buffer and channel | Track in-flight ownership, bound queue/drain waits and quarantine incomplete teardown until safe recovery is possible |
| Wi-Fi recovery | Late events and immediate connection failures could strand fallback/retry state | Gate events during stop, preserve recovery AP behavior, retry synchronous failures and bound callback shutdown |
| Network apply | Failed Wi-Fi stop and failed readiness allocation could be ignored | Keep live callback ownership intact, report transition failures and avoid waiting on a null semaphore |
| Configuration copying | Fixed-size copies read beyond short strings | Copy with source termination and destination bounds; sanitizer regressions cover short and legacy buffers |
| Internal acceptance plan | 46 entries were written into a 40-entry record; long IDs were truncated; incomplete evidence could pass | Bound the plan and IDs, require every required result, validate evidence and invalidate older schemas |
| Certification persistence | Commit failures could be reported as success and raw mirrored records exceeded available NVS capacity | Check storage results, preserve the previous committed generation, validate loaded records and use bounded lossless encoding |
| Release packaging | The workflow merged an 8 MB layout with a 4 MB image declaration | Enforce consistent 8 MB layout/image metadata, package exact binaries and their configurations/hashes, and publish CI artifacts |

The default firmware accurately reports that signed OTA verification is disabled. This change does not provision signing keys, secure boot or irreversible eFuse settings.

## Build and reproducibility

The build pins PlatformIO 6.2.0 in CI, the Espressif32 platform to 6.7.0 and ESP-IDF to the platform's 5.2.1 package. The embedded dashboard uses the checked-in npm lockfile. Build the production candidate and the diagnostic image separately:

```sh
python -m pip install platformio==6.2.0
npm ci --prefix lumigrid-lander-main
cd LumigridLedNode
pio run -e factory -e test
```

The GitHub firmware workflow is the canonical packaging command sequence. It uses the installed PlatformIO esptool package to merge the factory image with `--flash_size 8MB`, then runs `scripts/firmware_release_manifest.py`. Its artifact is named `lumigrid-firmware-candidate-<Git SHA>` and includes:

- Factory and test application binaries, bootloaders, partition tables and ELF files.
- Effective sdkconfig files and generated build metadata for each environment.
- The generated embedded dashboard, dependency lock and pinned build configuration.
- Source and artifact SHA-256 inventories, tool versions, image/partition validation and the factory linked-stack report.
- Explicit `hardware_validation: NOT_RUN` and `compliance_assessment: NOT_PERFORMED` labels.

The manifest records current matching source inputs and successful packaging checks; it is not cryptographic proof of the compilation process. Build with a clean checkout, retain the build log and do not modify inputs during a release build. Use the factory binary for normal operation. The test image contains diagnostic/development flags and is not the product image.

The merged image is intended for fresh installation. It includes erased padding across NVS/OTA metadata. For an existing board, preserve its configuration using the appropriate [flash migration or application-only OTA procedure](FLASH_LAYOUT.md).

## Software verification

The committed workflow runs boot configuration/stack checks, partition and packaging regressions, board GPIO failure injection, output-driver and Wi-Fi recovery scenarios, configuration memory-safety checks, certification service/API/storage regressions, architectural invariants and runtime mathematics. Output, configuration and certification host harnesses exercise production source with AddressSanitizer/UndefinedBehaviorSanitizer where applicable. Host stubs model error conditions; they do not emulate ESP32 peripheral timing or RF behavior.

Both ESP32 images must compile and satisfy the production partition/image budget. The factory ELF is checked for boot-stack limits. Building the diagnostic image does not mean its on-device suite has run. The final change report records the actual checks completed for this candidate.

### rc2 software verification

The rc2 workflow adds production-source host suites for persistent records, backup/export/ingest, restore transactions, runtime recovery, terminal jobs, factory cancellation and admission, lighting state, OTA boot verification and upload. It also runs the existing effects and catalogue API gates. Fault injection includes NVS writes/commits/readback, socket interruption, failed allocation, queued producers and delayed worker acknowledgement. AddressSanitizer/UndefinedBehaviorSanitizer are enabled by the new host harnesses. LeakSanitizer is disabled in the restricted host runtime; the backup harness also tracks live cJSON allocations explicitly.

| rc2 host check | Observed result |
| --- | --- |
| Persistent configuration | 44 scenarios passed, including 42 injected write/commit failure combinations within the matrix cases |
| Backup export, parsing and fidelity | 33 scenarios passed, including malformed/truncated packages, allocation failures, integer boundaries and full supported timeline tempo range |
| Restore transaction and rollback | 35 scenarios passed, including snapshots, partial writes, readback mismatch, marker failures and preservation of absent/unrelated records |
| Runtime recovery and shutdown ownership | 16 scenarios passed |
| OTA boot verification / upload and status | 24 / 25 scenarios passed |
| Output drivers / lighting state / terminal jobs | 30 / 13 / 5 scenarios passed; output total includes the PM teardown variant |
| Factory worker cancellation and lifetime | 8 scenarios passed |
| Factory cancellation and admission API | 17 scenarios passed, including active/pending certification exclusion and idle/no-service controls |
| Certification service / result and abort API | 31 / 20 scenarios passed |
| Wi-Fi recovery / bounded configuration strings | 25 / 6 scenarios passed |
| Boot configuration / stack parser / partition / release manifest | 8 / 7 / 3 / 10 checks passed |
| Board GPIO failure injection | Safe latch/readback and 27 injected failures passed |
| Certification storage | 22 scenarios passed, plus the codec-pattern matrix |
| Effects / catalogue API / embedded dashboard | 12 effect groups, real catalogue/scene-handler regression harness and embedded dashboard smoke passed |

| rc2 build and packaging check | Observed result |
| --- | --- |
| Final ESP32 factory build | PASS; application binary 1,380,336 bytes, 56.9% of the 2,424,832-byte slot |
| Final ESP32 diagnostic build | PASS; application binary 1,580,704 bytes, 65.2% of the slot |
| Factory linked boot-stack gate | PASS; estimated acyclic direct-call path 5,616 bytes; indirect/ROM/interrupt use still requires target measurement |
| Source consistency across the final build | PASS; all 601 inventoried source inputs matched before and after compilation |
| Web application and generated documentation | PASS |
| Architectural invariants and runtime mathematics | PASS |
| Real merged-image candidate packaging | PASS; image contents, 8 MB configuration, partitions and artifact hashes validated |

The factory application SHA-256 is `227f2aaffb08d7464cfcba02f9b2bc9b071554db3abf9cc5f059b746e19a4140`; the diagnostic application SHA-256 is `b8fec989415d20edf964fe134c8fa1c1546f50a0e5155719fff2e24cd3a071cc`. The candidate manifest contains the complete inventories and the final source commit. Hosted CI status is reported separately from these local checks.

The values in the next table are retained as **rc1 evidence**, not measurements of the rc2 binary.

### Prior rc1 measurements from 2026-10-23

| Check | Observed result |
| --- | --- |
| Final ESP32 factory build | PASS; application binary 1,367,456 bytes, 56.4% of the 2,424,832-byte slot |
| Final ESP32 diagnostic build | PASS; application binary 1,571,776 bytes, 64.8% of the slot |
| Factory linked boot-stack gate | PASS; estimated acyclic direct-call path 5,632 bytes; indirect/ROM/interrupt use still requires target measurement |
| Diagnostic certification stack frames | Compiled service test 448 bytes; physical-output-engine test 1,168 bytes, with large certification fixtures on checked heap allocations |
| Output driver host regressions | 28 scenarios plus 1 PM-enabled teardown/quarantine scenario passed |
| Wi-Fi host regressions | 25 scenarios passed, including old-session events and bounded callback/event-queue shutdown |
| Certification service / API / storage | 20 / 17 / 22 scenarios passed; storage coverage also exercises 96 codec patterns and modeled capacity |
| Early board GPIO / configuration copy | Safe latch/readback and 27 injected GPIO failures passed; 6 bounded-copy memory-safety cases passed |
| Boot configuration / boot-stack parser / partition layout / release manifest | 8 / 7 / 3 / 10 tests passed |
| Effects and catalogue API | 12 effect groups and real catalogue/scene-handler regressions passed |
| Architectural invariants and runtime mathematics | PASS |
| Web application build and generated documentation checks | PASS |
| Real merged-image candidate packaging | PASS; binary contents, effective 8 MB configuration, partitions and artifact hashes validated |

Those rc1 runtime/build inputs were fingerprinted during the frozen final build and matched after it. The raw certification record is 8,000 bytes; the host fixtures store an initial session in 833 bytes and a representative completed session in 2,609 bytes. These sizes describe those fixtures, not guaranteed capacity for every note/configuration combination. No physical measurements or on-device diagnostic execution are included in these results.

## Bench acceptance before the lab handoff

Use the intended PCB revision, enclosure, antenna, supplies, wiring and supported maximum loads. The sequence below is an engineering acceptance proposal; the lab must confirm the product's applicable conformity scope and operating modes.

| Gate | Exercise | Evidence to retain |
| --- | --- | --- |
| Image identity | Read board identity, firmware version and application ELF hash; match them to the archived candidate | Git SHA, binary/ELF hashes, board serial/revision, flash size and effective configuration |
| Boot and reset | Repeat cold starts and software resets; observe `/OE` and all eight addressable data pins from power application through readiness | Serial log and scope captures showing safe boot behavior; actual pull-up/level-shifter behavior before firmware begins |
| PWM channels | Exercise P1–P8 individually through zero, intermediate and intended full duty; inhibit with `/OE` | Scope duty/frequency, mapping and current observations; no unintended active channels |
| Addressable channels | Exercise A1–A8, correct color order, maximum intended pixel count and combined updates | Visible mapping plus logic-analyzer timing/error evidence under concurrent Wi-Fi traffic |
| Failure recovery | Introduce controlled I2C faults, network loss/reconnect and logical stop during output changes | Fault diagnostics, inhibited PWM, truthful blackout status, AP availability and successful bounded recovery |
| State persistence | Save intended configuration, network settings and acceptance results; round-trip a v4 backup; reboot and read back; test controlled power interruptions during save and restore | Previous or new complete valid record, explicit capacity/write errors, preserved credentials/configuration and exported reports |
| OTA and rollback | Perform compatible application updates, same-version updates, truncated uploads and controlled rejected/interrupted updates; induce startup failure before configuration init | Correct running hash, healthy dwell/verification, recovery path and preserved configuration |
| Combined endurance | Run the intended output topology with network/API/protocol traffic and representative scene transitions for the agreed duration | Continuous serial logs, free/largest heap and task stack minima, reset/watchdog/transport counters, actual supply current and temperatures |
| Radio/EMC preparation | Confirm Wi-Fi modes/channels, antenna and transmission settings with the lab; supply requested RF test firmware separately | Frozen product configuration and the lab's agreed operating-mode/test matrix |

Report each gate as PASS, FAIL or NOT_RUN with its observations and instrument context. Missing observations, sentinel telemetry values and a `MARGINAL` stress-run result are not a pass. `scripts/hardware_coexistence_soak.py` can drive combined-load exercise, but its summary does not replace electrical measurements or review of the raw diagnostics.

### Limits requiring explicit disposition

- The current board capability flags declare no physical emergency-stop input and no addressable power gate. If RMT is stuck, software cannot guarantee that previously latched LED output is dark. Validate the actual hardware shutdown requirement and supply isolation design.
- NVS remains 16 KB. Encoded acceptance records reduce ordinary storage pressure, but full/high-entropy notes plus other stored content can still exhaust it. Verify the intended production content, export records and resolve any `persistence_failed` state before accepting a unit. No automatic whole-partition erase is performed by these fixes.
- Signature enforcement is disabled in the default candidate. Confirm the required production security/provisioning design and applicable product requirements before market release.
- No physical board, RF, EMC, thermal, electrical or long-duration measurements were performed by this software change. Previous reports remain evidence only for their original hardware and image.

## CE preparation references

CE marking is the manufacturer's conformity declaration supported by the applicable assessment and technical documentation. There is no general EU-issued permission certificate for attaching the mark; whether a notified body is required depends on the applicable legislation and assessment route. See the [European Commission's CE guidance](https://europa.eu/youreurope/business/product-rules-compliance/general-product-compliance/ce-marking/index_en.htm).

For this Wi-Fi product, have the lab confirm the Radio Equipment Directive scope and current applicable requirements. Retain the exact firmware/configuration identity in the technical file and agree the intended operating conditions to test. See [Directive 2014/53/EU](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32014L0053), especially Article 17 and Annex V.

Espressif supplies [CE radio test guidance](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/development_stage/rf_test_certification/ce_certification.html) and [EspRFTestTool with dedicated RF test firmware](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32/development_stage/rf_test_guide/rf_test_guide.html). Agree those images/settings with the lab and archive them separately from the product candidate.
