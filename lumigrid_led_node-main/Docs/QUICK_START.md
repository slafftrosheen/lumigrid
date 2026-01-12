# LumiGrid Quick Start Guide

## ⚠️ Important: Build from the Correct Directory

The firmware project is in the `firmware/` subdirectory, not the root.

## Option 1: Use the Build Script (Easiest)

```bash
# From the project root directory
./BUILD.sh
```

This script will:
1. Build the modern SvelteKit web UI
2. Deploy it to firmware/assets/webapp/
3. Build the firmware with embedded web UI
4. Create LittleFS partition image

## Option 2: Manual Build

```bash
# Navigate to firmware directory
cd firmware

# Build (includes webapp in LittleFS partition)
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

**Note:** The firmware build automatically creates a LittleFS partition image from `assets/webapp/` and includes it in the flash process.

## First-Time Setup

If ESP-IDF is not set up in your terminal:

```bash
# Activate ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Or if installed elsewhere
source /path/to/esp-idf/export.sh

# Then build
cd <project-root>/firmware
idf.py build
```

## After Flashing

1. **Device boots and creates Wi-Fi Access Point:**
   - SSID: `LumiGrid-Setup`
   - Password: `lumigrid-setup`

2. **Connect to the AP and open browser:**
   - URL: `http://192.168.4.1`

3. **Access REST API:**
   ```bash
   curl http://192.168.4.1/api/status
   ```

4. **Monitor real-time events:**
   ```bash
   curl -N http://192.168.4.1/events
   ```

## Common Errors

### "CMake Error: The source directory does not appear to contain CMakeLists.txt"

**Problem:** You're running the build from the wrong directory.

**Solution:**
```bash
cd <project-root>/firmware
idf.py build
```

### "idf.py: command not found"

**Problem:** ESP-IDF environment not activated.

**Solution:**
```bash
source ~/esp/esp-idf/export.sh
```

### "Port /dev/ttyUSB0 doesn't exist"

**Problem:** ESP32 not connected or using different port.

**Solution:**
```bash
# List available ports
ls /dev/tty*

# Use the correct port (might be /dev/ttyACM0, /dev/ttyUSB1, etc.)
idf.py -p /dev/ttyACM0 flash monitor
```

## Project Structure

```
lumigrid-1/                  ← You are here (root)
├── firmware/                ← Build from here!
│   ├── CMakeLists.txt       ← Root CMake file
│   ├── main/                ← Application code
│   └── components/          ← 8 custom components
├── Docs/                    ← Documentation
├── BUILD.sh                 ← Convenient build script
└── README.md                ← Project overview
```

## What Gets Built

When you run `idf.py build` in the `firmware/` directory:

- ✅ 30 C source files compiled
- ✅ 8 custom components linked
- ✅ ESP-IDF libraries included
- ✅ Web UI embedded (if present in `/spiffs`)
- ✅ Binary size: ~800 KB (typical)

## Next Steps

See detailed guides:
- [BUILD_GUIDE.md](firmware/BUILD_GUIDE.md) - Complete build documentation
- [PROJECT_REPORT.md](PROJECT_REPORT.md) - Architecture & API reference
- [PHASE2_3_COMPLETE.md](PHASE2_3_COMPLETE.md) - Latest features

## Quick Commands Reference

```bash
# Navigate to firmware directory (ALWAYS do this first!)
cd <project-root>/firmware

# Build only
idf.py build

# Clean build
idf.py fullclean build

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Flash + Monitor (combined)
idf.py -p /dev/ttyUSB0 flash monitor

# Check code size
idf.py size

# Show component sizes
idf.py size-components
```

## Support

- 📖 Documentation: `Docs/` directory
- 🐛 Issues: Check build logs in `firmware/build/`
- 💬 Architecture: See `Docs/ARCHITECTURE.md`

---

**Remember:** Always `cd firmware/` before running `idf.py` commands!
