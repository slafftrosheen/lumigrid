# Firmware release candidate 1.0.0-rc1

Date: 2026-10-23. Source baseline: `83d98dd166853871e2ff4c303b57feb9323444bd`.

This candidate repairs identified output, boot, network and evidence-storage faults before board qualification. It is a software candidate for bench testing. Physical stability, RF/EMC, electrical safety and thermal acceptance have not been measured in this change. Record the final Git commit and the exact artifact hashes from the build manifest when testing; the version string alone is insufficient to identify a binary.

## Changes relevant to board qualification

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

### Checks completed on 2026-10-23

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

The actual runtime/build inputs were fingerprinted during the frozen final build and matched after it. The raw certification record is 8,000 bytes; the host fixtures store an initial session in 833 bytes and a representative completed session in 2,609 bytes. These sizes describe those fixtures, not guaranteed capacity for every note/configuration combination. No physical measurements or on-device diagnostic execution are included in these results.

## Bench acceptance before the lab handoff

Use the intended PCB revision, enclosure, antenna, supplies, wiring and supported maximum loads. The sequence below is an engineering acceptance proposal; the lab must confirm the product's applicable conformity scope and operating modes.

| Gate | Exercise | Evidence to retain |
| --- | --- | --- |
| Image identity | Read board identity, firmware version and application ELF hash; match them to the archived candidate | Git SHA, binary/ELF hashes, board serial/revision, flash size and effective configuration |
| Boot and reset | Repeat cold starts and software resets; observe `/OE` and all eight addressable data pins from power application through readiness | Serial log and scope captures showing safe boot behavior; actual pull-up/level-shifter behavior before firmware begins |
| PWM channels | Exercise P1–P8 individually through zero, intermediate and intended full duty; inhibit with `/OE` | Scope duty/frequency, mapping and current observations; no unintended active channels |
| Addressable channels | Exercise A1–A8, correct color order, maximum intended pixel count and combined updates | Visible mapping plus logic-analyzer timing/error evidence under concurrent Wi-Fi traffic |
| Failure recovery | Introduce controlled I2C faults, network loss/reconnect and logical stop during output changes | Fault diagnostics, inhibited PWM, truthful blackout status, AP availability and successful bounded recovery |
| State persistence | Save intended configuration, network settings and acceptance results; reboot and read back; test a controlled power interruption during a write | Previous or new complete valid record, explicit capacity/write errors, preserved credentials/configuration and exported reports |
| OTA and rollback | Perform compatible application updates and controlled rejected/interrupted updates | Correct running hash, healthy dwell/verification, recovery path and preserved configuration |
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
