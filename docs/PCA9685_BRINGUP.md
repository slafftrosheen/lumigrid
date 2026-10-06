# PCA9685 bring-up contract

SDA GPIO21, SCL GPIO22, /OE GPIO25; synchronous ESP-IDF master transactions at 100 kHz. The standard unstrapped address is 0x40. All Call is 0x70 and is enabled at chip power-up, but earlier configuration can disable it. Neither is a confirmed PCB strap address. Repository hardware sources do not establish the PCB strap address.

Factory/development initialization first probes 0x40, then 0x70. If the default is absent, one bounded 0x08–0x77 scan logs responses and selects only a single individual device in the PCA range. All Call is excluded, multiple devices are ambiguous, and bus/driver errors remain distinct from NACK. The existing register initialization/readback then verifies the candidate. The resolved address is reported by PcaDiagnostics.address; zero means unresolved. Nothing is persisted to NVS.

On detection or initialization failure, /OE stays HIGH and PWM remains unavailable while addressable outputs continue. Successful initialization makes eight PWM channels available through the existing runtime capability plumbing.

Safety hardware: software/runtime E-stop, PCA /OE cutoff, no dedicated physical E-stop input GPIO, no addressable power gate.

Reference: [NXP PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf). Historical 0x55 probe logs are evidence of the previous assumption, not proof of PCB strapping. They are preserved unchanged.

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
