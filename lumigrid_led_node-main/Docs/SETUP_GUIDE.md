# LumiGrid LED Node - Complete Setup Guide

**Version:** 1.0.0  
**Target Platform:** ESP32-WROOM-32U  
**Framework:** ESP-IDF v5.3+

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [System Dependencies](#system-dependencies)
3. [ESP-IDF Installation](#esp-idf-installation)
4. [Project Setup](#project-setup)
5. [Building the Firmware](#building-the-firmware)
6. [Flashing to Hardware](#flashing-to-hardware)
7. [First Boot Configuration](#first-boot-configuration)
8. [VSCode/PlatformIO Setup](#vscodeplatformio-setup)
9. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Required Software
- **Operating System:** Linux (Ubuntu 20.04+), macOS, or Windows with WSL2
- **Python:** 3.8 or newer
- **Git:** Latest version
- **USB Drivers:** CH340/CP210x for ESP32 serial connection

### Hardware Requirements
- ESP32-WROOM-32U development board
- USB cable (data capable, not charging-only)
- Power supply: 5V, minimum 2A (10A recommended for full LED load)
- LED strips: WS2812B or SK6812 addressable LEDs
- (Optional) PCA9685 PWM controller for additional PWM channels

---

## System Dependencies

### Linux (Ubuntu/Debian)

```bash
# Update package manager
sudo apt-get update && sudo apt-get upgrade -y

# Install required system packages
sudo apt-get install -y \
  git wget flex bison gperf cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  python3 python3-venv python3-pip python3-setuptools

# Install USB serial drivers (if not already installed)
sudo apt-get install -y dialout
sudo usermod -a -G dialout $USER
# Log out and back in for group changes to take effect
```

### macOS

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake ninja dfu-util python3

# Install USB driver (if needed)
# Download from https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
```

### Windows (WSL2)

```bash
# Use Ubuntu on WSL2 and follow Linux instructions above
# Install USB/IP for serial access from WSL2
# See: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html
```

---

## ESP-IDF Installation

### Method 1: Standard Installation (Recommended)

```bash
# Create ESP directory
mkdir -p ~/esp
cd ~/esp

# Clone ESP-IDF v5.3
git clone --depth=1 --branch v5.3 https://github.com/espressif/esp-idf.git

# Run installer for ESP32
cd ~/esp/esp-idf
./install.sh esp32

# Set up environment variables (add to ~/.bashrc or ~/.zshrc for persistence)
. ~/esp/esp-idf/export.sh

# Verify installation
idf.py --version
# Expected output: ESP-IDF v5.3.x
```

### Method 2: Using ESP-IDF Installer (Alternative)

Download and run the official installer from:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

### Setting Up Environment

Add to your shell profile (`~/.bashrc`, `~/.zshrc`, or `~/.profile`):

```bash
# ESP-IDF Environment
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

Then source ESP-IDF before each session:
```bash
get_idf
```

Or for automatic activation, add to your shell profile:
```bash
. $HOME/esp/esp-idf/export.sh
```

---

## Project Setup

### Clone the Repository

```bash
# Clone LumiGrid repository
git clone https://github.com/algorhythmicdev/lumigrid_led_node.git
cd lumigrid_led_node
```

### Install Python Dependencies

```bash
# Upgrade pip
python3 -m pip install --upgrade pip

# Install optional tools
pip install --user pipx && pipx ensurepath
pipx install esptool
```

---

## Building the Firmware

### Quick Build (Using Build Script)

The easiest way to build everything:

```bash
# From project root
./BUILD.sh
```

This script will:
1. Build the modern SvelteKit web UI
2. Deploy it to `firmware/assets/webapp/`
3. Build the ESP32 firmware with embedded web UI
4. Create LittleFS partition image

### Manual Build

#### Step 1: Activate ESP-IDF Environment

```bash
# Activate ESP-IDF (if not already active)
. ~/esp/esp-idf/export.sh
```

#### Step 2: Navigate to Firmware Directory

**IMPORTANT:** Always build from the `firmware/` directory!

```bash
cd firmware
```

#### Step 3: Set Target (First Time Only)

```bash
idf.py set-target esp32
```

#### Step 4: Configure Project (Optional)

```bash
idf.py menuconfig
```

**Recommended Settings:**
- `Component config → ESP32-specific → CPU frequency` → **240 MHz**
- `Component config → FreeRTOS → Tick rate (Hz)` → **1000** (default)
- `Partition Table` → Select "Custom partition table CSV" (if using LittleFS)

#### Step 5: Build

```bash
# Build firmware
idf.py build

# Or with verbose output
idf.py -v build

# Or using all CPU cores
idf.py -j$(nproc) build
```

**Expected Output:**
```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p (PORT) flash
```

#### Build Artifacts

After successful build:
- Binary: `firmware/build/lumigrid.bin`
- Partition table: `firmware/build/partition_table/partition-table.bin`
- LittleFS image: `firmware/build/storage.bin`

---

## Flashing to Hardware

### Step 1: Connect ESP32

1. Connect ESP32 to computer via USB cable
2. Identify the serial port:

```bash
# Linux
ls /dev/tty* | grep -E "(USB|ACM)"
# Common: /dev/ttyUSB0, /dev/ttyACM0

# macOS
ls /dev/cu.*
# Common: /dev/cu.usbserial-*

# Windows (WSL2)
# See USB/IP setup documentation
```

### Step 2: Flash Firmware

```bash
# Auto-detect port and flash
idf.py flash

# Or specify port manually
idf.py -p /dev/ttyUSB0 flash

# Flash with higher baud rate (faster)
idf.py -p /dev/ttyUSB0 -b 921600 flash

# Erase flash before flashing (clean slate)
idf.py -p /dev/ttyUSB0 erase-flash flash
```

### Step 3: Monitor Serial Output

```bash
# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Or combined flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Exit monitor: Press Ctrl+]
```

### Troubleshooting Flash Issues

**Problem:** Port permission denied
```bash
# Solution: Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

**Problem:** Flash fails with "A fatal error occurred"
```bash
# Solution 1: Hold BOOT button while flashing
# Solution 2: Try lower baud rate
idf.py -p /dev/ttyUSB0 -b 115200 flash

# Solution 3: Use external esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
```

---

## First Boot Configuration

### Wi-Fi Access Point Mode

On first boot, the device creates a Wi-Fi access point:

- **SSID:** `LumiGrid-Setup`
- **Password:** `lumigrid-setup` (or no password depending on configuration)
- **IP Address:** `192.168.4.1`

### Connecting to the Device

1. Connect to the `LumiGrid-Setup` Wi-Fi network
2. Open a web browser
3. Navigate to: `http://192.168.4.1`
4. You should see the LumiGrid web interface

### Configuring Wi-Fi

**Via Web UI:**
1. Navigate to Settings → Wi-Fi
2. Enter your network SSID and password
3. Click Save
4. Device will reboot and connect to your network

**Via REST API:**
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi": {
      "ssid": "YourNetworkName",
      "password": "YourPassword"
    }
  }'
```

### Finding Device IP Address

After connecting to your Wi-Fi network:

```bash
# Check serial monitor for IP address
idf.py monitor

# Or use network scanner
nmap -sn 192.168.1.0/24 | grep -i esp

# Or check your router's DHCP client list
```

---

## VSCode/PlatformIO Setup

### Using VSCode with ESP-IDF Extension

1. **Install VSCode Extensions:**
   - ESP-IDF Extension
   - C/C++ Extension (Microsoft)

2. **Configure ESP-IDF Path:**
   - Press `F1`
   - Type: "ESP-IDF: Configure ESP-IDF Extension"
   - Select your ESP-IDF installation path: `~/esp/esp-idf`

3. **Open Project:**
   ```bash
   code lumigrid_led_node/firmware
   ```

4. **Build Commands in VSCode:**
   - `F1` → "ESP-IDF: Build Project"
   - `F1` → "ESP-IDF: Flash Device"
   - `F1` → "ESP-IDF: Monitor Device"

### Workspace Settings

Create `.vscode/settings.json` in the `firmware/` directory:

```json
{
  "idf.adapterTargetName": "esp32",
  "idf.flashBaudRate": "921600",
  "idf.openOcdConfigs": [
    "interface/ftdi/esp32_devkitj_v1.cfg",
    "target/esp32.cfg"
  ],
  "C_Cpp.intelliSenseEngine": "Tag Parser",
  "files.associations": {
    "*.h": "c",
    "*.c": "c"
  }
}
```

### Tasks Configuration

Create `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Build",
      "type": "shell",
      "command": "idf.py build",
      "group": {
        "kind": "build",
        "isDefault": true
      }
    },
    {
      "label": "Flash",
      "type": "shell",
      "command": "idf.py -p /dev/ttyUSB0 flash",
      "group": "build"
    },
    {
      "label": "Monitor",
      "type": "shell",
      "command": "idf.py -p /dev/ttyUSB0 monitor",
      "group": "build"
    }
  ]
}
```

---

## Troubleshooting

### Common Build Errors

**Error:** "CMake Error: The source directory does not appear to contain CMakeLists.txt"

**Solution:**
```bash
# Make sure you're in the firmware directory
cd <project-root>/firmware
idf.py build
```

**Error:** "idf.py: command not found"

**Solution:**
```bash
# Activate ESP-IDF environment
. ~/esp/esp-idf/export.sh
```

**Error:** "Python.h: No such file or directory"

**Solution:**
```bash
# Install Python development headers
sudo apt-get install python3-dev
```

**Error:** "Missing component 'esp_littlefs'"

**Solution:**
```bash
# The component should be included, but if missing:
idf.py add-dependency "joltwallet/littlefs^1.10.2"
```

### Serial Port Issues

**Error:** "Could not open port /dev/ttyUSB0"

**Solution:**
```bash
# Check if port exists
ls -l /dev/ttyUSB*

# Add user to dialout group
sudo usermod -a -G dialout $USER

# Or run with sudo (not recommended)
sudo idf.py -p /dev/ttyUSB0 flash
```

### Build Performance

**Slow builds:**
```bash
# Enable ccache
export IDF_CCACHE_ENABLE=1

# Use all CPU cores
idf.py -j$(nproc) build

# Clean old build artifacts
idf.py fullclean
```

### Network Issues

**Cannot connect to device Wi-Fi AP:**
- Check that Wi-Fi is enabled on your computer
- Look for SSID starting with "LumiGrid"
- Check serial monitor for actual SSID/password
- Try forgetting the network and reconnecting

**Cannot access web UI:**
- Verify IP address (should be 192.168.4.1 in AP mode)
- Check firewall settings
- Try different browser
- Check serial monitor for errors

### Memory Issues

**Boot loops or crashes:**
```bash
# Check serial output for stack traces
idf.py monitor

# Enable memory debugging in menuconfig
idf.py menuconfig
# Navigate to: Component config → Heap memory debugging
```

---

## Next Steps

After successful setup:

1. **Read the API Reference:**
   - See [API_REFERENCE.md](API_REFERENCE.md) for complete REST API documentation

2. **Configure Hardware:**
   - See [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md) for wiring and safety

3. **Set Up Networking:**
   - See [NETWORKING_GUIDE.md](NETWORKING_GUIDE.md) for UDP sync and MQTT

4. **Development:**
   - See [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) for contribution guidelines

5. **Test the API:**
   ```bash
   # Check node status
   curl http://192.168.4.1/api/status
   
   # List available effects
   curl http://192.168.4.1/api/fx
   
   # Monitor real-time events
   curl -N http://192.168.4.1/events
   ```

---

## Quick Reference Commands

```bash
# Activate ESP-IDF
. ~/esp/esp-idf/export.sh

# Build from scratch
cd firmware
idf.py fullclean build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Just monitor
idf.py -p /dev/ttyUSB0 monitor

# Exit monitor
Ctrl+]

# Check sizes
idf.py size
idf.py size-components

# Set environment variable for port
export ESPPORT=/dev/ttyUSB0
idf.py flash monitor
```

---

## Support and Resources

- **Documentation:** [/Docs](/Docs) directory
- **Issues:** https://github.com/algorhythmicdev/lumigrid_led_node/issues
- **ESP-IDF Documentation:** https://docs.espressif.com/projects/esp-idf/en/latest/
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md)
- **Project Report:** [PROJECT_REPORT.md](PROJECT_REPORT.md)

---

**Last Updated:** 2026-01-08  
**ESP-IDF Version:** v5.3  
**Target:** ESP32-WROOM-32U
