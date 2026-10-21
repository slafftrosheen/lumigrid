> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-P0-BOOT-001 Boot Stack Hardening Report

## Crash evidence

The supplied panic ELF SHA256 prefix `0745118e3` matches `LumigridLedNode/.pio/build/test/firmware.elf` exactly: `0745118E3DEBAD320FBA692F09323CDB27D5C8F73FDB3EAA78116E28A039395E`.

The matching backtrace decodes to:

| Address | Decoded symbol and source |
|---|---|
| `0x4008be0d` | `xTaskIncrementTick`, FreeRTOS `tasks.c:3223` (panic PC `0x4008be10` is in the same function) |
| `0x4008afa3`, `0x4008aced`, `0x40086b82` | SysTick/low-level interrupt frames |
| `0x400ee4d9` | `RenderFrame::RenderFrame()` inlined into `LightingRuntime::LightingRuntime`, `RenderTypes.hpp:38`, `LightingRuntime.cpp:29` |
| `0x400da8b1` | `test_dmx_source_isolation`, `RuntimeHardeningTests.cpp:314` |
| `0x400e5e60` | `run_runtime_hardening_tests`, `RuntimeHardeningTests.cpp:7523` |
| `0x400d8c4b` | `LumigridRuntime::init`, `LumigridRuntime.cpp:526` |
| `0x400d3f6d` | `app_main`, `main.cpp:51` |

This confirms the panic occurred while the boot task was running Item 13.D and constructing the large runtime, with the faulting path entering `RenderFrame`. The fixture sizes are `DmxRouter` 46,760 bytes, `LightingRuntime` 29,768 bytes, `OutputController` 2,936 bytes, and `DeviceConfig` 1,676 bytes. Those four local objects alone require 81,140 bytes, against the unchanged 10,240-byte app task stack. This is the confirmed source-level root cause for the stack corruption; the interrupt PC reflects the later failure while handling a tick after the task stack had been exhausted.

## Changes

- Reworked Item 13.D to use checked, RAII-owned heap fixtures and explicit failure reporting. It retains source isolation, `[200, 50, 0]` and `[100, 150, 0]` contributions, HTP `[200, 150, 0]`, A timeout behavior, B continuing as `[100, 150, 0]`, and source/packet counter checks. The HTP/LTP algorithm was not changed.
- Converted 261 large hardening-suite fixture declarations to checked heap ownership, including output/runtime/router/frame/config/scene/sequence fixtures, and changed 18 `make_unique` allocations to checked `nothrow` allocations. The 16 KiB serialization buffers now use checked heap allocations too.
- The diagnostic test image applies early output safety and runs the suite in a separate 16 KiB task. Its startup does not initialize the production runtime. Suite-owned `OutputController` fixtures use a test-only in-memory mode; PCA output enable is mocked so `/OE` remains physically HIGH. The production app task remains 10,240 bytes, and production startup no longer runs integration tests.
- Test assertions log and return failure, allowing RAII cleanup. The suite reports `PASS`, `FAIL`, or `NOT_RUN`; heap admission checks preserve 32 KiB of 8-bit heap, 16 KiB of internal free heap, and an 8 KiB largest internal block before and after each fixture allocation.
- Test `ConfigStore` and `AuthStore` objects use `lg_test` and `lg_auth_test`. Raw config test blobs use `lg_test`. The diagnostic image does not open the production config/auth stores or run the provisioned runtime startup.
- Added per-test heap and byte-based stack-watermark instrumentation, an ELF/source `.su` audit generator, and the PCA electrical bench checklist at [LG-P0-BOOT-001_PCA_BENCH_CHECKLIST.md](LG-P0-BOOT-001_PCA_BENCH_CHECKLIST.md).

## Stack audit

The current ESP32 diagnostic ELF measured the fixture sizes and the compiler emitted `.su` stack frames. See [LG-P0-BOOT-001_STACK_AUDIT.md](LG-P0-BOOT-001_STACK_AUDIT.md). Item 13.D's function frame is 2,000 bytes after the change; its large fixture state is heap-owned. The largest static test function frame is 8,368 bytes, below the diagnostic task's 16 KiB stack. This is compiler static-frame evidence, not a physical high-water measurement; the on-device runner logs high-water values for each test.

## Validation and remaining acceptance

- `check_invariants.py`: passed after the final reserve guard edit.
- Factory firmware build (`pio run -d LumigridLedNode -e factory`): passed.
- Dedicated `env:test` build (`pio run -d LumigridLedNode -e test`): passed after the final reserve guard edit. The suite was not flashed or executed on a controller in this session, so Item 13.D's runtime result and device stack watermarks remain unverified.
- The supplied panic ELF was matched and decoded as described above. The DMX integration result, five physical Ready boots, NVS preservation on hardware, and no-output activation acceptance remain open.
- PCA9685 electrical bring-up remains open. Keep PWM unavailable and `/OE` HIGH until detection succeeds. Use the bench checklist to capture VDD/ground, SDA/SCL idle voltage and pull-ups, GPIO21/22 routing, `0x55` address straps, and a START/address/ACK-or-NACK/STOP waveform. `ESP_FAIL` alone does not establish the electrical cause.

## Commit and files

Implementation commits: `9059e9d34f8a85ae9ac9932f8086ce951f86f868` (`fix: isolate runtime hardening tests from boot stack`) and `c49308e` (`fix: keep runtime test output mocks isolated`).

Changed files are listed in the task summary; the user's pre-existing `OutputController` edits are preserved and are not part of this task's commit.
