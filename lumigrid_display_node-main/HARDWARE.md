# Hardware Setup Guide

This guide provides detailed information about hardware setup for the Lumigrid Display Node.

## Table of Contents
- [LED Matrix Panels](#led-matrix-panels)
- [Real-Time Clock (RTC)](#real-time-clock-rtc)
- [Temperature Sensors](#temperature-sensors)
- [Power Supply](#power-supply)
- [Complete Wiring Diagram](#complete-wiring-diagram)

## LED Matrix Panels

### Supported Panels
The system supports any RGB LED matrix panel that works with hzeller's rpi-rgb-led-matrix library, including:
- 16x32, 32x32, 32x64, 64x64 pixel panels
- Indoor and outdoor panels
- Various pixel pitches (P2, P2.5, P3, P4, P5, P6, P8, P10)

### Panel Connection
Connect your LED matrix panel to the Raspberry Pi GPIO header using:
- A proper RGB LED Matrix HAT/bonnet (recommended), or
- Direct wiring following the rpi-rgb-led-matrix pinout

**Recommended HATs:**
- Adafruit RGB Matrix HAT
- Adafruit RGB Matrix Bonnet
- Electrodragon RGB Matrix Panel Drive Board

### Panel Configuration
Configure your panel in `config.yaml`:
```yaml
led_matrix:
  rows: 32              # Panel height in pixels
  cols: 64              # Panel width in pixels
  chain_length: 1       # Number of panels chained
  parallel: 1           # Number of parallel chains
  hardware_mapping: "regular"  # or "adafruit-hat", "adafruit-hat-pwm"
  brightness: 50        # 0-100
  gpio_slowdown: 4      # Adjust if you see flickering
```

## Real-Time Clock (RTC)

### DS3231 RTC Module (Recommended)

**Specifications:**
- High accuracy: ±2ppm (±1 minute/year)
- I2C interface (address: 0x68)
- Built-in temperature sensor
- Battery backup (CR2032)

**Wiring:**
```
DS3231    →    Raspberry Pi
VCC       →    3.3V (Pin 1)
GND       →    Ground (Pin 6)
SDA       →    GPIO 2 / SDA (Pin 3)
SCL       →    GPIO 3 / SCL (Pin 5)
```

### DS1307 RTC Module (Alternative)

**Specifications:**
- Accuracy: Not as precise as DS3231
- I2C interface (address: 0x68)
- Battery backup (CR2032)

**Wiring:**
Same as DS3231

### Enable I2C
```bash
sudo raspi-config
# Navigate to: Interface Options → I2C → Enable
```

Verify I2C is working:
```bash
sudo i2cdetect -y 1
```

You should see the RTC at address 0x68:
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --
```

## Temperature Sensors

### DS18B20 Digital Temperature Sensor (1-Wire)

**Specifications:**
- Accuracy: ±0.5°C (-10°C to +85°C)
- 1-Wire digital interface
- Each sensor has unique 64-bit address
- Can connect multiple sensors on same bus

**Wiring:**
```
DS18B20        →    Raspberry Pi
VCC (Red)      →    3.3V (Pin 1)
GND (Black)    →    Ground (Pin 6)
Data (Yellow)  →    GPIO 4 (Pin 7)

+ 4.7kΩ resistor between Data and VCC
```

**Enable 1-Wire:**
Add to `/boot/config.txt`:
```
dtoverlay=w1-gpio,gpiopin=4
```

Verify sensor detection:
```bash
ls /sys/bus/w1/devices/
# Should show: 28-xxxxxxxxxxxx (your sensor ID)

# Read temperature:
cat /sys/bus/w1/devices/28-*/w1_slave
```

### DHT22 (AM2302) Temperature & Humidity Sensor

**Specifications:**
- Temperature: -40°C to 80°C (±0.5°C)
- Humidity: 0-100% RH (±2-5%)
- Digital output

**Wiring:**
```
DHT22          →    Raspberry Pi
VCC (Pin 1)    →    3.3V or 5V
Data (Pin 2)   →    GPIO 4 (configurable)
NC (Pin 3)     →    Not connected
GND (Pin 4)    →    Ground

+ 10kΩ resistor between Data and VCC
```

**Note:** Some DHT22 modules have built-in pull-up resistors.

### DHT11 Temperature & Humidity Sensor

**Specifications:**
- Temperature: 0-50°C (±2°C)
- Humidity: 20-90% RH (±5%)
- Lower accuracy than DHT22

**Wiring:**
Same as DHT22

## Power Supply

### LED Matrix Power Requirements

**Important:** LED matrices can draw significant current!

**Power consumption calculation:**
- Each RGB LED: ~60mA at full brightness
- 32x64 matrix = 2048 pixels = ~122.8A at full white, full brightness

**Practical requirements:**
- Typical usage (50% brightness, mixed colors): ~15-30A for 32x64 panel
- Use a 5V power supply rated for your specific panel
- Connect power directly to the LED panel's power input
- **DO NOT power the panel through the Raspberry Pi**

**Recommended power supplies:**
- 5V 10A for smaller panels (16x32)
- 5V 20A for medium panels (32x32)
- 5V 40A for larger panels (64x64)
- Use thick gauge wires (18 AWG or thicker)

**Power Raspberry Pi separately:**
- Use official Raspberry Pi power supply (5V 3A)
- Or power through GPIO (ensure 5V rail is adequate)

## Complete Wiring Diagram

### Raspberry Pi GPIO Pinout Reference

```
3.3V  (1)  (2)  5V
GPIO2 (3)  (4)  5V      ← SDA (RTC)
GPIO3 (5)  (6)  GND     ← SCL (RTC), Ground
GPIO4 (7)  (8)  GPIO14  ← 1-Wire (DS18B20) or DHT Data
GND   (9) (10)  GPIO15
...
```

### Complete Connection Example

```
Components:
- Raspberry Pi 3/4
- Adafruit RGB Matrix HAT
- 32x64 LED Matrix Panel
- DS3231 RTC Module
- DS18B20 Temperature Sensor
- 5V 20A Power Supply
- 4.7kΩ resistor

Connections:
1. LED Matrix:
   - Connect to RGB Matrix HAT (follows HAT pinout)
   - Power: 5V 20A PSU → LED matrix power input

2. RGB Matrix HAT:
   - Mount on Raspberry Pi GPIO header
   
3. DS3231 RTC (connected to HAT I2C pins):
   - VCC → 3.3V
   - GND → Ground
   - SDA → GPIO 2 (I2C SDA)
   - SCL → GPIO 3 (I2C SCL)

4. DS18B20 Sensor:
   - VCC → 3.3V
   - GND → Ground
   - Data → GPIO 4
   - 4.7kΩ resistor between Data and VCC

5. Power:
   - LED Matrix: 5V 20A PSU
   - Raspberry Pi: Official PSU or through HAT
```

## Safety Considerations

1. **Always disconnect power before wiring**
2. **Use proper gauge wires for high current**
3. **Ensure proper polarity (especially for power)**
4. **Use fuses or circuit breakers**
5. **Ensure adequate cooling for power supply**
6. **Don't mix 3.3V and 5V logic levels without level shifters**
7. **Test with low brightness first**

## Troubleshooting

### LED Matrix Issues
- **Flickering:** Increase `gpio_slowdown` in config
- **Wrong colors:** Check `hardware_mapping` setting
- **No display:** Check power supply and connections
- **Dim display:** Insufficient power supply

### I2C Issues
- **Device not detected:** Check wiring, enable I2C in raspi-config
- **Multiple I2C devices:** Ensure no address conflicts

### 1-Wire Issues
- **Sensor not found:** Enable 1-Wire overlay, check wiring
- **Unreliable readings:** Add/check pull-up resistor

### DHT Sensor Issues
- **No readings:** Check wiring, verify GPIO pin in config
- **Timeout errors:** Ensure pull-up resistor present
- **Checksum errors:** Sensor may be faulty or wiring issue

## Reference Links

- [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
- [Raspberry Pi Pinout](https://pinout.xyz/)
- [Adafruit RGB Matrix HAT](https://www.adafruit.com/product/2345)
- [DS3231 Datasheet](https://datasheets.maximintegrated.com/en/ds/DS3231.pdf)
- [DS18B20 Datasheet](https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf)
