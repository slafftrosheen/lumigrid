# LumiGrid LED Node - Hardware Guide

**Version:** 1.0.0  
**Target Platform:** ESP32-WROOM-32U  
**Last Updated:** 2026-01-08

---

## Table of Contents

1. [Board Overview](#board-overview)
2. [GPIO Pinout](#gpio-pinout)
3. [Wiring Diagrams](#wiring-diagrams)
4. [Power Requirements](#power-requirements)
5. [Addressable LED Channels](#addressable-led-channels)
6. [PWM Channels](#pwm-channels)
7. [Safety Guidelines](#safety-guidelines)
8. [Component Specifications](#component-specifications)
9. [Testing and Validation](#testing-and-validation)
10. [Troubleshooting](#troubleshooting)

---

## Board Overview

The LumiGrid LED Node is built around the ESP32-WROOM-32U microcontroller and supports:

- **8× Addressable LED Channels** - WS2812B/SK6812 via RMT peripheral
- **8× PWM Channels** - PCA9685 I²C PWM controller @ 1kHz
- **Wi-Fi Connectivity** - 2.4GHz 802.11 b/g/n
- **Web Interface** - Embedded responsive UI
- **REST/MQTT APIs** - Network control and telemetry

### Key Features

- **Addressable LED Support:**
  - WS2812B (RGB)
  - SK6812 (RGBW)
  - Up to 1200 pixels per channel
  - Individual pixel control
  - Effects engine with blending

- **PWM LED Support:**
  - 12-bit resolution (0-4095)
  - ~1kHz frequency (configurable)
  - Linear or logarithmic curves
  - Soft-start capability

- **Signal Buffering:**
  - SN74HCT245 level shifters for ALED channels
  - Clean 5V logic output
  - Protection against voltage spikes

- **Current Handling:**
  - Up to 10A @ 5V total
  - Configurable per-channel limits
  - Global power budget management

---

## GPIO Pinout

### Complete Pin Assignment

| Function | GPIO | Connected To | Direction | Notes |
|----------|------|--------------|-----------|-------|
| **Addressable LED Channels** | | | | |
| ALEDch1 | GPIO16 | SN74HCT245 → Strip 1 | Output | RMT Channel 0 |
| ALEDch2 | GPIO4  | SN74HCT245 → Strip 2 | Output | RMT Channel 1 |
| ALEDch3 | GPIO17 | SN74HCT245 → Strip 3 | Output | RMT Channel 2 |
| ALEDch4 | GPIO18 | SN74HCT245 → Strip 4 | Output | RMT Channel 3 |
| ALEDch5 | GPIO19 | SN74HCT245 → Strip 5 | Output | RMT Channel 4 |
| ALEDch6 | GPIO23 | SN74HCT245 → Strip 6 | Output | RMT Channel 5 |
| ALEDch7 | GPIO26 | SN74HCT245 → Strip 7 | Output | RMT Channel 6 |
| ALEDch8 | GPIO27 | SN74HCT245 → Strip 8 | Output | RMT Channel 7 |
| **I²C Bus (PCA9685)** | | | | |
| SDA | GPIO21 | PCA9685 SDA | Bidirectional | 400 kHz, 4.7kΩ pullup |
| SCL | GPIO22 | PCA9685 SCL | Output | 400 kHz, 4.7kΩ pullup |
| **PWM Control** | | | | |
| PCA9685 OE# | GPIO25 | PCA9685 Output Enable | Output | Active-low, HIGH=disabled |
| PCA9685 ch0 | — | MOSFET Q1 → LEDch1 | — | 1kHz PWM |
| PCA9685 ch1 | — | MOSFET Q2 → LEDch2 | — | 1kHz PWM |
| PCA9685 ch2 | — | MOSFET Q3 → LEDch3 | — | 1kHz PWM |
| PCA9685 ch3 | — | MOSFET Q4 → LEDch4 | — | 1kHz PWM |
| PCA9685 ch4 | — | MOSFET Q5 → LEDch5 | — | 1kHz PWM |
| PCA9685 ch5 | — | MOSFET Q6 → LEDch6 | — | 1kHz PWM |
| PCA9685 ch6 | — | MOSFET Q7 → LEDch7 | — | 1kHz PWM |
| PCA9685 ch7 | — | MOSFET Q8 → LEDch8 | — | 1kHz PWM |
| **Serial Console** | | | | |
| UART TX | GPIO1 | USB-Serial TX | Output | Console output |
| UART RX | GPIO3 | USB-Serial RX | Input | Console input |
| **Boot Control** | | | | |
| BOOT | GPIO0 | Boot button | Input | LOW=flash mode |

### Reserved/Unused Pins

- **GPIO2** - Bootstrap/LED (avoid using)
- **GPIO5** - Bootstrap (avoid using)
- **GPIO12** - Bootstrap MTDI (avoid using)
- **GPIO15** - Bootstrap (avoid using)
- **PCA9685 ch8-15** - Reserved for expansion header (not exposed in firmware)

### Pin Quick Reference Card

```
┌──────────────────────────────────────┐
│   LumiGrid LED Node GPIO Map         │
├──────────────────────────────────────┤
│ ALED Channels:                       │
│   ch1-8: 16,4,17,18,19,23,26,27      │
│                                      │
│ I²C Bus:                             │
│   SDA: GPIO21                        │
│   SCL: GPIO22                        │
│                                      │
│ PWM Control:                         │
│   OE: GPIO25 (active-low)            │
│                                      │
│ Serial:                              │
│   TX: GPIO1                          │
│   RX: GPIO3                          │
│                                      │
│ Boot:                                │
│   BOOT: GPIO0                        │
└──────────────────────────────────────┘
```

---

## Wiring Diagrams

### Addressable LED Channel Wiring

```
ESP32-WROOM-32U                 SN74HCT245              WS2812B Strip
┌──────────┐                   ┌──────────┐            ┌─────────────┐
│          │                   │          │            │             │
│  GPIO16  ├──────────────────►│ A1   Y1  ├───────────►│ DI (Data)   │
│  (ALEDch1)                   │          │   5V logic │             │
│          │                   │          │            │ +5V ────────┤ 5V Rail
│          │                   │ VCC  GND │            │ GND ────────┤ Ground
│      GND ├───────────────────┤          │            │             │
│          │    ┌──────────────┤ DIR  OE# │            └─────────────┘
│          │    │  (HIGH)      └──────────┘
└──────────┘    │
                └─── +3.3V (Direction: A→Y)

Notes:
- SN74HCT245 converts 3.3V ESP32 logic to 5V LED logic
- DIR pin tied HIGH for A→Y direction
- OE# pin tied LOW to enable outputs
- Add 330Ω-470Ω resistor in series with DI for protection
- Add 1000µF capacitor across 5V/GND near LED strip
```

### PWM LED Channel Wiring

```
PCA9685                         N-Channel MOSFET        LED Strip
┌──────────┐                   ┌──────────┐            ┌─────────────┐
│          │                   │          │            │             │
│  PWM0    ├──────────────────►│ Gate     │            │ +5V/12V/24V │
│  (ch1)   │   1kΩ             │          │            │     │       │
│          │                   │ Drain    ├────────────┤ LED-  LED+  │
│          │    ┌──────────────┤          │            │     │       │
│      GND ├────┴──────────────┤ Source   │            └─────┼───────┘
│          │    Common Ground  └──────────┘                  │
└──────────┘                                                 │
     │                                                       │
     └───────────────────────────────────────────────────────┘
                         Common Ground

Recommended MOSFETs:
- IRLZ44N (55V, 47A, logic-level)
- IRF540N (100V, 33A, standard)
- IRL540N (100V, 36A, logic-level)

Notes:
- Add 1kΩ resistor between PWM output and MOSFET gate
- Add 10kΩ pulldown from gate to ground
- Use heatsink for currents >2A
- Add flyback diode (1N4007) for inductive loads
```

### I²C Bus Wiring

```
ESP32-WROOM-32U              PCA9685 PWM Controller
┌──────────┐                ┌──────────────────┐
│          │                │                  │
│  GPIO21  ├───────────────►│ SDA              │
│  (SDA)   │   4.7kΩ to     │                  │
│          │   +3.3V        │                  │
│  GPIO22  ├───────────────►│ SCL              │
│  (SCL)   │   4.7kΩ to     │                  │
│          │   +3.3V        │                  │
│  GPIO25  ├───────────────►│ OE# (Output En)  │
│          │   (Active-low) │                  │
│      VCC ├───────────────►│ VCC (+3.3V/5V)   │
│      GND ├───────────────►│ GND              │
│          │                │                  │
│          │                │ A0, A1, A2  ──┬─ GND
│          │                │ A3, A4, A5  ──┘  (Addr: 0x40)
└──────────┘                └──────────────────┘

Notes:
- Pull-up resistors: 4.7kΩ on SDA and SCL
- I²C address: 0x40 (all address pins to GND)
- Bus frequency: 400 kHz (Fast Mode)
- OE# HIGH = outputs disabled (safe state)
```

### Complete System Diagram

```
                    ┌─────────────────────────────┐
                    │   5V Power Supply (10A)     │
                    │  (Mean Well LRS-50-5)       │
                    └──────┬──────────────┬───────┘
                           │ +5V          │ GND
                           │              │
        ┌──────────────────┼──────────────┼───────────────┐
        │                  │              │               │
        │  ┌───────────────▼──────────────▼────────┐      │
        │  │      Power Distribution Block         │      │
        │  │  (Fused: 8×1A + 1×2A for controller)  │      │
        │  └───┬───────────────────────────────┬───┘      │
        │      │                               │          │
        │      │ +5V (Controller)              │ +5V (LEDs)
        │      │                               │          │
┌───────▼──────▼──────┐             ┌──────────▼────────┐ │
│  ESP32-WROOM-32U    │             │  LED Strips       │ │
│  + PCA9685          │             │  (8 channels)     │ │
│  + SN74HCT245       │             │                   │ │
│                     │             │  - ALED ch1-8     │ │
│  GPIOs ─────────────┼────────────►│  - PWM ch1-8      │ │
│  (see pinout above) │   Control   │                   │ │
└─────────────────────┘   Signals   └───────────────────┘ │
         │                                                │
         │ GND                                       GND  │
         └────────────────────────────────────────────────┘
                         Common Ground Bus
```

---

## Power Requirements

### Power Supply Specifications

**Recommended:** Mean Well LRS-50-5 or similar

- **Voltage:** 5V DC ±5%
- **Current:** 10A minimum (50W)
- **Efficiency:** >80%
- **Protection:** Overcurrent, overvoltage, short-circuit
- **Cooling:** Fan or passive heatsink

### Power Budget Calculation

#### Addressable LEDs (WS2812B)

**Per Pixel:**
- Maximum: 60 mA @ full white (255,255,255)
- Typical: 20-40 mA @ moderate colors
- Idle: 1 mA @ off (control chip only)

**Example: 8 channels × 120 pixels = 960 pixels**
- Maximum theoretical: 960 × 60 mA = 57.6 A
- Configurable limit (recommended): 8 A
- Firmware enforces global current limit

#### PWM LEDs

**Per Channel (12V LED strip via MOSFET):**
- Depends on LED strip specification
- Example: 12V, 1A strip = 12W per channel
- Total for 8 channels: 8 × 1A = 8A @ 12V (separate supply)

#### ESP32 Controller

- **Active (Wi-Fi on):** 150-250 mA @ 5V
- **Peak (transmitting):** 350 mA @ 5V
- **Recommend:** 500 mA allocation @ 5V

#### Total System

**5V Rail:**
- ESP32 + peripherals: 0.5 A
- Addressable LEDs: 8 A (configurable limit)
- **Total:** 8.5 A @ 5V = 42.5W

**Recommended power supply:** 10A @ 5V (50W) with 18% headroom

### Power Distribution

```
5V Power Supply (10A)
│
├─ Fuse 2A ──► ESP32-WROOM-32U + PCA9685 + Level Shifters
│
├─ Fuse 1A ──► ALED Channel 1
├─ Fuse 1A ──► ALED Channel 2
├─ Fuse 1A ──► ALED Channel 3
├─ Fuse 1A ──► ALED Channel 4
├─ Fuse 1A ──► ALED Channel 5
├─ Fuse 1A ──► ALED Channel 6
├─ Fuse 1A ──► ALED Channel 7
└─ Fuse 1A ──► ALED Channel 8

PWM Channels (separate 12V/24V supply if used)
```

### Fusing Recommendations

- **Controller:** 2A slow-blow fuse
- **Each ALED channel:** 1A fast-blow fuse
- **Main supply:** 10A circuit breaker or fuse

---

## Addressable LED Channels

### Supported LED Types

#### WS2812B (RGB)
- **Color Order:** GRB (default)
- **Voltage:** 5V ±0.5V
- **Protocol:** Single-wire, 800 kHz
- **Timing:**
  - T0H: 0.4 µs ±150 ns
  - T0L: 0.85 µs ±150 ns
  - T1H: 0.8 µs ±150 ns
  - T1L: 0.45 µs ±150 ns
  - RES: >50 µs

#### SK6812 (RGBW)
- **Color Order:** GRBW (default)
- **Voltage:** 5V ±0.5V
- **Protocol:** Single-wire, 800 kHz
- **White Channel:** Dedicated white LED
- **Timing:** Similar to WS2812B

### Configuration

Edit `main/config/default_config.json`:

```json
{
  "aled": [
    {
      "ch": 1,
      "pixels": 120,
      "type": "WS2812B",
      "order": "GRB",
      "gamma": 2.2,
      "max_fps": 90,
      "mA_per_led": 45
    }
  ]
}
```

**Parameters:**
- `ch` - Channel number (1-8)
- `pixels` - Number of LEDs (1-1200)
- `type` - "WS2812B" or "SK6812_RGBW"
- `order` - Color order: "GRB", "RGB", "GRBW", "RGBW"
- `gamma` - Gamma correction (1.0-3.0, typical: 2.2)
- `max_fps` - Maximum frame rate (1-120)
- `mA_per_led` - Current per LED in mA (typical: 45-60)

### Level Shifting

**Why SN74HCT245?**
- ESP32 outputs 3.3V logic
- WS2812B requires 5V logic (>3.5V HIGH)
- SN74HCT245 converts 3.3V → 5V cleanly
- Fast switching for 800 kHz signal

**Alternative:** 74AHCT125 (quad buffer, lower gate count)

### Signal Integrity

**Best Practices:**
1. **Short traces:** Keep ESP32 → level shifter < 10 cm
2. **Series resistor:** 330Ω-470Ω between shifter and first LED
3. **Capacitor:** 1000 µF across power near LED strip
4. **Twisted pair:** Use for long runs (DI + GND)
5. **Termination:** Not required for <5 meters

---

## PWM Channels

### PCA9685 Specifications

- **Channels:** 16 (8 used for LEDch1-8, 8 reserved)
- **Resolution:** 12-bit (0-4095)
- **Frequency:** 40 Hz to 1600 Hz
- **I²C Address:** 0x40 (configurable via A0-A5 pins)
- **Supply Voltage:** 2.3V to 5.5V
- **Logic Level:** 3.3V or 5V compatible

### Configuration

```json
{
  "pwm": [
    {
      "ch": 1,
      "freq_hz": 1000,
      "curve": "log",
      "soft_start_ms": 150,
      "max_duty": 0.85
    }
  ]
}
```

**Parameters:**
- `ch` - Channel number (1-8)
- `freq_hz` - PWM frequency (50-5000 Hz, default: 1000)
- `curve` - "linear" or "log" (logarithmic for perceived brightness)
- `soft_start_ms` - Soft-start duration (0-1000 ms)
- `max_duty` - Maximum duty cycle (0.0-1.0)

### MOSFET Selection

**Logic-Level MOSFETs (recommended):**
- **IRLZ44N** - 55V, 47A, Vgs(th) = 1-2V
- **IRL540N** - 100V, 36A, Vgs(th) = 1-2V
- **IRLB8721** - 30V, 62A, Vgs(th) = 1.35-2.35V

**Standard MOSFETs (requires 10V gate drive):**
- **IRF540N** - 100V, 33A, Vgs(th) = 2-4V
- (Not recommended without gate driver)

**Heatsinking:**
- <2A: No heatsink required
- 2-5A: Small TO-220 clip heatsink
- >5A: Large heatsink with thermal compound

### Wiring LED Strips to PWM

**12V LED Strip:**
```
PCA9685 PWM → 1kΩ → MOSFET Gate
                     MOSFET Drain → LED Strip (-)
12V Power Supply (+) → LED Strip (+)
12V Power Supply (-) → MOSFET Source → Common Ground
```

**Protection:**
- **Gate resistor:** 1kΩ (limit inrush current)
- **Pulldown resistor:** 10kΩ gate-to-source (ensure off state)
- **Flyback diode:** 1N4007 across load (for inductive loads)

---

## Safety Guidelines

### Electrical Safety

**DANGER - HIGH CURRENT**

1. **Never work on live circuits**
   - Disconnect power before wiring changes
   - Use insulated tools
   - Wear safety glasses

2. **Proper wire gauge**
   - 5V, 10A: Use 16 AWG (1.3 mm²) or larger
   - 12V, 5A: Use 18 AWG (0.82 mm²) or larger
   - Short runs (<1m) can use lighter gauge

3. **Fusing**
   - Always fuse each channel
   - Use appropriate fuse ratings
   - Fast-blow for overcurrent protection

4. **Insulation**
   - Heat-shrink all solder joints
   - Use wire management (cable ties, conduit)
   - Avoid exposed conductors

5. **Grounding**
   - Connect all ground returns to common point
   - Use star grounding topology
   - Minimize ground loops

### Fire Safety

**LED strips and power supplies can get hot!**

1. **Mounting:**
   - Mount on non-flammable surfaces
   - Maintain airflow around power supply
   - Don't cover LED strips (heat dissipation)

2. **Temperature limits:**
   - LED strips: <60°C ambient
   - Power supply: Follow manufacturer specs
   - MOSFETs: <100°C junction (use heatsink)

3. **Overcurrent protection:**
   - Firmware limits addressable LED current
   - Fuses protect against shorts
   - Thermal cutoffs on power supply

### ESD Protection

**Electrostatic discharge can damage components!**

1. **Handling:**
   - Ground yourself before touching boards
   - Use ESD wrist strap or mat
   - Store in anti-static bags

2. **Installation:**
   - Avoid carpeted areas
   - Discharge body to ground before handling
   - Keep components in packaging until ready

### Operating Environment

**Suitable for indoor use only**

- **Temperature:** 0°C to 40°C (32°F to 104°F)
- **Humidity:** 20% to 80% RH, non-condensing
- **Altitude:** <2000m
- **Pollution degree:** 2 (normal indoor environment)

**NOT suitable for:**
- Outdoor use without weatherproof enclosure
- High humidity environments (bathrooms, pools)
- Explosive atmospheres
- Medical applications

---

## Component Specifications

### Main Components

| Component | Part Number | Specifications |
|-----------|-------------|----------------|
| Microcontroller | ESP32-WROOM-32U | 240 MHz, 520KB RAM, Wi-Fi |
| PWM Driver | PCA9685 | 16-ch, 12-bit, I²C |
| Level Shifter | SN74HCT245 | Octal, 3.3V→5V |
| MOSFET | IRLZ44N | 55V, 47A, logic-level |
| Voltage Regulator | AMS1117-3.3 | 3.3V, 1A LDO |
| Power Supply | LRS-50-5 | 5V, 10A, 50W |

### Passive Components

- **I²C Pull-ups:** 4.7kΩ ±5%, 1/4W
- **Gate Resistors:** 1kΩ ±5%, 1/4W
- **Pulldown Resistors:** 10kΩ ±5%, 1/4W
- **Data Resistors:** 330Ω-470Ω ±5%, 1/4W
- **Decoupling Caps:** 100nF ceramic, 0603/0805
- **Bulk Caps:** 1000µF electrolytic, 6.3V+

---

## Testing and Validation

### Initial Power-On Test

1. **Visual inspection:**
   - Check for shorts
   - Verify all connections
   - Look for damaged components

2. **Power supply test:**
   - Measure output voltage (should be 5.0V ±0.25V)
   - Check current draw (idle: <100 mA)

3. **ESP32 boot:**
   - Connect USB serial
   - Monitor boot messages
   - Verify Wi-Fi AP starts

4. **I²C test:**
   - Check for PCA9685 @ address 0x40
   - Verify all PWM channels initialize

5. **ALED test:**
   - Run self-test (pulses ALEDch1)
   - Check all 8 channels respond

### Functional Tests

**Addressable LED Test:**
```bash
# From web UI or REST API
curl -X POST http://192.168.4.1/api/trigger \
  -d '{"action":"flash","channel":0,"duration_ms":1000}'
```

**PWM Test:**
```bash
# Set PWM channel 1 to 50% duty
curl -X POST http://192.168.4.1/api/config \
  -d '{"pwm":[{"ch":1,"max_duty":0.5}]}'
```

### Performance Validation

**Metrics to check:**
- Free heap: ≥80 KB during operation
- Effect loop time: ≤3 ms for 8×180px @ 60fps
- I²C transaction time: <1 ms per PWM update
- REST response time: <50 ms
- Wi-Fi signal strength: ≥-70 dBm

### Load Testing

**Full load test (caution: high current):**
1. Connect ammeter in series with 5V supply
2. Set all ALED channels to full white
3. Verify current ≤ configured limit (default: 8A)
4. Check voltage under load (should stay >4.75V)
5. Monitor temperature (power supply and MOSFETs)

---

## Troubleshooting

### Power Issues

**Problem:** ESP32 won't boot
- Check 5V supply voltage
- Verify 3.3V regulator output
- Check for shorts on power rails

**Problem:** Brownout detector triggers
- Increase power supply capacity
- Add bulk capacitance (1000-2200µF)
- Reduce LED current limit

### Addressable LED Issues

**Problem:** LEDs don't light up
- Check 5V power to LED strip
- Verify data signal at first pixel (should see pulses)
- Check level shifter output (should be 5V logic)
- Verify GPIO pin assignment in firmware

**Problem:** LEDs show wrong colors
- Check color order setting (GRB vs RGB)
- Verify gamma correction value
- Test with solid colors first

**Problem:** LEDs flicker or glitch
- Add 330Ω resistor before first pixel
- Add 1000µF capacitor at strip power input
- Shorten or shield data wire
- Check for ground loops

### PWM Issues

**Problem:** PCA9685 not detected
- Check I²C pull-up resistors (4.7kΩ)
- Verify address pins (should be GND for 0x40)
- Test with I²C scanner tool
- Check SDA/SCL connections

**Problem:** PWM output always off
- Check OE# pin (should be LOW to enable)
- Verify MOSFET gate connection
- Check MOSFET source to ground
- Test PCA9685 directly (bypass MOSFET)

**Problem:** PWM doesn't dim smoothly
- Use logarithmic curve instead of linear
- Increase PWM frequency (try 2000 Hz)
- Check for PWM aliasing with camera

### Network Issues

**Problem:** Can't connect to Wi-Fi AP
- Check SSID in serial monitor output
- Verify password (default: "lumigrid-setup")
- Try forgetting network and reconnecting
- Check for 2.4 GHz radio on client device

**Problem:** Web UI won't load
- Verify IP address (192.168.4.1 in AP mode)
- Check filesystem mounted (serial monitor)
- Try different browser
- Clear browser cache

---

## Bill of Materials (BOM)

### Essential Components

| Item | Quantity | Part Number | Notes |
|------|----------|-------------|-------|
| ESP32-WROOM-32U | 1 | ESP32-WROOM-32U | Main MCU |
| PCA9685 | 1 | PCA9685PW | PWM driver IC |
| SN74HCT245 | 1 | SN74HCT245N | Level shifter |
| IRLZ44N MOSFET | 8 | IRLZ44N | Logic-level N-FET |
| AMS1117-3.3 | 1 | AMS1117-3.3 | 3.3V regulator |
| 4.7kΩ resistor | 2 | - | I²C pull-ups |
| 1kΩ resistor | 8 | - | MOSFET gate resistors |
| 10kΩ resistor | 8 | - | MOSFET pulldowns |
| 330Ω resistor | 8 | - | Data line protection |
| 100nF capacitor | 10 | - | Decoupling |
| 1000µF capacitor | 9 | - | Bulk (1 per ALED + 1 PS) |
| 1N4007 diode | 8 | 1N4007 | Flyback protection |
| 1A fuse | 8 | - | ALED channel protection |
| 2A fuse | 1 | - | Controller protection |

### Power Supply

| Item | Specifications | Example Part |
|------|----------------|--------------|
| 5V Power Supply | 10A, 50W | Mean Well LRS-50-5 |
| Wire (16 AWG) | Stranded copper | - |
| Fuse holders | Blade or cartridge | - |
| Terminal blocks | 5mm pitch | - |

---

## Additional Resources

- **ESP32 Datasheet:** https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
- **PCA9685 Datasheet:** https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
- **WS2812B Datasheet:** https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf
- **SN74HCT245 Datasheet:** https://www.ti.com/lit/ds/symlink/sn74hct245.pdf

---

**Safety Notice:** This hardware can draw high currents and generate significant heat. Always follow proper electrical safety practices. If you're not comfortable working with mains voltage or high-current DC systems, consult a qualified electrician.

**Disclaimer:** This guide is for informational purposes. The authors assume no liability for damage or injury resulting from construction or use of this hardware.

---

**Last Updated:** 2026-01-08  
**Hardware Revision:** v1.0  
**Compatible Firmware:** v1.0.0+
