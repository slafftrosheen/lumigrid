# Flash layout and updates

Layout v2 targets 8 MB flash. `partitions.factory.csv` defines:

| Partition | Offset | Size |
| --- | --- | --- |
| nvs | 0x9000 | 0x4000 |
| otadata | 0xd000 | 0x2000 |
| phy_init | 0xf000 | 0x1000 |
| factory | 0x10000 | 0x250000 |
| ota_0 | 0x260000 | 0x250000 |
| ota_1 | 0x4b0000 | 0x250000 |
| littlefs | 0x700000 | 0x100000 |

The `littlefs` label has the CSV `spiffs` subtype; the name alone does not establish a mounted LittleFS content store. ConfigStore/NVS owns configuration persistence.

## Build and wired migration

Build `factory`. Pre/post-build hooks generate embedded assets and check invariants, partition layout, ELF stack and image budget. `verify_partition_layout.py` checks layout/storage capacity.

An older partition layout requires wired migration. `scripts/migrate_flash_v2.py` verifies artifact headers/table/physical flash, backs up NVS privately, writes bootloader/table/OTA initializer/application, then verifies NVS preservation. Its default build directory is historical; pass the actual fresh build and port from `LumigridLedNode/`:

```powershell
python scripts/migrate_flash_v2.py --port COM_PORT --build-dir .pio/build/factory
```

Review the script and retain a backup first. Whole-chip erase is not a preservation migration.

## OTA

Admin-only POST `/api/v1/ota` accepts a raw application image. It checks maintenance conflicts, memory, image size, storage health, chip/image descriptor and project identity `lumigrid_led_node`, writes the next slot and persists a verification journal before reboot.

Boot policy requires 10 seconds of healthy dwell inside a 60-second window. Fatal startup, critical memory or timeout can request rollback. Inspect `/api/v1/ota/status`, recovery status and health after reconnecting. Upload completion is not boot acceptance.

The `signedOta` capability follows ESP-IDF's effective `CONFIG_SECURE_SIGNED_ON_UPDATE` setting. The checked-in defaults leave it disabled, so the default build reports `signedOta=false`. Image validation and rollback do not establish cryptographic signature enforcement. A signed production provisioning/update process remains separate work.

## Traceable candidate artifacts

The firmware workflow builds both `factory` and `test`, then packages their binaries, ELF files, effective configurations, source/tool hashes and the checked partition table with `scripts/firmware_release_manifest.py`. The factory image is the production candidate; the test image contains diagnostic/development flags. The manifest explicitly records hardware validation as `NOT_RUN` and compliance assessment as `NOT_PERFORMED`.

The merged image declares **8 MB** flash. It is a fresh-install image at offset zero: its padding covers NVS and OTA metadata and can erase saved credentials/configuration if written to an existing device. Use the preserving migration above for an older layout, or application-only OTA on a compatible layout. Do not treat the merged image as a preserving update.

See [firmware candidate and bench gates](FIRMWARE_RELEASE_CANDIDATE.md) before handing an image to a test lab.

The web flasher provides build guidance: no verified `latest_builds/manifest.json` release bundle is supplied by this checkout.

Sources: `LumigridLedNode/platformio.ini`, `partitions.factory.csv`, `scripts/pre_build.py`, `scripts/migrate_flash_v2.py`, `components/ota/`, `components/factory/FactoryApi.cpp`.
