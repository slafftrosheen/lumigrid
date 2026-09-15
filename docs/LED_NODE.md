# LED Node Hardware & Capabilities

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LumiGrid LED Node is the core hardware controller in the ecosystem.

## Hardware Output Contracts

The actual hardware/software contract for the LED Node is explicitly defined as **8 addressable LED lanes + 8 PWM channels**.

- **Addressable LED Lanes:** 8 lanes (numbered 0-7) using RMT peripheral.
- **PWM Channels:** 8 channels (numbered 0-7) using PCA9685 via I2C.

> [!WARNING]
> Historical documentation previously described a 16-channel generalized PWM driver. The node firmly asserts 8 lanes + 8 PWM channels in its factory validation constraints.

## System Health

The node tracks system health comprehensively, making a distinction between operational and factory/diagnostic states. Health telemetry includes:
- Free heap & minimum free heap
- Wi-Fi RSSI
- NVS state & configuration validity
- Startup diagnostics (startup stage, failing stage)
- Output state (Healthy, Faulted, Armed) & output faults
- DMX statistics (drops)
- Watchdog fallback & packet loss
