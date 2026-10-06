# PCA9685 bring-up contract

SDA GPIO21, SCL GPIO22, /OE GPIO25; synchronous ESP-IDF master transactions (`trans_queue_depth = 0`) at 100 kHz. The PCB address is **0x55** (hardware fact): A0/A2/A4 tied HIGH, A1/A3/A5 tied LOW, so 0x40 + 0x01 + 0x04 + 0x10 = 0x55.

Initialization binds directly to 0x55 with no `i2c_master_probe()` gate, no 0x40/0x70 probe, and no bus scan or address auto-discovery. Presence is decided by real PCA transactions: a MODE1 read, and if that fails a direct MODE1 write (`SLEEP | AI`). Either succeeding means the PCA is present. Then the native sequence runs: MODE1=SLEEP|AI, PRE_SCALE=5, MODE2=OUTDRV, MODE1=AI, wait 2 ms, MODE1=AI|RESTART, read back, zero all channels. The address is reported by PcaDiagnostics.address.

On failure, /OE stays HIGH and PWM remains unavailable while addressable outputs continue. /OE is HIGH before init and on failure, a known-zero frame is written before enable, and OutputController exclusively owns normal /OE enable. Successful initialization makes eight PWM channels available.

Safety hardware: software/runtime E-stop, PCA /OE cutoff, no dedicated physical E-stop input GPIO, no addressable power gate.

Reference: [NXP PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf). If direct transactions still fail, the next A/B step is swapping only the transport to legacy `driver/i2c.h` (same pins, 100 kHz, 0x55).

> The section below is historical evidence from a build that gated on `i2c_master_probe()` (0x40/0x70/scan). Its conclusions about an unknown address are superseded; the log is preserved unchanged.

## Physical result — 2026-10-06

The application-only upload wrote 1,314,064 bytes at 0x10000 and esptool verified its hash. Following manual reset, the complete boot reported:

```text
I (1813) pca9685: I2C bus ready at 100 kHz; external SDA/SCL pull-ups remain recommended
I (1821) pca9685: PCA9685: default address 0x40 NACK
I (1827) pca9685: PCA9685: all-call address 0x70 NACK
W (1832) pca9685: PCA9685: scanning individual address once
W (1854) pca9685: I2C scan: no individual devices found; PWM subsystem unavailable
W (1862) output: Failed to init PCA9685 PWM driver (ESP_ERR_NOT_FOUND). Addressable LEDs will still work, but PWM configurations will be rejected.
I (3540) lumigrid: BOOT_STACK minimumFree=7348 bytes; READY free8=25876 largest8=20480 state=Ready
```

No ACK, bus timeout or other probe error was reported in the bounded 0x08–0x77 scan. No resolved PCB address can be claimed. This demonstrates an unresponsive bus/device under the current GPIO21/22 configuration; it does not prove the physical routing, supply or electrical signal quality. Stop driver changes and verify PCA population/identity, VDD and ground, SDA/SCL routing and continuity, and external pull-ups. /OE remains HIGH by the initialization failure path; its physical voltage and loaded addressable output are not established by this log.

Raw capture: [PCA9685-bringup-boot.log](PCA9685-bringup-boot.log). Earlier short boot fragments/reset lines are retained before the complete boot. LG-038.3/LG-038.4 evidence files were not rewritten.
