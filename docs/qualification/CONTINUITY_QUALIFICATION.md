# Continuity qualification and evidence handoff

This is a project bench plan, not a conformity assessment or certificate. The accompanying script checks the completeness and identity of operator-supplied evidence; it does not independently evaluate measurements.

## Candidate identity

After applying the batch, completing application and firmware builds, and committing the reviewed source, record the new source commit, exact application-image SHA-256, PCB revision, serial number, wiring revision, supply, loads, antenna and enclosure. Do not reuse an older candidate's evidence as a pass for a modified image.

Build and verification results are revision-specific; see the dated change report for the candidate under test. The October 30 source batch did not include target builds or flashing. No software report establishes physical output behavior, and a previous image's build result does not qualify a modified image.

## Bench records

| Check ID | Record to retain |
| --- | --- |
| firmware_builds | Factory/diagnostic build logs, effective configurations, image hashes and flash layout checks |
| safe_boot_and_reset | Measured output-enable and LED-data behavior through cold start, resets and power interruption; identify instruments and capture files |
| all_16_outputs | Connector-by-connector mapping and behavior for A1–A8 and P1–P8 with supported loads |
| combined_outputs_and_network | Simultaneous output operation and network/API load; record selected effects, loads, timing captures, resets and memory minima |
| network_loss_and_reconnect | Leader loss, stale and conflicting reports, reconnection, content mismatch and group selection; retain both nodes' logs |
| sync_shutdown_and_recovery | On the actual PCB, repeat sync disable/enable, role/token changes and network rebind under simultaneous output load; retain timeout/retry behavior, worker counts, output captures, heap and stack minima. Demonstrate no replacement worker or dependency teardown before the old worker acknowledges exit. Lose the leader during cue/timeline playback in Auto mode: local leadership must wait for remote cue/scheduler release; rejected release must remain visible and retry before takeover. Repeat with a preferred replacement leader and a rebooted leader reusing scene/timeline generation values. A failed release must retain the previous source and must not acknowledge or apply the replacement packet. |
| ota_and_rollback | Valid update, interruption and rollback on the intended layout; preserve configuration and image identity |
| configuration_power_loss | Interrupted save/restore and recovery; do not erase the only backup during the test |
| thermal_endurance | Actual current and temperature observations with duration, ambient conditions and agreed limits; firmware does not invent temperature sensor data |
| installation_limits_review | Approved supply/voltage/load/wiring/enclosure/antenna constraints for the exact assembly |

Set test limits and durations with the responsible engineer/lab and include those decisions in the evidence. Software-estimated power is not a current or temperature measurement. A software blackout is not a physical disconnection of strip power.

For power-limit acceptance, compare actual driver commands and measured supply current with the shared model in [Configuration](../CONFIGURATION.md). Include mixed RGB/RGBW/PWM loads, channel/supply/global ceilings, soft start, and a supply derated to zero usable capacity. Confirm that a ceiling below addressable idle extinguishes active demand while retaining the idle estimate. Inject output and blackout failures: last-confirmed estimates must be marked incomplete until recovery. Unknown PWM ratings cannot establish compliance with a positive power budget.

## Running the completeness preflight

Keep measurement records outside the source tree so that adding evidence does not make the frozen source dirty. Copy `evidence-template.json`, fill the identity only from the exact tested build, and mark each result truthfully. The `lumigrid-qualification-v2` record binds every check to the full declared identity: source and firmware hashes, PCB revision, unit serial, wiring, supply, loads, enclosure and antenna. A check repeats the firmware hash and PCB revision and records `identity_sha256` for that complete setup. Changing any identity field invalidates results still bound to the old setup.

Run the preflight with the filled identity and the checks still marked `NOT_RUN` to obtain the `identity_sha256` in its report. Exit 2 is expected at this stage. Record that value with each test **when it is performed**, and archive the original record. This digest detects mixed records; it cannot prove that the operator declared the physical setup truthfully.

Each evidence entry is an object containing a relative `path` and the SHA-256 recorded when the capture was archived:

```json
{
  "path": "safe-boot/scope.csv",
  "sha256": "<64 lowercase hexadecimal characters from the archived capture>"
}
```

For example, calculate a capture's digest with Python:

```sh
python -c "import hashlib,pathlib,sys; print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())" /path/to/evidence/safe-boot/scope.csv
```

The checker compares those recorded digests with the files present now. Replacing a capture at the same path blocks completeness. Empty files, escaping paths, duplicate paths within one check, missing hashes and mismatched setup identity also block completeness. A shared capture may support several checks; the report lists those associations. Current hashes appear in incomplete reports for diagnosis, but do not replace archived hashes merely to clear a mismatch.

Version 1 records and plain path strings are rejected. Migrate from the original archived test records and capture hashes, preserving the tested setup and result. If that identity or evidence cannot be established, leave the check `NOT_RUN` and repeat it; do not automatically stamp old results with the current setup.

```sh
python scripts/qualification_preflight.py \
  --record /path/to/evidence/record.json \
  --firmware /path/to/tested/factory/firmware.bin \
  --source-root . \
  --output /path/to/evidence/preflight-001.json
```

Exit 0 means the specified bench-evidence records are complete, **not** that the product is certified. Exit 2 means blocked/incomplete evidence. Exit 1 means the command itself could not complete. Existing output files are never overwritten. No network request, firmware flash, or lighting command is issued.

## Remaining engineering work before release

The source includes cooperative NodeSync shutdown ownership, runtime teardown/callback integration and acknowledged remote release before automatic leader takeover. Host behavior and interleavings have been tested; the dated change report records which target and web builds were actually completed. Whole-runtime on-board acceptance remains required. Oversized sync datagrams are rejected in host tests. Full UDP semantic/security validation, actual multi-node timing, hardware recovery, output coexistence and RF/EMC tests are not established by these host tests. Do not treat protocol compatibility, matching checksums or a green dashboard as a physical measurement.

## Regulatory boundary and sources

The applicable conformity assessment, technical documentation, declaration and marking must be addressed for the intended product configuration. The manufacturer remains responsible; this internal tooling cannot issue a CE certificate. Confirm current applicability, including radio/cybersecurity requirements, with the lab rather than relying on an older checklist.

Official references checked on 2026-10-30:
- European Commission, Radio Equipment Directive: https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en
- European Commission, manufacturers and CE marking: https://single-market-economy.ec.europa.eu/single-market/goods/ce-marking/manufacturers_en
- Directive 2014/53/EU, including Article 17 (operating configurations): https://eur-lex.europa.eu/eli/dir/2014/53/oj/eng
