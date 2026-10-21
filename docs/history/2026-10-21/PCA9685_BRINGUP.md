> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# PCA9685 bring-up contract

SDA GPIO21, SCL GPIO22, /OE GPIO25; **ESP-IDF legacy synchronous I2C driver** (`driver/i2c.h`) at **100 kHz**. The PCB address is **0x55** (hardware fact): A0/A2/A4 HIGH, A1/A3/A5 LOW, so 0x40 + 0x01 + 0x04 + 0x10 = 0x55.

The transport changed because the new `i2c_master` path under ESP-IDF 5.2.1 produced `ESP_ERR_INVALID_STATE` / `ESP_ERR_TIMEOUT` on this controller. PlatformIO remains `espressif32@6.7.0`, ESP-IDF 5.2.1. Only the PCA I2C transport changes.

Initialization installs the legacy master on I2C_NUM_0, with internal pull-ups and no asynchronous callbacks, device handles, transaction queue, probe or scanner. A single MODE1 read at 0x55 decides presence; failure returns immediately with /OE HIGH. Every register read and write uses mutex-serialized synchronous helpers with a 100 ms timeout.

The existing sequence remains: MODE1=SLEEP|AI, PRE_SCALE=5, MODE2=OUTDRV, MODE1=AI, wait 2 ms, MODE1=AI|RESTART, read MODE1/MODE2/PRE_SCALE, zero all channels. The address is reported by PcaDiagnostics.address.

## Failed new-driver evidence

The user supplied these observed failures; existing raw captures remain unchanged:

```text
i2c_master_transmit_receive -> ESP_ERR_INVALID_STATE
i2c_master_transmit         -> ESP_ERR_TIMEOUT
i2c_master_bus_rm_device    -> Wrong I2C status
```

## Legacy transport validation � 2026-10-07

`python LumigridLedNode/scripts/check_invariants.py` passed. `pio run -d LumigridLedNode -e factory` passed with espressif32 6.7.0 / ESP-IDF 5.2.1, including linked boot-stack and partition-budget checks. Firmware binary: 1,313,856 bytes, 54.2% of the factory slot. Linked-symbol inspection found the two legacy transfer helpers and no `i2c_acquire_bus_handle`, `i2c_new_master_bus` or `i2c_master_transmit` implementation, avoiding IDF's legacy/new-driver startup conflict.

No ESP32 USB serial port was present (only Bluetooth COM8/COM9), so no flash or physical boot/P1/P8 test was performed. The regression remains physically unverified until the expected detection/readback boot and existing Factory PWM P1/P8 checks succeed.

On failure, /OE stays HIGH and PWM remains unavailable while addressable outputs continue. /OE is HIGH before init and on failure, a known-zero frame is written before enable, and OutputController exclusively owns normal /OE enable. Successful initialization makes eight PWM channels available.

Safety hardware: software/runtime E-stop, PCA /OE cutoff, no dedicated physical E-stop input GPIO, no addressable power gate.

Reference: [NXP PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf).

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
