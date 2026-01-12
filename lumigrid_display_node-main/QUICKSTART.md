# LumiGrid Display Node - Quick Start Guide

Welcome to LumiGrid Display Node! This guide will get you up and running in minutes.

## Table of Contents

- [What You'll Need](#what-youll-need)
- [Quick Installation](#quick-installation)
- [First Run](#first-run)
- [Basic Usage](#basic-usage)
- [Next Steps](#next-steps)

## What You'll Need

### Hardware
- **Raspberry Pi** (Model 3, 4, or 5 recommended)
- **RGB LED Matrix Panel** (16x32, 32x32, 32x64, or 64x64 pixels)
- **Power Supply** for LED matrix (5V, 10A+ depending on panel size)
- **MicroSD Card** (16GB+ with Raspberry Pi OS installed)

### Optional Hardware
- **DS3231 RTC Module** - For accurate timekeeping without internet
- **DS18B20 Temperature Sensor** - For temperature display
- **DHT22 Sensor** - For temperature and humidity display

### Network
- WiFi or Ethernet connection for your Raspberry Pi
- Access to your Pi via SSH or monitor/keyboard

## Quick Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/algorhythmicdev/lumigrid_display_node.git
cd lumigrid_display_node
```

### Step 2: Run the Installation Script

The install script will handle everything automatically:

```bash
chmod +x install.sh
./install.sh
```

This script will:
- ✅ Install system dependencies
- ✅ Set up Python virtual environment
- ✅ Build and install RGB LED matrix library
- ✅ Install Python packages
- ✅ Configure I2C and 1-Wire interfaces
- ✅ Set up the systemd service

**Installation takes 10-15 minutes.** ☕

### Step 3: Configure Your Display

Edit the configuration file:

```bash
nano config.yaml
```

**Minimal configuration** (update these values):

```yaml
led_matrix:
  rows: 32              # Your panel height
  cols: 64              # Your panel width
  brightness: 50        # Start with 50%
  hardware_mapping: "regular"  # or "adafruit-hat" if using Adafruit HAT

display:
  default_mode: "frames"  # Use the frame system
```

Save and exit (Ctrl+X, Y, Enter)

### Step 4: Start the Service

```bash
sudo systemctl enable lumigrid
sudo systemctl start lumigrid
```

## First Run

### Access the Web Interface

1. Find your Pi's IP address:
   ```bash
   hostname -I
   ```

2. Open a web browser and navigate to:
   ```
   http://YOUR_PI_IP:5000
   ```
   
   For example: `http://192.168.1.100:5000`

3. You should see the LumiGrid control panel! 🎉

### Verify Everything Works

Check the service status:

```bash
sudo systemctl status lumigrid
```

View logs:

```bash
sudo journalctl -u lumigrid -f
```

Press Ctrl+C to stop viewing logs.

## Basic Usage

### Using the Web Interface

The web interface provides easy control over your display:

1. **Dashboard** - View current status and system info
2. **Display Control** - Change brightness, modes, and send messages
3. **Presets** - Create and manage display presets
4. **Timelines** - Schedule automatic preset playback
5. **Settings** - Configure display and system settings

### Quick Actions via Web UI

**Change Brightness:**
- Use the brightness slider on the dashboard
- Range: 0-100%

**Send a Message:**
- Click "Message" mode
- Enter your text in the message field
- Click "Send"

**Create a Time Display Preset:**
1. Navigate to "Presets" tab
2. Click "Create Preset"
3. Fill in:
   - Name: `my_clock`
   - Type: `time`
   - Text Color: `#00FFFF` (cyan)
   - Background: `#000000` (black)
4. Click "Save"

### Quick API Examples

**Get Current Status:**

```bash
curl http://YOUR_PI_IP:5000/api/status
```

**Set Brightness:**

```bash
curl -X POST http://YOUR_PI_IP:5000/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "set_brightness", "value": 75}'
```

**Show a Message:**

```bash
curl -X POST http://YOUR_PI_IP:5000/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "show_message", "message": "Hello World!"}'
```

**Create a Preset:**

```bash
curl -X POST http://YOUR_PI_IP:5000/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "blue_time",
    "description": "Blue time display",
    "frame_config": {
      "type": "time",
      "colors": {
        "background": "#000000",
        "text": "#0000FF"
      },
      "font": {
        "size": 18
      },
      "format": "24h"
    }
  }'
```

## Next Steps

### Learn More

📖 **[Full README](README.md)** - Complete feature documentation

🔧 **[Hardware Setup Guide](HARDWARE.md)** - Detailed wiring and hardware info

🌐 **[API Reference](docs/API_REFERENCE.md)** - Complete API documentation

🔄 **[UDP Synchronization](docs/UDP_SYNCHRONIZATION.md)** - Multi-display sync

⏱️ **[Timeline Sequencer](TIMELINE_SEQUENCER.md)** - Automated scheduling

🎨 **[Style System](StyleSystem.md)** - Customize the web interface

🔌 **[ESP32 Integration](INTEGRATION.md)** - Connect with LED nodes

### Common Customizations

**Add a Temperature Sensor:**

Edit `config.yaml`:

```yaml
temperature:
  enabled: true
  sensor_type: "DS18B20"  # or "DHT22"
  unit: "celsius"
```

Then restart:

```bash
sudo systemctl restart lumigrid
```

**Set Up RTC for Offline Timekeeping:**

Edit `config.yaml`:

```yaml
rtc:
  enabled: true
  type: "DS3231"
  sync_interval: 3600
```

**Create Automated Schedules:**

See [TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md) for detailed scheduling setup.

### Troubleshooting

**Display not showing anything?**

1. Check power supply is adequate
2. Verify GPIO connections
3. Try running manually to see errors:
   ```bash
   sudo systemctl stop lumigrid
   cd /home/pi/lumigrid_display_node
   source venv/bin/activate
   sudo -E python3 -m lumigrid.main
   ```

**Web interface not accessible?**

1. Check the service is running:
   ```bash
   sudo systemctl status lumigrid
   ```
2. Verify firewall settings
3. Try accessing from the Pi itself:
   ```bash
   curl http://localhost:5000
   ```

**Colors look wrong or flickering?**

- Try increasing `gpio_slowdown` in config.yaml (try values 2-4)
- Verify `hardware_mapping` matches your panel type
- Run with sudo for best performance

**Need more help?**

- Check the full [README.md](README.md) troubleshooting section
- View logs: `sudo journalctl -u lumigrid -f`
- Visit: https://github.com/algorhythmicdev/lumigrid_display_node

## Development Setup

For contributors wanting to develop LumiGrid:

See **[CONTRIBUTING.md](CONTRIBUTING.md)** for development setup, testing guidelines, and code standards.

---

**Congratulations! Your LumiGrid Display Node is ready to use!** 🎉

For advanced features and integrations, explore the documentation links above.
