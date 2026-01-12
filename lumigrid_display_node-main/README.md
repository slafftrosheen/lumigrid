# LumiGrid Display Node

A comprehensive Raspberry Pi-based LED matrix information display system with web interface control, ESP32 integration, and multi-node synchronization.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## 🚀 Quick Links

- **[Quick Start Guide](QUICKSTART.md)** - Get up and running in minutes
- **[API Reference](docs/API_REFERENCE.md)** - Complete REST API documentation
- **[Hardware Setup](HARDWARE.md)** - Detailed wiring and hardware guide
- **[ESP32 Integration](INTEGRATION.md)** - Connect with LED nodes and sensors
- **[Configuration Guide](CONFIGURATION.md)** - All config options explained
- **[Contributing](CONTRIBUTING.md)** - Join the development
- **[Preset Examples](preset_examples/)** - Ready-to-use display presets

## Features

- 🎨 **LED Matrix Display** - Powered by [hzeller's rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) library
- 🕐 **Real-Time Clock (RTC)** - DS3231/DS1307 support for accurate timekeeping
- 🌡️ **Temperature Sensors** - Support for DS18B20, DHT11, and DHT22 sensors
- 📱 **Web Interface** - Modern, responsive control panel accessible from any device
- ⏱️ **Timeline Sequencer** - Schedule preset playback based on time, date, or intervals
- 🔄 **UDP Synchronization** - Master/Slave sync for coordinated multi-node displays (ESP32 compatible)
- 🔄 **Multiple Display Modes**:
  - Clock display with customizable format
  - Temperature display
  - Custom message display with scrolling
  - Slideshow mode (auto-cycling through modes)
  - Frame-based content system with custom presets
- ⚙️ **Configurable** - YAML-based configuration for easy customization
- 🔧 **Systemd Integration** - Run as a system service with auto-restart

## Hardware Requirements

### Required Components
- Raspberry Pi (any model with GPIO headers)
- RGB LED Matrix (supported by rpi-rgb-led-matrix library)
- Power supply appropriate for your LED matrix

### Optional Components
- DS3231 or DS1307 RTC module (I2C)
- DS18B20 temperature sensor (1-Wire) OR
- DHT11/DHT22 temperature/humidity sensor

## Software Requirements

- Raspberry Pi OS (previously Raspbian)
- Python 3.7 or higher
- hzeller's rpi-rgb-led-matrix library

## Installation

**New users**: See the **[Quick Start Guide](QUICKSTART.md)** for step-by-step installation instructions.

### Quick Install Summary

```bash
# Clone repository
git clone https://github.com/algorhythmicdev/lumigrid_display_node.git
cd lumigrid_display_node

# Run installation script (takes 10-15 minutes)
chmod +x install.sh
./install.sh

# Edit configuration
nano config.yaml

# Start the service
sudo systemctl enable lumigrid
sudo systemctl start lumigrid

# Access web interface at http://YOUR_PI_IP:5000
```

For detailed installation instructions, hardware setup, and troubleshooting, see:
- **[Quick Start Guide](QUICKSTART.md)** - Complete installation walkthrough
- **[Hardware Setup Guide](HARDWARE.md)** - Wiring diagrams and hardware details
- **[Configuration Guide](CONFIGURATION.md)** - All configuration options

### Manual Installation

For manual installation or if the quick install script fails, see the [Quick Start Guide - Manual Installation section](QUICKSTART.md#manual-installation).

## Configuration

Configuration is managed through `config.yaml`. See **[CONFIGURATION.md](CONFIGURATION.md)** for complete documentation of all options.

### Key Configuration Sections

### LED Matrix Settings
```yaml
led_matrix:
  rows: 32              # Matrix height
  cols: 64              # Matrix width
  chain_length: 1       # Number of chained panels
  brightness: 50        # Brightness (0-100)
  hardware_mapping: "regular"  # Hardware type
  disable_hardware_pulsing: false  # Set to true to run without root privileges (reduces color quality)
```

### Display Settings
```yaml
display:
  default_mode: "clock"  # clock, temperature, message, slideshow
  font_path: "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
  slideshow_interval: 10  # seconds between mode changes
```

### Temperature Sensor
```yaml
temperature:
  enabled: true
  sensor_type: "DS18B20"  # DS18B20, DHT22, DHT11
  pin: 4                  # GPIO pin (for DHT sensors)
  unit: "celsius"         # celsius or fahrenheit
```

### RTC Settings
```yaml
rtc:
  enabled: true
  type: "DS3231"         # DS3231 or DS1307
  sync_interval: 3600    # Sync system time every hour
```

### Web Interface
```yaml
web:
  host: "0.0.0.0"       # Listen on all interfaces
  port: 5000            # Web server port
  debug: false
```

## Usage

### Web Interface

Access the web interface at `http://your-pi-ip:5000` to:
- Switch between display modes
- Send custom messages
- Adjust brightness
- View temperature readings
- Monitor system status
- **Access comprehensive in-app documentation** at `/help` or via the "Help & Docs" menu

The web interface includes:
- **Help & Documentation** (`/help`) - Complete user and developer guides
- **FAQ** (`/faq`) - Frequently asked questions
- **Setup Wizard** - First-run configuration assistant
- **Interactive Troubleshooting** - Common problems and solutions

For headless setups, replace `your-pi-ip` with the actual IP address of your Raspberry Pi (e.g., `http://192.168.8.218:5000`). You can find your Pi's IP address with:
```bash
hostname -I
```

### Command Line

Run directly:
```bash
python3 -m lumigrid.main
```

Or use systemd:
```bash
sudo systemctl start lumigrid    # Start the service
sudo systemctl stop lumigrid     # Stop the service
sudo systemctl restart lumigrid  # Restart the service
sudo systemctl status lumigrid   # Check status
```

View logs:
```bash
sudo journalctl -u lumigrid -f   # Follow systemd logs
tail -f logs/lumigrid.log        # Follow application logs
```

### Production Deployment

For production use, it's recommended to run the web interface with a production WSGI server instead of Flask's built-in development server:

**Using Gunicorn:**
```bash
# Install Gunicorn in the virtual environment
source venv/bin/activate
pip install gunicorn

# Run with Gunicorn
gunicorn --workers 1 --bind 0.0.0.0:5000 --timeout 120 lumigrid.wsgi:app
```

**For systemd service with Gunicorn:**
Update your systemd service file to use Gunicorn:
```ini
[Service]
Type=simple
User=pi
Group=pi
WorkingDirectory=/home/pi/lumigrid_display_node
Environment="LUMIGRID_CONFIG=/home/pi/lumigrid_display_node/config.yaml"
Environment="PATH=/home/pi/lumigrid_display_node/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
ExecStart=/home/pi/lumigrid_display_node/venv/bin/gunicorn --workers 1 --bind 0.0.0.0:5000 --timeout 120 lumigrid.wsgi:app
Restart=always
RestartSec=10
```

**Using uWSGI:**
```bash
# Install uWSGI
pip3 install uwsgi

# Run with uWSGI
uwsgi --http 0.0.0.0:5000 --module lumigrid.wsgi:app --master --processes 4
```

Note: The current implementation includes thread synchronization to safely handle concurrent web requests and display updates.

## Display Modes

### Clock Mode
Displays current time in 24-hour or 12-hour format with optional seconds.

### Temperature Mode
Shows current temperature reading from the configured sensor.

### Message Mode
Displays custom text messages. Long messages automatically scroll across the display.

### Slideshow Mode
Automatically cycles through Clock and Temperature modes at configured intervals.

### Frame Mode (Preset System)
Display custom content frames with full control over:
- Text, time, date, and temperature displays
- Custom fonts, colors, and positioning
- Individual frame duration and transitions
- Enable/disable individual frames
- Frame reordering

### Timeline Sequencer
Schedule automated preset playback based on:
- **Time of Day**: Display specific content at set times (e.g., weather at 8 AM)
- **Specific Dates**: One-time or event-based displays
- **Intervals**: Rotate content at regular intervals
- **Calendar Integration**: Different schedules for different days of the week

See [TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md) for detailed documentation.

## API Endpoints

The web interface provides a comprehensive REST API for programmatic control.

**Complete API documentation**: See **[API_REFERENCE.md](docs/API_REFERENCE.md)**

### Quick API Examples

**Get Status:**
```bash
curl http://YOUR_PI_IP:5000/api/status
```

**Create a Preset:**
```bash
curl -X POST http://YOUR_PI_IP:5000/api/presets \
  -H "Content-Type: application/json" \
  -d '{
    "preset_name": "blue_clock",
    "frame_config": {
      "type": "time",
      "colors": {"text": "#0000FF", "background": "#000000"}
    }
  }'
```

**Trigger Preset:**
```bash
curl -X POST http://YOUR_PI_IP:5000/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"action": "play_preset", "name": "blue_clock"}'
```

See **[API_REFERENCE.md](docs/API_REFERENCE.md)** for:
- Complete endpoint documentation
- Request/response schemas
- Integration examples (Python, JavaScript, curl)
- Error handling
- Rate limiting details

## ESP32 Integration

LumiGrid Display Nodes can integrate with ESP32 LED nodes for coordinated lighting systems.

**Complete integration guide**: See **[INTEGRATION.md](INTEGRATION.md)**

### Key Integration Features

- **UDP Synchronization** - Keep all nodes in perfect sync
- **Preset Schema Alignment** - Share compatible presets
- **Sensor Data Bridging** - ESP32 sensors → Pi displays
- **Coordinated Effects** - Synchronized animations and color schemes

### Quick Integration Example

**ESP32 sending sensor data to Display Node:**

```cpp
HTTPClient http;
http.begin("http://display-node:5000/api/sensor_update");
http.addHeader("Content-Type", "application/json");

String payload = "{\"node_id\":\"esp32-01\",\"readings\":{\"temperature\":23.5}}";
http.POST(payload);
```

See **[INTEGRATION.md](INTEGRATION.md)** for complete setup instructions and examples.

## Documentation

### 📱 In-App Documentation (Web UI)

**Access comprehensive documentation directly from the web interface:**

- **Help & Documentation** (`/help`) - Complete user and developer guides accessible from any device
  - Hardware requirements and OS setup
  - Installation and Setup Wizard guide
  - Web interface tutorials for each page
  - Troubleshooting common issues
  - API reference with examples
  - ESP32 integration guide
  - Project architecture and libraries
  
- **FAQ** (`/faq`) - Frequently asked questions organized by category
- **Setup Wizard** - Interactive first-run configuration assistant

👉 **Access the in-app documentation at:** `http://your-pi-ip:5000/help`

### 📚 User Guides

- **[Quick Start](QUICKSTART.md)** - Get started in minutes
- **[Hardware Setup](HARDWARE.md)** - Wiring diagrams and component details
- **[Configuration](CONFIGURATION.md)** - Complete config reference
- **[Timeline Sequencer](TIMELINE_SEQUENCER.md)** - Automated scheduling
- **[Style System](StyleSystem.md)** - Web UI customization

### 👨‍💻 Developer Documentation

- **[API Reference](docs/API_REFERENCE.md)** - REST API documentation
- **[UDP Synchronization](docs/UDP_SYNCHRONIZATION.md)** - Sync protocol details
- **[ESP32 Integration](INTEGRATION.md)** - LED node integration
- **[Contributing](CONTRIBUTING.md)** - Development guidelines
- **[Preset Examples](preset_examples/)** - Sample preset files

## API Endpoints

The web interface provides a REST API:

- `GET /api/status` - Get current system status
- `GET/POST /api/mode` - Get or set display mode
- `POST /api/message` - Send custom message
- `GET/POST /api/brightness` - Get or set brightness
- `GET /api/temperature` - Get current temperature
- `GET/POST /api/config` - Get or update configuration

## Troubleshooting

### LED Matrix not displaying
- Check power supply is adequate
- Verify GPIO connections
- Check hardware_mapping in config.yaml matches your panel
- Run with sudo for GPIO access
- If running without sudo, ensure `disable_hardware_pulsing: true` in config.yaml

### Color quality issues or flickering
- For best color quality and stability, run with `sudo`
- If running without sudo, set `disable_hardware_pulsing: true` in config.yaml
- Consider adding `isolcpus=3` to `/boot/cmdline.txt` and rebooting for better timing precision

### Brightness configuration issues
- If you see errors about brightness being outside the usable range, ensure `brightness` in `config.yaml` is set between 1-100
- Default value should be 50: `brightness: 50`

### Temperature sensor not reading
- For DS18B20: Check 1-Wire is enabled in /boot/config.txt
- For DHT: Verify correct GPIO pin in config.yaml
- Check physical connections and pull-up resistor
- Verify sensor with: `ls /sys/bus/w1/devices/` (DS18B20)

### RTC not working
- Verify I2C is enabled: `sudo i2cdetect -y 1`
- DS3231 should appear at address 0x68
- Check physical connections

### Web interface not accessible
- Check firewall settings
- Verify service is running: `sudo systemctl status lumigrid`
- Check logs for errors
- Try accessing from Pi locally: `http://localhost:5000`

## Development

### Project Structure
```
lumigrid_display_node/
├── lumigrid/
│   ├── __init__.py
│   ├── config.py           # Configuration management
│   ├── controller.py       # Application controller
│   ├── main.py            # Entry point (development)
│   ├── wsgi.py            # WSGI entry point (production)
│   ├── display/           # Display management
│   │   ├── matrix.py      # LED matrix driver
│   │   └── modes.py       # Display modes
│   ├── sensors/           # Sensor integrations
│   │   ├── temperature.py # Temperature sensors
│   │   └── rtc.py        # RTC management
│   └── web/              # Web interface
│       ├── app.py        # Flask application
│       ├── templates/    # HTML templates
│       └── static/       # CSS/JS assets
├── config.yaml.example    # Example configuration
├── requirements.txt       # Python dependencies
├── setup.py              # Package setup
├── install.sh            # Installation script
├── lumigrid.service      # Systemd service file
├── test_setup.py         # Setup verification utility
└── README.md            # This file
```

### Running in Development Mode

1. Create a virtual environment:
```bash
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip setuptools wheel
```

2. Install the RGB LED Matrix library (see Manual Installation above)

3. Install in development mode:
```bash
pip install -r requirements.txt
pip install -e .
```

4. Run with debug enabled:
```bash
# Edit config.yaml and set web.debug: true
python3 -m lumigrid.main
```

### Troubleshooting Installation

#### "externally-managed-environment" error
On Raspberry Pi OS Bookworm and later, Python packages must be installed in a virtual environment due to PEP 668. The installation script automatically creates and uses a virtual environment.

#### RGB LED Matrix compilation errors
Ensure you have all build dependencies installed:
```bash
sudo apt-get install -y build-essential cython3 python3-dev libgraphicsmagick++-dev
```

#### Missing core.cpp or compilation fails
Make sure you cloned the repository with `--recursive` flag to get all submodules:
```bash
git clone --recursive https://github.com/hzeller/rpi-rgb-led-matrix.git
```

## License

MIT License - See LICENSE file for details

## Documentation

- [UDP Synchronization Guide](docs/UDP_SYNCHRONIZATION.md) - Master/Slave sync setup and troubleshooting
- [Timeline Sequencer Guide](TIMELINE_SEQUENCER.md) - Schedule-based preset playback
- [API Reference](docs/API_REFERENCE.md) - Web API documentation

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Acknowledgments

- [hzeller's rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) - Excellent LED matrix library
- Flask - Web framework
- Adafruit - Sensor libraries

## Support

For issues, questions, or contributions, please visit:
https://github.com/algorhythmicdev/lumigrid_display_node