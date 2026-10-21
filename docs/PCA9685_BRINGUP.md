# PCA9685 bring-up

Current transport is legacy synchronous ESP-IDF `driver/i2c.h`, `I2C_NUM_0`, using `i2c_master_write_to_device` and `i2c_master_write_read_device`. New-driver I2C APIs are rejected by the invariant policy.

SDA is GPIO21, SCL GPIO22, address `0x55` (A0/A2/A4 high), active-low /OE GPIO25. LumiGrid exposes eight independent PWM channels P1–P8.

1. Keep /OE inhibited and loads safely limited while checking board power/wiring.
2. Build current `factory` and verify expected pin/address constants.
3. Inspect startup and `/api/v1/diagnostics/output` for initialization/errors/state.
4. Configure one PWM channel with its load rating and test bounded stepped duty.
5. Measure duty and inhibit externally, then repeat across P1–P8 with addressable/Wi-Fi activity.
6. Record reboot, communication failure and recovery behavior.

I2C acknowledgement proves bus response, not correct duty or load behavior. A register write, host test or old boot log is not current physical acceptance.

Sources: `LumigridLedNode/components/output/Pca9685Driver.cpp`, `OutputController.cpp`, `components/board/BoardOutputSafety.cpp`, `include/lg_board_pins.hpp`, `scripts/check_invariants.py`.
