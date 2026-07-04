# Embedded Integration Guide

Implementation samples and best practices for integrating LumiGrid design tokens, APIs, and UX patterns into LED Node Board and Raspberry Pi nodes.

## Table of Contents

1. [LED Node Board Integration](#LED Node Board-integration)
2. [Raspberry Pi Integration](#raspberry-pi-integration)
3. [Design Token Mapping](#design-token-mapping)
4. [Web UI Implementation](#web-ui-implementation)
5. [API Server Examples](#api-server-examples)
6. [Cross-Platform Consistency](#cross-platform-consistency)

---

## LED Node Board Integration

### Development Environment

#### Prerequisites

- **ESP-IDF v5.1+** or **PlatformIO**
- **USB-to-Serial Driver** (CP2102, CH340, etc.)
- **Python 3.8+** (for esptool and build tools)

#### ESP-IDF Setup

```bash
# Install ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh LED Node Board

# Activate environment
. ./export.sh

# Verify installation
idf.py --version
```

#### PlatformIO Setup

```bash
# Install PlatformIO
pip install platformio

# Create new project
pio project init --board LED Node Boarddev

# Build and flash
pio run --target upload
```

### Project Structure

```
lumigrid-node-led/
├── main/
│   ├── main.c                  # Entry point
│   ├── config.h                # Configuration constants
│   ├── wifi.c / wifi.h         # Wi-Fi management
│   ├── http_server.c / .h      # REST API server
│   ├── effect_engine.c / .h    # LED effect rendering
│   ├── pwm_driver.c / .h       # Multi-channel PWM Driver PWM driver
│   ├── aled_driver.c / .h      # RMT addressable LED driver
│   ├── sync_protocol.c / .h    # UDP multicast sync
│   └── web_ui/                 # Embedded HTML/CSS/JS
│       ├── index.html
│       ├── styles.css
│       └── app.js
├── components/                 # Reusable libraries
│   ├── cJSON/
│   ├── littlefs/
│   └── Multi-channel PWM Driver/
├── CMakeLists.txt
└── sdkconfig
```

### Design Token Implementation (LED Node Board)

#### C Header File: `design_tokens.h`

```c
#ifndef DESIGN_TOKENS_H
#define DESIGN_TOKENS_H

#include <stdint.h>

// Brand colors (24-bit RGB)
#define COLOR_LED_CYAN      0x00D4FF
#define COLOR_LED_AMBER     0xFF9500
#define COLOR_LED_MAGENTA   0xFF2D55
#define COLOR_LED_CHERRY    0xFF3B30
#define COLOR_ACCENT        0x0071E3

// Neutral colors
#define COLOR_BG_PRIMARY    0x000000
#define COLOR_BG_SECONDARY  0x1D1D1F
#define COLOR_TEXT_PRIMARY  0xF5F5F7
#define COLOR_TEXT_MUTED    0xA1A1A6

// Glass effect alpha (0-255)
#define GLASS_BG_ALPHA      184  // 0.72 * 255
#define GLASS_BORDER_ALPHA  26   // 0.1 * 255

// Spacing tokens (pixels)
#define SPACING_XS          8
#define SPACING_SM          16
#define SPACING_MD          24
#define SPACING_LG          32
#define SPACING_XL          48

// Border radius (pixels)
#define RADIUS_SM           8
#define RADIUS_MD           12
#define RADIUS_LG           18
#define RADIUS_XL           24

// Animation durations (milliseconds)
#define DUR_FAST            150
#define DUR_MED             300
#define DUR_SLOW            500

// Halo opacity levels (0-255)
#define HALO_NONE           0
#define HALO_SUBTLE         15   // 0.06 * 255
#define HALO_SOFT           26   // 0.10 * 255
#define HALO_STRONG         41   // 0.16 * 255
#define HALO_VIVID          56   // 0.22 * 255

// Helper: Convert RGB to separate channels
#define RGB_RED(color)      ((color >> 16) & 0xFF)
#define RGB_GREEN(color)    ((color >> 8) & 0xFF)
#define RGB_BLUE(color)     (color & 0xFF)

// Helper: Pack separate RGB channels
#define RGB_PACK(r, g, b)   (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (b))

#endif // DESIGN_TOKENS_H
```

#### Usage in C Code

```c
#include "design_tokens.h"
#include "effect_engine.h"

void apply_brand_gradient() {
    // Extract RGB from tokens
    uint8_t r_start = RGB_RED(COLOR_LED_MAGENTA);
    uint8_t g_start = RGB_GREEN(COLOR_LED_MAGENTA);
    uint8_t b_start = RGB_BLUE(COLOR_LED_MAGENTA);
    
    uint8_t r_end = RGB_RED(COLOR_LED_CYAN);
    uint8_t g_end = RGB_GREEN(COLOR_LED_CYAN);
    uint8_t b_end = RGB_BLUE(COLOR_LED_CYAN);
    
    // Apply gradient across LED strip
    for (int i = 0; i < LED_COUNT; i++) {
        float t = (float)i / LED_COUNT;
        uint8_t r = r_start + t * (r_end - r_start);
        uint8_t g = g_start + t * (g_end - g_start);
        uint8_t b = b_start + t * (b_end - b_start);
        
        set_led_color(i, RGB_PACK(r, g, b));
    }
}
```

### HTTP Server (ESP-IDF)

#### Minimal REST API Server

```c
#include "esp_http_server.h"
#include "cJSON.h"

// GET /api/status handler
static esp_err_t status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "node_id", CONFIG_NODE_ID);
    cJSON_AddStringToObject(root, "role", "participant");
    cJSON_AddStringToObject(root, "mode", get_current_mode());
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "heap_free_kb", esp_get_free_heap_size() / 1024);
    cJSON_AddNumberToObject(root, "fps", get_current_fps());
    
    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/trigger handler
static esp_err_t trigger_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(json, "action"));
    
    if (strcmp(action, "blackout") == 0) {
        trigger_blackout();
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else if (strcmp(action, "play_preset") == 0) {
        const char *preset = cJSON_GetStringValue(cJSON_GetObjectItem(json, "preset"));
        play_preset(preset);
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// Register URI handlers
httpd_uri_t uri_status = {
    .uri       = "/api/status",
    .method    = HTTP_GET,
    .handler   = status_handler
};

httpd_uri_t uri_trigger = {
    .uri       = "/api/trigger",
    .method    = HTTP_POST,
    .handler   = trigger_handler
};

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_trigger);
    }
    
    return server;
}
```

### Embedded Web UI (LED Node Board)

Store HTML/CSS/JS in flash and serve via HTTP server:

#### Embed Files at Build Time

```c
// main/CMakeLists.txt
target_add_binary_data(main.elf "web_ui/index.html" TEXT)
target_add_binary_data(main.elf "web_ui/styles.css" TEXT)
target_add_binary_data(main.elf "web_ui/app.js" TEXT)
```

#### Declare External Symbols

```c
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[] asm("_binary_styles_css_end");
```

#### Serve Static Files

```c
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, 
                    index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t styles_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)styles_css_start, 
                    styles_css_end - styles_css_start);
    return ESP_OK;
}
```

#### Minimal Embedded HTML

```html
<!-- web_ui/index.html -->
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LumiGrid Node</title>
  <link rel="stylesheet" href="/styles.css">
</head>
<body>
  <div class="card" data-halo="2">
    <h2>LED Node Control</h2>
    <div id="status">Loading...</div>
    <button class="btn primary" onclick="playPreset('rainbow-chase')">Rainbow Chase</button>
    <button class="btn" onclick="blackout()">Blackout</button>
  </div>
  <script src="/app.js"></script>
</body>
</html>
```

#### Embedded CSS (Design Tokens)

```css
/* web_ui/styles.css */
:root {
  --bg: #050505;
  --ink: #f5f5f7;
  --accent-primary: #0071e3;
  --glass-bg: rgba(29, 29, 31, 0.72);
  --spacing-lg: 2rem;
  --radius-lg: 18px;
}

body {
  margin: 0;
  padding: 1rem;
  background: var(--bg);
  color: var(--ink);
  font-family: -apple-system, sans-serif;
}

.card {
  background: var(--glass-bg);
  border-radius: var(--radius-lg);
  padding: var(--spacing-lg);
  max-width: 400px;
  margin: 0 auto;
}

.btn {
  background: rgba(255,255,255,.08);
  color: var(--ink);
  border: 0;
  padding: 0.75rem 1rem;
  border-radius: 0.85rem;
  cursor: pointer;
  margin: 0.5rem 0.25rem;
}

.btn.primary {
  background: var(--accent-primary);
  color: white;
}
```

#### Embedded JavaScript

```javascript
// web_ui/app.js
async function getStatus() {
  const res = await fetch('/api/status');
  const data = await res.json();
  document.getElementById('status').textContent = 
    `Mode: ${data.mode}, FPS: ${data.fps}, Uptime: ${Math.floor(data.uptime_ms / 1000)}s`;
}

async function playPreset(preset) {
  await fetch('/api/trigger', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'play_preset', preset })
  });
  getStatus();
}

async function blackout() {
  await fetch('/api/trigger', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'blackout' })
  });
  getStatus();
}

// Refresh status every 2 seconds
setInterval(getStatus, 2000);
getStatus();
```

---

## Raspberry Pi Integration

### Development Environment

#### Prerequisites

- **Raspberry Pi 4 or 5** (2GB+ RAM recommended)
- **Raspberry Pi OS Lite** (Debian-based)
- **Python 3.11+** or **Node.js 20+**

#### Initial Setup

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Python dependencies
sudo apt install python3-pip python3-venv

# Install Node.js (if using Node)
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

### Python REST API Server (Flask)

#### Project Structure

```
lumigrid-hub-pi/
├── app.py                  # Main Flask application
├── config.py               # Configuration
├── api/
│   ├── __init__.py
│   ├── status.py           # Status endpoints
│   ├── trigger.py          # Trigger endpoints
│   └── presets.py          # Preset management
├── sync/
│   ├── __init__.py
│   └── multicast.py        # UDP multicast sync
├── static/
│   ├── styles.css          # Design tokens (same as SvelteKit)
│   └── app.js
├── templates/
│   └── index.html
└── requirements.txt
```

#### Design Tokens in Python

```python
# config.py - Design tokens as Python constants

class DesignTokens:
    """LumiGrid design tokens mirroring src/lib/tokens.ts"""
    
    # Brand colors
    COLOR_LED_CYAN = "#00d4ff"
    COLOR_LED_AMBER = "#ff9500"
    COLOR_LED_MAGENTA = "#ff2d55"
    COLOR_LED_CHERRY = "#ff3b30"
    COLOR_ACCENT_PRIMARY = "#0071e3"
    
    # Neutral colors
    COLOR_BG = "#050505"
    COLOR_INK = "#f5f5f7"
    COLOR_MUTED = "#a1a1a6"
    
    # Spacing (in pixels or rem)
    SPACING_XS = "0.5rem"
    SPACING_SM = "1rem"
    SPACING_MD = "1.5rem"
    SPACING_LG = "2rem"
    SPACING_XL = "3rem"
    
    # Border radius
    RADIUS_SM = "8px"
    RADIUS_MD = "12px"
    RADIUS_LG = "18px"
    RADIUS_XL = "24px"
    
    # Animation durations (milliseconds)
    DUR_FAST = 150
    DUR_MED = 300
    DUR_SLOW = 500
    
    @classmethod
    def to_css_vars(cls):
        """Convert to CSS custom properties"""
        return {
            '--accent-primary': cls.COLOR_ACCENT_PRIMARY,
            '--led-cyan': cls.COLOR_LED_CYAN,
            '--led-amber': cls.COLOR_LED_AMBER,
            '--led-magenta': cls.COLOR_LED_MAGENTA,
            '--led-cherry': cls.COLOR_LED_CHERRY,
            '--bg': cls.COLOR_BG,
            '--ink': cls.COLOR_INK,
            '--muted': cls.COLOR_MUTED,
        }
```

#### Flask Application

```python
# app.py
from flask import Flask, jsonify, request, render_template
import time
import psutil
from config import DesignTokens

app = Flask(__name__)

# Node state
node_state = {
    'node_id': 'hub-001',
    'role': 'master',
    'mode': 'idle',
    'start_time': time.time()
}

@app.route('/')
def index():
    """Serve web UI with design tokens"""
    css_vars = DesignTokens.to_css_vars()
    return render_template('index.html', css_vars=css_vars)

@app.route('/api/status')
def status():
    """Get node status"""
    uptime_s = time.time() - node_state['start_time']
    memory = psutil.virtual_memory()
    
    return jsonify({
        'node_id': node_state['node_id'],
        'role': node_state['role'],
        'mode': node_state['mode'],
        'uptime_ms': int(uptime_s * 1000),
        'heap_free_kb': memory.available // 1024,
        'cpu_percent': psutil.cpu_percent(),
        'temperature_celsius': get_cpu_temperature()
    })

@app.route('/api/trigger', methods=['POST'])
def trigger():
    """Execute action"""
    data = request.json
    action = data.get('action')
    
    if action == 'blackout':
        node_state['mode'] = 'blackout'
        # Execute blackout logic
        return jsonify({'success': True})
    
    elif action == 'play_preset':
        preset = data.get('preset')
        node_state['mode'] = 'playing'
        # Load and play preset
        return jsonify({'success': True, 'preset': preset})
    
    else:
        return jsonify({'success': False, 'error': 'unknown_action'}), 400

def get_cpu_temperature():
    """Read Raspberry Pi CPU temperature"""
    try:
        with open('/sys/class/thermal/thermal_zone0/temp', 'r') as f:
            temp = float(f.read()) / 1000.0
            return round(temp, 1)
    except:
        return 0.0

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=80, debug=False)
```

#### Requirements File

```txt
# requirements.txt
Flask==3.0.0
psutil==5.9.6
paho-mqtt==1.6.1
```

#### Run as Service

```bash
# Create systemd service
sudo nano /etc/systemd/system/lumigrid-hub.service
```

```ini
[Unit]
Description=LumiGrid Hub Node
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/lumigrid-hub-pi
ExecStart=/home/pi/lumigrid-hub-pi/venv/bin/python app.py
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

```bash
# Enable and start service
sudo systemctl enable lumigrid-hub.service
sudo systemctl start lumigrid-hub.service
sudo systemctl status lumigrid-hub.service
```

---

## Design Token Mapping

### Token Synchronization Strategy

1. **Source of Truth**: `src/lib/tokens.ts` (landing repo)
2. **CSS Mirror**: `src/lib/styles.css` (auto-synced via script)
3. **LED Node Board Mirror**: Manual sync to `design_tokens.h`
4. **Python Mirror**: Manual sync to `config.py`

### Sync Script (Node.js)

```javascript
// scripts/sync-tokens-embedded.mjs
import fs from 'fs';
import { tokens } from '../src/lib/tokens.ts';

// Generate C header
function generateCHeader() {
  let header = '#ifndef DESIGN_TOKENS_H\n#define DESIGN_TOKENS_H\n\n';
  
  // Colors
  header += '// Brand colors (24-bit RGB)\n';
  header += `#define COLOR_LED_CYAN      0x${tokens.colors.brand.ledCyan.slice(1).toUpperCase()}\n`;
  header += `#define COLOR_LED_AMBER     0x${tokens.colors.brand.ledAmber.slice(1).toUpperCase()}\n`;
  // ... more colors
  
  header += '\n#endif // DESIGN_TOKENS_H\n';
  return header;
}

// Generate Python module
function generatePython() {
  let python = 'class DesignTokens:\n';
  python += `    COLOR_LED_CYAN = "${tokens.colors.brand.ledCyan}"\n`;
  python += `    COLOR_LED_AMBER = "${tokens.colors.brand.ledAmber}"\n`;
  // ... more tokens
  return python;
}

fs.writeFileSync('embedded/LED Node Board/design_tokens.h', generateCHeader());
fs.writeFileSync('embedded/python/design_tokens.py', generatePython());
console.log('✅ Embedded tokens synced');
```

---

## Cross-Platform Consistency

### Visual Parity Checklist

- [ ] **Colors**: Same hex values across web, LED Node Board, Pi
- [ ] **Spacing**: Proportional scaling (adjust for screen DPI)
- [ ] **Typography**: System fonts on all platforms
- [ ] **Glassmorphism**: Blur effects (web only, simulate with gradients on embedded)
- [ ] **Halo Effects**: LED glow levels consistent
- [ ] **Animations**: Same durations, respect reduced-motion

### Testing Matrix

| Feature | SvelteKit (Web) | LED Node Board | Raspberry Pi |
|---------|----------------|-------|--------------|
| Color Tokens | ✅ CSS vars | ✅ C defines | ✅ Python constants |
| REST API | ✅ SvelteKit | ✅ esp_http_server | ✅ Flask |
| MQTT | ✅ Optional | ✅ esp-mqtt | ✅ paho-mqtt |
| Web UI | ✅ Svelte | ✅ Embedded HTML | ✅ Jinja templates |
| Glassmorphism | ✅ Full | ⚠️ Simulated | ✅ Full (if GUI) |
| Responsive | ✅ Mobile-first | ⚠️ Small screens | ✅ Desktop UI |

---

## Additional Resources

- **ESP-IDF Documentation**: [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/)
- **Raspberry Pi Documentation**: [raspberrypi.com/documentation](https://www.raspberrypi.com/documentation/)
- **Design Tokens Spec**: [design-tokens.github.io](https://design-tokens.github.io/)
- **LumiGrid API Reference**: [API_REFERENCE.md](./API_REFERENCE.md)

---

**Embedded Integration Guide** — Bring the LumiGrid design language to every device in your network.
