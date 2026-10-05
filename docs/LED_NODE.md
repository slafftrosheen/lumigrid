# LED Node Hardware & Capabilities

> **Status:** **IMPLEMENTED** (Unless explicitly marked as PLANNED, PARTIALLY IMPLEMENTED, or CONCEPTUAL)

The LumiGrid LED Node is the core hardware controller in the ecosystem.

## Hardware Output Contracts

The actual hardware/software contract for the LED Node is explicitly defined as **8 addressable LED lanes + 8 PWM channels**.

- **Addressable LED Lanes:** 8 lanes (numbered 0-7) driven by a custom parallel RMT driver:
  - Canonical maximum: **256 pixels per lane** (2,048 pixels total per node).
  - Supported protocols: WS2812B (RGB, 30 µs/pixel wire time) and SK6812 (RGBW, 40 µs/pixel wire time).
  - Parallel submission invariant: all active lanes are submitted to RMT hardware before any lane is waited on.
  - Zero heap allocation per frame.
- **PWM Channels:** 8 channels (numbered 0-7) using PCA9685 via standard-mode **100 kHz I2C**:
  - Operating frequency: ~1017 Hz (prescale 5).
  - Fast fail-safe: `/OE` line is asserted HIGH directly via GPIO (<1 µs) without I2C transaction on emergency stop.

> [!WARNING]
> Historical documentation previously described a 16-channel generalized PWM driver and 300 pixels per port. The node firmly asserts 8 lanes (max 256 px) + 8 PWM channels (100 kHz I2C) in its factory validation constraints.

## Safety & Emergency Stop Contracts

- **Software E-Stop:** Latched atomically; immediately disables output commits and sets PCA `/OE HIGH`.
- **PWM Cutoff:** Immediate (<1 µs via physical `/OE` assertion).
- **Addressable Blackout:** Protocol-bound to serial frame wire transmission (~7.68 ms for 256 pixels of WS2812 / ~10.24 ms for SK6812 RGBW). Sub-5ms addressable physical cutoff requires future hardware load-switch / power-gate.

## System Health & Output Engine Telemetry

The node tracks system health and physical timing comprehensively:
- Free heap & minimum free heap
- Wi-Fi RSSI & network routes
- NVS state & configuration validity
- Startup diagnostics (startup stage, failing stage)
- Output state (Healthy, Faulted, Armed, E-Stop) & commit failures
- **Output Engine Timing (`/api/v1/diagnostics/output`)**:
  - Target framerate (40.0 FPS / 25.0 ms period)
  - Committed framerate
  - Frame commit duration, RMT wire duration, PWM duration
  - Frame headroom (ms)
  - Frame deadline misses (rebased without catch-up burst cycles)
- DMX statistics (drops, unpatched)
- Watchdog fallback & sync packet loss

