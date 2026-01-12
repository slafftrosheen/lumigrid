# LumiGrid Display Node Configuration Guide

Complete reference for all configuration options in `config.yaml`.

## Table of Contents

- [Configuration File Location](#configuration-file-location)
- [LED Matrix Settings](#led-matrix-settings)
- [Display Settings](#display-settings)
- [Frame System](#frame-system)
- [Font Settings](#font-settings)
- [Temperature Sensor](#temperature-sensor)
- [RTC Settings](#rtc-settings)
- [Web Interface](#web-interface)
- [Network Settings](#network-settings)
- [Control Mode](#control-mode)
- [UDP Synchronization](#udp-synchronization)
- [Timeline Sequencer](#timeline-sequencer)
- [Logging](#logging)
- [Best Practices](#best-practices)

## Configuration File Location

**Default location**: `config.yaml` in the project root directory

**Specify custom location** via environment variable:

```bash
export LUMIGRID_CONFIG=/path/to/custom/config.yaml
```

## LED Matrix Settings

Configure your RGB LED matrix panel hardware.

```yaml
led_matrix:
  # Panel dimensions
  rows: 32                      # Height of single panel (8-128)
  cols: 64                      # Width of single panel (8-128)
  chain_length: 1               # Number of daisy-chained panels
  parallel: 1                   # Number of parallel chains (1-6)
  
  # Hardware configuration
  hardware_mapping: "regular"   # regular, adafruit-hat, adafruit-hat-pwm, 
                                # compute-module, regular-pi1
  rgb_sequence: "RGB"           # Color channel order: RGB, RBG, GRB, GBR, BGR, BRG
  row_address_type: 0           # 0=direct, 1=AB-addressed, 2=direct row, 3=ABC-addressed
  multiplexing: 0               # Panel multiplexing type (0-18)
  panel_type: ""                # Special panel type (e.g., "FM6126A")
  pixel_mapper_config: ""       # Pixel remapping config string
  
  # PWM settings
  pwm_bits: 11                  # Color depth (1-11)
                                # Lower = faster refresh, less colors
                                # Higher = more colors, slower refresh
  pwm_lsb_nanoseconds: 130      # PWM base time (50-500ns)
  pwm_dither_bits: 0            # Temporal dithering (0-2)
  
  # Display quality
  brightness: 50                # Brightness (0-100)
  scan_mode: 0                  # 0=progressive, 1=interlaced
  gpio_slowdown: 4              # GPIO slowdown (0-5)
                                # Increase for flickering, decrease for speed
  limit_refresh_rate_hz: 0      # Max refresh rate (0=unlimited)
  
  # Advanced options
  show_refresh_rate: false      # Show refresh rate in console
  inverse_colors: false         # Invert all colors
  disable_hardware_pulsing: false  # Use software PWM (allows non-root)
  drop_privileges: true         # Drop root after GPIO init
```

### Hardware Mapping Values

- `regular` - Standard wiring (most common)
- `adafruit-hat` - Adafruit RGB Matrix HAT
- `adafruit-hat-pwm` - Adafruit HAT with hardware PWM
- `compute-module` - For Raspberry Pi Compute Module
- `regular-pi1` - For original Raspberry Pi Model 1

### Common Panel Sizes

| Panel Size | Rows | Cols | Typical Use Case |
|------------|------|------|------------------|
| 16x32      | 16   | 32   | Small displays   |
| 32x32      | 32   | 32   | Square displays  |
| 32x64      | 32   | 64   | Widescreen (most common) |
| 64x64      | 64   | 64   | Large square     |

### PWM Bits Trade-off

| PWM Bits | Colors | Refresh Rate | Best For |
|----------|--------|--------------|----------|
| 1        | 8      | Very fast    | Simple displays |
| 6        | 262K   | Fast         | Basic color |
| 11       | 2M+    | Standard     | Full color (recommended) |

## Display Settings

General display configuration.

```yaml
display:
  default_mode: "frames"        # frames, clock, temperature, message, slideshow
  rotation: 0                   # Display rotation: 0, 90, 180, 270
  font_path: "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
  slideshow_interval: 10        # Seconds between slides (legacy mode)
  scroll_speed: 0.05            # Seconds per pixel for scrolling text
```

### Display Modes

- `frames` - Use frame system (recommended)
- `clock` - Simple clock display
- `temperature` - Temperature sensor display
- `message` - Static message display
- `slideshow` - Auto-cycle through modes

## Frame System

Configure the frame-based display system.

```yaml
frames:
  enabled: true
  default_duration: 5           # Default seconds per frame
  transition_effect: "fade"     # fade, slide, instant (future)
  queue:
    - id: "time-display"        # Unique frame ID
      type: "time"              # Frame type
      name: "Current Time"      # Display name
      enabled: true             # Whether frame is active
      duration: 5               # Seconds to display
      font:
        path: "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
        size: 16
      colors:
        text: "#00FFFF"
        background: "#000000"
        accent: "#0088CC"       # Optional accent color
      position:
        x: "center"             # center, left, right, or pixel value
        y: "center"             # center, top, bottom, or pixel value
      format: "%H:%M:%S"        # Time format string
    
    - id: "temp-display"
      type: "temperature"
      name: "Temperature"
      enabled: true
      duration: 5
      font:
        size: 14
      colors:
        text: "#FF8000"
        background: "#000000"
      position:
        x: "center"
        y: "center"
      unit: "celsius"           # celsius or fahrenheit
      show_icon: true           # Show temperature icon
```

### Frame Types

| Type | Description | Required Fields |
|------|-------------|-----------------|
| `time` | Clock display | `format` |
| `date` | Date display | `format` |
| `datetime` | Date and time | `format` |
| `temperature` | Temperature | `unit` |
| `humidity` | Humidity | - |
| `text` | Static text | `text` |
| `message` | Scrolling text | `text`, `scroll` |

### Position Values

- **String values**: `center`, `left`, `right`, `top`, `bottom`
- **Numeric values**: Pixel coordinates (can be negative)

## Font Settings

Configure font directories and defaults.

```yaml
fonts:
  system_fonts_dir: "/usr/share/fonts/truetype"
  custom_fonts_dir: "fonts"     # Relative to project root
  default_font: "DejaVuSans.ttf"
  default_size: 12
```

### Supported Font Formats

- `.ttf` - TrueType fonts
- `.otf` - OpenType fonts
- `.bdf` - Bitmap fonts (limited support)

## Temperature Sensor

Configure temperature/humidity sensors.

```yaml
temperature:
  enabled: true
  sensor_type: "DS18B20"        # DS18B20, DHT22, DHT11
  pin: 4                        # GPIO pin (for DHT sensors)
  unit: "celsius"               # celsius or fahrenheit
  update_interval: 5            # Seconds between readings
  calibration_offset: 0.0       # Temperature offset for calibration
```

### Sensor Types

**DS18B20** (1-Wire):
```yaml
temperature:
  enabled: true
  sensor_type: "DS18B20"
  unit: "celsius"
```

**DHT22** (Digital):
```yaml
temperature:
  enabled: true
  sensor_type: "DHT22"
  pin: 4
  unit: "celsius"
```

**DHT11** (Digital):
```yaml
temperature:
  enabled: true
  sensor_type: "DHT11"
  pin: 4
  unit: "celsius"
```

## RTC Settings

Real-Time Clock configuration for accurate timekeeping.

```yaml
rtc:
  enabled: true
  type: "DS3231"                # DS3231 or DS1307
  sync_interval: 3600           # Sync system time every N seconds
  i2c_bus: 1                    # I2C bus number
  i2c_address: 0x68             # I2C address (usually 0x68)
```

### RTC Module Comparison

| Module | Accuracy | Battery | Built-in Temp |
|--------|----------|---------|---------------|
| DS3231 | ±2ppm    | Yes     | Yes           |
| DS1307 | ±50ppm   | Yes     | No            |

**Recommendation**: DS3231 for better accuracy

## Web Interface

Configure the web server and API.

```yaml
web:
  host: "0.0.0.0"               # Listen address (0.0.0.0 = all interfaces)
  port: 5000                    # Web server port
  debug: false                  # Enable Flask debug mode
  secret_key: "change-me"       # Session secret (change in production!)
  
  # Rate limiting
  rate_limiting:
    enabled: true
    default_limit: "200 per day"
    api_limit: "50 per hour"
```

### Security Recommendations

1. **Change `secret_key`** in production
2. **Enable HTTPS** with reverse proxy (nginx, Apache)
3. **Restrict `host`** to specific interface if needed
4. **Enable rate limiting** to prevent abuse
5. **Use firewall** to restrict access

## Network Settings

Network and service discovery configuration.

```yaml
network:
  hostname: "lumigrid-01"       # Network hostname
  mdns_enabled: true            # Enable mDNS/Bonjour discovery
  mdns_service_name: "LumiGrid Display"
  interfaces:
    - "eth0"                    # List of network interfaces to use
    - "wlan0"
```

### mDNS Service Discovery

When enabled, the display announces itself as:
- Service type: `_lumigrid._tcp`
- Name: Configured `mdns_service_name`
- Port: Web server port

Access via: `http://lumigrid-01.local:5000`

## Control Mode

Configure how the display is controlled.

```yaml
control:
  mode: "independent"           # independent, master, slave
  sync_role: null               # null, master, slave
  external_source: null         # null, udp, mqtt, http
```

### Control Modes

**Independent**:
```yaml
control:
  mode: "independent"
  sync_role: null
  external_source: null
```
Display operates standalone.

**Master** (broadcasts sync):
```yaml
control:
  mode: "master"
  sync_role: "master"
  external_source: null
```

**Slave** (syncs to master):
```yaml
control:
  mode: "slave"
  sync_role: "slave"
  external_source: "udp"
```

## UDP Synchronization

Configure UDP multicast synchronization.

```yaml
sync:
  enabled: true
  multicast_group: "239.10.7.42"  # Multicast IP
  port: 45454                     # UDP port
  broadcast_rate_hz: 20           # Master broadcast rate (Hz)
  timeout_ms: 1000                # Slave timeout (milliseconds)
  drift_threshold_ms: 50          # Alert if drift exceeds this
```

See [UDP_SYNCHRONIZATION.md](docs/UDP_SYNCHRONIZATION.md) for details.

## Timeline Sequencer

Schedule automated preset playback.

```yaml
timelines:
  - id: "daily-schedule"
    name: "Daily Schedule"
    description: "Automated daily display rotation"
    enabled: true
    loop: true                    # Repeat timeline continuously
    entries:
      - id: "morning-clock"
        preset_name: "Current Time"
        trigger_type: "time_of_day"  # time_of_day, interval, datetime, manual
        time_of_day: "06:00:00"   # HH:MM:SS format
        days_of_week: [0, 1, 2, 3, 4, 5, 6]  # 0=Monday, 6=Sunday
        duration: 0               # 0 = until next entry
        enabled: true
        
        # Optional calendar features
        valid_from: "2024-01-01"  # YYYY-MM-DD
        valid_until: "2024-12-31"
        start_time: "06:00:00"    # Time window start
        end_time: "22:00:00"      # Time window end
        exception_dates:          # Skip these dates
          - "2024-12-25"
          - "2024-01-01"
```

### Trigger Types

- `time_of_day` - Trigger at specific time daily
- `interval` - Trigger every N seconds
- `datetime` - Trigger at specific date/time
- `manual` - Manual trigger only

See [TIMELINE_SEQUENCER.md](TIMELINE_SEQUENCER.md) for complete guide.

## Logging

Configure application logging.

```yaml
logging:
  level: "INFO"                 # DEBUG, INFO, WARNING, ERROR, CRITICAL
  file: "logs/lumigrid.log"     # Log file path
  max_bytes: 10485760           # Max log file size (10MB)
  backup_count: 5               # Number of backup logs to keep
  format: "%(asctime)s - %(name)s - %(levelname)s - %(message)s"
  
  # Per-module log levels
  modules:
    lumigrid.sync: "DEBUG"
    lumigrid.web: "INFO"
```

### Log Levels

- `DEBUG` - Detailed diagnostic info
- `INFO` - General informational messages
- `WARNING` - Warning messages
- `ERROR` - Error messages
- `CRITICAL` - Critical failures

## Best Practices

### Performance Optimization

```yaml
led_matrix:
  brightness: 50                # Start at 50%, adjust as needed
  gpio_slowdown: 4              # Increase if flickering
  pwm_bits: 11                  # Balance quality vs speed
  disable_hardware_pulsing: false  # Only if running without root

display:
  scroll_speed: 0.05            # Adjust for smooth scrolling
```

### Reliable Operation

```yaml
rtc:
  enabled: true                 # Use RTC for offline operation
  type: "DS3231"                # Use DS3231 for accuracy

logging:
  level: "INFO"                 # Use INFO in production
  max_bytes: 10485760           # Rotate logs to save space
  backup_count: 5

control:
  mode: "independent"           # Start independent, add sync later
```

### Development vs Production

**Development**:
```yaml
web:
  debug: true
  
logging:
  level: "DEBUG"
```

**Production**:
```yaml
web:
  debug: false
  secret_key: "randomly-generated-secret"
  rate_limiting:
    enabled: true

logging:
  level: "INFO"
```

### Multi-Node Setup

**Master Node**:
```yaml
network:
  hostname: "lumigrid-master"

control:
  mode: "master"
  sync_role: "master"

sync:
  enabled: true
  broadcast_rate_hz: 20
```

**Slave Nodes**:
```yaml
network:
  hostname: "lumigrid-slave-01"  # Unique per node

control:
  mode: "slave"
  sync_role: "slave"
  external_source: "udp"

sync:
  enabled: true
  timeout_ms: 1000
```

## Configuration Validation

Validate your configuration:

```bash
python -m lumigrid.config --validate config.yaml
```

## Environment Variables

Override config values with environment variables:

```bash
# Set config file location
export LUMIGRID_CONFIG=/etc/lumigrid/config.yaml

# Override web port
export LUMIGRID_WEB_PORT=8080

# Override brightness
export LUMIGRID_BRIGHTNESS=75
```

## Example Configurations

### Minimal Configuration

```yaml
led_matrix:
  rows: 32
  cols: 64
  brightness: 50

display:
  default_mode: "frames"

web:
  host: "0.0.0.0"
  port: 5000
```

### Complete Configuration

See `config.yaml.example` for a complete configuration template with all available options.

## Troubleshooting Configuration

**Config file not found**:
```bash
# Check current directory
ls -la config.yaml

# Specify path
export LUMIGRID_CONFIG=/full/path/to/config.yaml
```

**Invalid YAML syntax**:
```bash
# Validate YAML
python -c "import yaml; yaml.safe_load(open('config.yaml'))"
```

**Permission errors**:
```bash
# Check file permissions
ls -la config.yaml

# Fix permissions
chmod 644 config.yaml
```

## Additional Resources

- [Quick Start Guide](QUICKSTART.md)
- [API Reference](docs/API_REFERENCE.md)
- [Hardware Setup](HARDWARE.md)
- [Integration Guide](INTEGRATION.md)

---

For questions or issues with configuration, visit:
https://github.com/algorhythmicdev/lumigrid_display_node/issues
