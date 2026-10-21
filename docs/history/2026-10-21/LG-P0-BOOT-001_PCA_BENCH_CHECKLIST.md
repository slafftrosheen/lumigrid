> Historical document preserved on 2026-10-21. Use the [current documentation](../../README.md) for current behavior. Results below apply only to their recorded revision.

# LG-P0-BOOT-001 PCA9685 Bench Checklist

Keep PWM unavailable and PCA `/OE` HIGH until the controller detects the PCA9685 successfully. Do not scan alternate addresses or change the legacy I²C driver as part of this check.

- [ ] Confirm PCA9685 VDD and ground at the device pins.
- [ ] Measure SDA and SCL idle voltage; record both values.
- [ ] Confirm the external SDA/SCL pull-ups are populated and connected to the intended rail.
- [ ] Verify board routing from SDA to GPIO21 and SCL to GPIO22.
- [ ] Verify address strap states decode to I²C address `0x55`.
- [ ] Capture a logic-analyzer or oscilloscope trace for the MODE1 read; record START, address, ACK/NACK, and STOP.
- [ ] If the bus is stuck or the transaction fails, capture recovery diagnostics and the post-recovery bus state.
- [ ] Keep `/OE` HIGH and PWM unavailable if the device still does not ACK.

`ESP_FAIL` alone does not identify the electrical cause. The bus waveform and pin measurements are the evidence for that diagnosis.
