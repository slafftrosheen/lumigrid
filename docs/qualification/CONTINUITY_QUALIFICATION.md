# Continuity qualification and evidence handoff

This is a project bench plan, not a conformity assessment or certificate. The accompanying script checks the completeness and identity of operator-supplied evidence; it does not independently evaluate measurements.

## Candidate identity

After applying the batch, completing application and firmware builds, and committing the reviewed source, record the new source commit, exact application-image SHA-256, PCB revision, serial number, wiring revision, supply, loads, antenna and enclosure. Do not reuse an older candidate's evidence as a pass for a modified image.

The full ESP32 factory/test images and browser application have **not** been built or flashed as part of this patch package. Host regression results are recorded separately in its delivery report. No hardware pin assignment, radio setting, signed-update policy, effect ID or output count is changed by this batch.

## Bench records

| Check ID | Record to retain |
| --- | --- |
| firmware_builds | Factory/diagnostic build logs, effective configurations, image hashes and flash layout checks |
| safe_boot_and_reset | Measured output-enable and LED-data behavior through cold start, resets and power interruption; identify instruments and capture files |
| all_16_outputs | Connector-by-connector mapping and behavior for A1–A8 and P1–P8 with supported loads |
| combined_outputs_and_network | Simultaneous output operation and network/API load; record selected effects, loads, timing captures, resets and memory minima |
| network_loss_and_reconnect | Leader loss, stale and conflicting reports, reconnection, content mismatch and group selection; retain both nodes' logs |
| sync_shutdown_and_recovery | On the actual PCB, repeat sync disable/enable, role/token changes and network rebind under simultaneous output load; retain timeout/retry behavior, worker counts, output captures, heap and stack minima. Demonstrate no replacement worker or dependency teardown before the old worker acknowledges exit. |
| ota_and_rollback | Valid update, interruption and rollback on the intended layout; preserve configuration and image identity |
| configuration_power_loss | Interrupted save/restore and recovery; do not erase the only backup during the test |
| thermal_endurance | Actual current and temperature observations with duration, ambient conditions and agreed limits; firmware does not invent temperature sensor data |
| installation_limits_review | Approved supply/voltage/load/wiring/enclosure/antenna constraints for the exact assembly |

Set test limits and durations with the responsible engineer/lab and include those decisions in the evidence. Software-estimated power is not a current or temperature measurement. A software blackout is not a physical disconnection of strip power.

## Running the completeness preflight

Keep measurement records outside the source tree so that adding evidence does not make the frozen source dirty. Copy `evidence-template.json`, fill the identity only from the exact tested build, and mark each result truthfully. Each check repeats the tested firmware hash and PCB revision to catch stale records. Evidence paths are relative to the evidence directory. Empty files, escaping paths, duplicates and mismatched identity block completeness.

```sh
python scripts/qualification_preflight.py \
  --record /path/to/evidence/record.json \
  --firmware /path/to/tested/factory/firmware.bin \
  --source-root . \
  --output /path/to/evidence/preflight-001.json
```

Exit 0 means the specified bench-evidence records are complete, **not** that the product is certified. Exit 2 means blocked/incomplete evidence. Exit 1 means the command itself could not complete. Existing output files are never overwritten. No network request, firmware flash, or lighting command is issued.

## Remaining engineering work before release

The source batch includes a cooperative NodeSync shutdown ownership fix and exact runtime teardown/callback integration edits. Host behavior and interleavings have been tested; full target compilation, whole-runtime integration and on-board acceptance are still required. Oversized sync datagrams are rejected in host tests. Full UDP semantic/security validation, actual multi-node timing, hardware recovery, output coexistence and RF/EMC tests are not established by these host tests. Do not treat protocol compatibility, matching checksums or a green dashboard as a physical measurement.

## Regulatory boundary and sources

The applicable conformity assessment, technical documentation, declaration and marking must be addressed for the intended product configuration. The manufacturer remains responsible; this internal tooling cannot issue a CE certificate. Confirm current applicability, including radio/cybersecurity requirements, with the lab rather than relying on an older checklist.

Official references checked on 2026-10-30:
- European Commission, Radio Equipment Directive: https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en
- European Commission, manufacturers and CE marking: https://single-market-economy.ec.europa.eu/single-market/goods/ce-marking/manufacturers_en
- Directive 2014/53/EU, including Article 17 (operating configurations): https://eur-lex.europa.eu/eli/dir/2014/53/oj/eng
