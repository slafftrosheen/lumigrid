> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LumiGrid production flash layout

LumiGrid production hardware uses 8 MB flash.

Partition Layout v2 requires one wired migration from Layout v1.
It cannot be installed safely using normal OTA.

## Layout v2

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | `0x4000` |
| OTA selection metadata | `0xd000` | `0x2000` |
| PHY | `0xf000` | `0x1000` |
| factory | `0x10000` | `0x250000` |
| ota_0 | `0x260000` | `0x250000` |
| ota_1 | `0x4b0000` | `0x250000` |
| littlefs reserve | `0x700000` | `0x100000` |

The final address is exactly `0x800000`. Each app slot holds 2,424,832 bytes.
The LittleFS-named partition retains the requested `spiffs` subtype; IDF can print
a naming warning. It is reserved, not mounted or used for production data. The
dashboard remains embedded and Scenes/configuration remain in NVS. No filesystem
content migration is needed.

## Build and wired migration on this workstation

Export `.lgbak` and record node ID/version before migrating a provisioned unit.
The current controller was never provisioned; its raw NVS was backed up privately
under `.pio/lg0384-private` instead. A raw NVS backup can contain credentials and
must never be committed. A normal `.lgbak` intentionally excludes authentication
and network secrets.

```powershell
Set-Location C:\Users\slavd\lumigrid\LumigridLedNode
$env:PLATFORMIO_BUILD_DIR = "$PWD\.pio\build-lg0384"
& C:\Users\slavd\AppData\Local\Programs\Python\Python312\Scripts\pio.exe run -e factory
& C:\Users\slavd\AppData\Local\Programs\Python\Python312\python.exe scripts\migrate_flash_v2.py --port COM3
```

The migration script validates the freshly built image headers and partition
binary, detects actual physical flash, saves NVS, writes the four artifacts,
then compares NVS byte-for-byte **before the first application boot**. It refuses
to overwrite an earlier migration backup. Archive those files privately before
an intentional repeat migration. It does not issue a full-chip erase.

The underlying write command is:

```powershell
& C:\Users\slavd\AppData\Local\Programs\Python\Python312\python.exe `
  C:\Users\slavd\.platformio\packages\tool-esptoolpy\esptool.py `
  --chip esp32 --port COM3 --baud 115200 --after no_reset write_flash `
  --flash_mode dio --flash_freq 40m --flash_size 8MB `
  0x1000 .pio\build-lg0384\factory\bootloader.bin `
  0x8000 .pio\build-lg0384\factory\partitions.bin `
  0xd000 .pio\build-lg0384\factory\ota_data_initial.bin `
  0x10000 .pio\build-lg0384\factory\firmware.bin
```

Do not preserve v1 OTA selection metadata: both OTA application addresses move.
Writing the fresh 8,192-byte initializer at `0xd000` resets selection; NVS at
`0x9000` is not erased. Full factory reset is a separate acceptance test, after
backup and migration-preservation checks.

## Build and runtime contracts

Factory and test targets explicitly declare 8 MB flash and use the production v2
table. The build validator enforces exact offsets/sizes, alignment, non-overlap,
NVS preservation, factory plus exactly two OTA slots, and the 8 MB end address.
After every image build, the storage report measures actual `firmware.bin` bytes:
below 75% is healthy, 75–85% warns, above 85% fails the build.

`/api/v1/info`, `/api/v1/health`, backup source metadata, OTA status and
Installer-level `/api/v1/diagnostics/storage` expose the layout/capacity. Physical
flash is read with pinned IDF's `esp_flash_get_physical_size`; configured flash
uses `esp_flash_get_size`. Runtime checks exact partition addresses and sizes.
Capacity/layout mismatch forces safe mode and outputs off, where startup can
reach recovery. A physically undersized module can fail in the bootloader before
application recovery is possible; it is not supported production hardware.

OTA rejects declared bodies above the next slot capacity with HTTP 413 before
`esp_ota_begin`. It reads and validates the image header, chip revision, 8 MB
header, project and version descriptor before starting an OTA write. Final IDF
image validation still applies; a valid descriptor alone does not authenticate
an unsigned image. Progress is based on declared image length.

Candidates require a continuous ten-second healthy local runtime interval.
The existing recovery worker polls the gate; no new permanent task is created.
API alone cannot confirm. Required planned services, valid configuration, Ready
state and non-critical memory must hold. Internet, STA, DNS and SNTP are not
requirements. Health loss restarts the dwell; fatal/critical conditions roll back,
and sixty seconds without successful verification times out. State schema 2
persists candidate boot/reset reason, dwell start/deadline and failure reason;
schema 1 identity/reason data migrates. Boot-relative timestamps restart on a
candidate boot, and a pending candidate reset is governed by IDF rollback.
Unattributed safe-mode recovery is conservatively not confirmed; firmware does
not invent evidence that it predates the candidate.

## Serial-only bench access

The PC stays on its current Wi-Fi. COM3 at 115200 baud provides bounded read-only
`diagnostics`, `memory`, and `storage` commands, plus a settled snapshot ten
seconds after startup. These use the existing recovery worker and UART FIFO;
no additional task or UART receive allocation is introduced. They expose no
credentials and cannot provision, reset, modify configuration or authorize OTA.

```powershell
& C:\Users\slavd\AppData\Local\Programs\Python\Python312\python.exe `
  scripts\capture_serial.py --port COM3 --seconds 60 --command diagnostics
```

The capture reconnects after USB interruption and flushes each chunk to the
acceptance log. Serial does not replace the authenticated HTTP OTA workflow or
the phone/browser setup acceptance. Use a separate device for AP access.
