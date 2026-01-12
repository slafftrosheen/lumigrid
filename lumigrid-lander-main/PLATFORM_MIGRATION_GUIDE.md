# ESP32 and Raspberry Pi Token Migration Guide

This guide provides step-by-step instructions for implementing the LumiGrid design token system on ESP32 LED Nodes and Raspberry Pi Gateway/Hub devices.

---

## Table of Contents

1. [ESP32 LED Node Implementation](#esp32-led-node-implementation)
2. [Raspberry Pi Gateway Implementation](#raspberry-pi-gateway-implementation)
3. [Token Synchronization](#token-synchronization)
4. [Testing & Validation](#testing--validation)
5. [Troubleshooting](#troubleshooting)

---

## ESP32 LED Node Implementation

### Prerequisites

- ESP-IDF v5.x development environment
- LumiGrid LED Node firmware repository
- 4KB+ available flash memory for web UI

### Step 1: Extract Minimal Token Subset

The ESP32 has flash memory constraints, so we use a minimal token subset (~0.65KB).

**Run the token sync script:**
```bash
cd /path/to/lumigrid-lander
npm run sync:tokens
```

This generates `TOKEN_SYNC_REPORT.md` with the ESP32 minimal token subset.

**Minimal tokens included:**
- Core colors: `--bg-0`, `--bg-1`, `--ink`, `--muted`
- Accent: `--accent-primary`
- LED colors: `--cya`, `--mag`, `--amb`, `--cherry`
- Glass effects: `--glass-bg`, `--glass-border`
- Motion: `--dur-med`, `--ease-default`
- Typography: `--fs-body`, `--fs-caption`, `--font-body`
- Spacing: `--spacing-mobile-sm`, `--spacing-mobile-md`
- Touch: `--touch-target-min`

### Step 2: Create ESP32 Styles Header

Create `main/web/lumigrid_tokens.h` in your ESP32 project:

```c
#ifndef LUMIGRID_TOKENS_H
#define LUMIGRID_TOKENS_H

/**
 * LumiGrid Minimal Design Tokens for ESP32
 * Auto-generated from lumigrid-lander repository
 * 
 * Size: ~0.65 KB minified
 * Flash usage: Approximately 1KB with HTTP overhead
 */

static const char* LUMIGRID_MINIMAL_CSS = 
":root{"
"--bg-0:#000000;"
"--bg-1:#1d1d1f;"
"--bg:#050505;"
"--ink:#f5f5f7;"
"--muted:#a1a1a6;"
"--accent-primary:#0071e3;"
"--cya:#00d4ff;"
"--mag:#ff2d55;"
"--amb:#ff9500;"
"--cherry:#ff3b30;"
"--glass-bg:rgba(29,29,31,.72);"
"--glass-border:rgba(255,255,255,.1);"
"--dur-med:300ms;"
"--ease-default:cubic-bezier(.4,0,.2,1);"
"--fs-body:1rem;"
"--fs-caption:0.875rem;"
"--font-body:-apple-system,BlinkMacSystemFont,sans-serif;"
"--spacing-mobile-sm:1rem;"
"--spacing-mobile-md:1.5rem;"
"--touch-target-min:44px;"
"}"
"@media(prefers-reduced-motion:reduce){:root{--dur-med:0ms;}}";

/* Base component styles for ESP32 */
static const char* LUMIGRID_COMPONENT_CSS =
"body{"
"margin:0;"
"padding:1rem;"
"background:var(--bg-0);"
"color:var(--ink);"
"font-family:var(--font-body);"
"font-size:var(--fs-body);"
"}"
".card{"
"background:var(--glass-bg);"
"border:1px solid var(--glass-border);"
"border-radius:12px;"
"padding:1.5rem;"
"margin-bottom:1rem;"
"}"
".btn{"
"display:inline-block;"
"padding:.65rem 1rem;"
"background:var(--accent-primary);"
"color:#fff;"
"border:0;"
"border-radius:.85rem;"
"cursor:pointer;"
"min-height:var(--touch-target-min);"
"transition:transform var(--dur-med) var(--ease-default);"
"}"
".btn:active{"
"transform:translateY(2px);"
"}";

#endif // LUMIGRID_TOKENS_H
```

### Step 3: Integrate into HTTP Server

Update your HTTP server handler:

```c
#include "lumigrid_tokens.h"

static const char* html_head = 
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LumiGrid LED Node</title>"
"<style>";

static const char* html_head_close = 
"</style>"
"</head>"
"<body>";

static const char* html_footer = 
"</body></html>";

esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    
    // Send HTML head
    httpd_resp_sendstr_chunk(req, html_head);
    
    // Send CSS tokens
    httpd_resp_sendstr_chunk(req, LUMIGRID_MINIMAL_CSS);
    httpd_resp_sendstr_chunk(req, LUMIGRID_COMPONENT_CSS);
    
    // Close head, open body
    httpd_resp_sendstr_chunk(req, html_head_close);
    
    // Send content
    httpd_resp_sendstr_chunk(req, 
        "<div class='card'>"
        "  <h2>LED Node Control</h2>"
        "  <p style='color:var(--muted)'>Status: <span id='status'>Active</span></p>"
        "  <button class='btn' onclick='toggleLED()'>Toggle LEDs</button>"
        "</div>");
    
    // Send footer
    httpd_resp_sendstr_chunk(req, html_footer);
    httpd_resp_sendstr_chunk(req, NULL); // End chunked response
    
    return ESP_OK;
}
```

### Step 4: Test on Hardware

**Flash and test:**
```bash
idf.py build flash monitor
```

**Verify:**
1. Connect to LED Node's IP in browser
2. Check that styles render correctly
3. Verify touch targets are 44px minimum
4. Test reduced-motion on supported browsers
5. Measure flash usage: `idf.py size`

### Step 5: Optimize for Production

**Compression:**
```c
// Enable GZIP compression in ESP-IDF menuconfig
// Component config -> HTTP Server -> Enable GZIP compression

httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
```

**Caching:**
```c
// Add cache headers for static styles
httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
```

---

## Raspberry Pi Gateway Implementation

### Prerequisites

- Raspberry Pi (3B+ or newer recommended)
- Python 3.8+
- Flask web framework
- LumiGrid Pi Gateway repository

### Step 1: Set Up Flask Application

**Create directory structure:**
```bash
mkdir -p ~/lumigrid-gateway/{static/css,templates}
cd ~/lumigrid-gateway
```

**Create virtual environment:**
```bash
python3 -m venv venv
source venv/bin/activate
pip install flask
```

### Step 2: Copy Full Style System

**Copy from lumigrid-lander repository:**
```bash
# From lumigrid-lander repo
cp src/lib/styles.css ~/lumigrid-gateway/static/css/lumigrid-styles.css

# Copy tokens for Python usage
cp src/lib/tokens.ts ~/lumigrid-gateway/static/js/tokens.js
```

### Step 3: Create Python Token Dictionary

**Create `tokens.py`:**
```python
"""
LumiGrid Design Tokens for Python/Flask
Synchronized with lumigrid-lander/src/lib/tokens.ts
"""

TOKENS = {
    'colors': {
        'brand': {
            'primary': '#0071e3',
            'ledCyan': '#00d4ff',
            'ledAmber': '#ff9500',
            'ledMagenta': '#ff2d55',
            'ledCherry': '#ff3b30',
        },
        'neutral': {
            'bg': {
                'primary': '#000000',
                'secondary': '#1d1d1f',
                'tertiary': '#2d2d2f',
                'base': '#050505',
            },
            'text': {
                'primary': '#f5f5f7',
                'secondary': '#a1a1a6',
                'tertiary': '#6e6e73',
            },
        },
        'glass': {
            'bg': 'rgba(29, 29, 31, 0.72)',
            'border': 'rgba(255, 255, 255, 0.1)',
        },
    },
    'spacing': {
        'xs': '0.5rem',
        'sm': '1rem',
        'md': '1.5rem',
        'lg': '2rem',
        'xl': '3rem',
        'xxl': '4rem',
    },
    'radius': {
        'sm': '8px',
        'md': '12px',
        'lg': '18px',
        'xl': '24px',
        'full': '9999px',
    },
}

def get_token(path):
    """
    Get token value by dot-notation path
    Example: get_token('colors.brand.primary') -> '#0071e3'
    """
    keys = path.split('.')
    value = TOKENS
    for key in keys:
        value = value.get(key)
        if value is None:
            return None
    return value
```

### Step 4: Create Base Template

**Create `templates/base.html`:**
```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{% block title %}LumiGrid Gateway{% endblock %}</title>
    <link rel="stylesheet" href="{{ url_for('static', filename='css/lumigrid-styles.css') }}">
    {% block extra_css %}{% endblock %}
</head>
<body>
    <a href="#main" class="skip-link">Skip to main content</a>
    
    <header class="header" role="banner">
        <div class="container" style="display: flex; justify-content: space-between; align-items: center; padding: 1rem 0;">
            <div style="display: flex; align-items: center; gap: 0.75rem;">
                <div class="brand-mark" aria-hidden="true"></div>
                <span class="brand-label" style="font-weight: 600; font-size: 1.1rem;">LumiGrid Gateway</span>
            </div>
            <nav aria-label="Main navigation">
                <a href="/" class="btn ghost">Dashboard</a>
                <a href="/nodes" class="btn ghost">Nodes</a>
                <a href="/settings" class="btn ghost">Settings</a>
            </nav>
        </div>
    </header>

    <main id="main" role="main">
        {% block content %}{% endblock %}
    </main>

    <footer class="site-footer" role="contentinfo">
        <div class="container footer-grid">
            <div>
                <p style="margin: 0; color: var(--muted);">
                    LumiGrid Gateway &copy; 2026
                </p>
            </div>
        </div>
    </footer>

    {% block extra_js %}{% endblock %}
</body>
</html>
```

### Step 5: Create Dashboard

**Create `templates/dashboard.html`:**
```html
{% extends "base.html" %}

{% block title %}Dashboard - LumiGrid Gateway{% endblock %}

{% block content %}
<div class="container">
    <section class="hero center">
        <h1>LumiGrid Network</h1>
        <p class="lead">{{ node_count }} nodes connected</p>
    </section>

    <section class="section">
        <h2>Active Nodes</h2>
        <div class="grid two">
            {% for node in nodes %}
            <article class="card" data-halo="2">
                <h3>{{ node.name }}</h3>
                <p style="color: var(--muted);">IP: {{ node.ip }}</p>
                <div class="pills">
                    <span class="pill" aria-selected="true" 
                          style="background: {% if node.status == 'active' %}rgba(0, 212, 255, 0.2){% else %}rgba(255, 255, 255, 0.06){% endif %}">
                        {{ node.status }}
                    </span>
                </div>
                <a href="http://{{ node.ip }}" class="btn primary" target="_blank" rel="noopener">
                    Open Control Panel
                </a>
            </article>
            {% endfor %}
        </div>
    </section>
</div>
{% endblock %}
```

### Step 6: Create Flask Application

**Create `app.py`:**
```python
from flask import Flask, render_template
from tokens import TOKENS, get_token

app = Flask(__name__)

# Make tokens available to all templates
@app.context_processor
def inject_tokens():
    return {'tokens': TOKENS, 'get_token': get_token}

@app.route('/')
def dashboard():
    # Mock data - replace with actual node discovery
    nodes = [
        {'name': 'LED Node 1', 'status': 'active', 'ip': '192.168.1.101'},
        {'name': 'LED Node 2', 'status': 'active', 'ip': '192.168.1.102'},
        {'name': 'LED Node 3', 'status': 'offline', 'ip': '192.168.1.103'},
    ]
    
    return render_template('dashboard.html', 
                         nodes=nodes, 
                         node_count=len([n for n in nodes if n['status'] == 'active']))

@app.route('/nodes')
def nodes():
    return render_template('nodes.html')

@app.route('/settings')
def settings():
    return render_template('settings.html')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
```

### Step 7: Run and Test

**Start the server:**
```bash
python app.py
```

**Access:**
```
http://raspberrypi.local:5000
# or
http://192.168.1.x:5000
```

**Verify:**
1. All cards use glassmorphism
2. Buttons have proper hover effects
3. Grid layouts are responsive
4. Touch targets are 44px minimum
5. Reduced-motion works

---

## Token Synchronization

### Automated Workflow

**Run token sync before platform updates:**
```bash
cd /path/to/lumigrid-lander
npm run sync:tokens
```

**Review the report:**
```bash
cat TOKEN_SYNC_REPORT.md
```

**Update ESP32:**
1. Copy ESP32 token subset from report
2. Update `lumigrid_tokens.h`
3. Rebuild and flash firmware

**Update Pi Gateway:**
1. Copy updated `styles.css` to `static/css/`
2. Update `tokens.py` with new values
3. Restart Flask application

### Manual Synchronization Checklist

When tokens change in `lumigrid-lander`:

- [ ] Update `src/lib/tokens.ts`
- [ ] Update `src/lib/styles.css`
- [ ] Run `npm run sync:tokens`
- [ ] Review `TOKEN_SYNC_REPORT.md`
- [ ] Update ESP32 `lumigrid_tokens.h`
- [ ] Update Pi `tokens.py`
- [ ] Copy `styles.css` to Pi gateway
- [ ] Update `StyleSystem.md` documentation
- [ ] Test on all platforms
- [ ] Commit changes with clear message

---

## Testing & Validation

### ESP32 Testing Checklist

**Visual:**
- [ ] Cards render with glass effect
- [ ] Buttons have correct colors
- [ ] Text is readable (contrast check)
- [ ] Touch targets are 44px minimum
- [ ] Responsive on mobile browsers

**Functional:**
- [ ] All interactive elements work
- [ ] Hover/active states work
- [ ] Forms validate properly
- [ ] No console errors

**Performance:**
- [ ] Page loads < 200ms on LAN
- [ ] Flash usage < 16KB for UI
- [ ] Styles don't cause reflows
- [ ] Animations are smooth (if any)

**Accessibility:**
- [ ] Keyboard navigation works
- [ ] Color contrast passes WCAG AA
- [ ] Touch targets are adequate
- [ ] Reduced-motion support works

### Raspberry Pi Testing Checklist

**Visual:**
- [ ] Full style system renders correctly
- [ ] All components match lander design
- [ ] Halo effects work properly
- [ ] Gradients render smoothly
- [ ] Responsive across breakpoints

**Functional:**
- [ ] All routes work
- [ ] Dynamic content renders
- [ ] Forms submit correctly
- [ ] Real-time updates work (if applicable)

**Performance:**
- [ ] Page loads < 1s on LAN
- [ ] No layout shifts (CLS)
- [ ] Smooth scrolling
- [ ] Python templates render quickly

**Cross-browser:**
- [ ] Chrome/Chromium
- [ ] Firefox
- [ ] Safari (if testing on Mac/iOS)
- [ ] Mobile browsers

---

## Troubleshooting

### ESP32 Issues

**Problem: Styles not loading**
```c
// Check HTTP response headers
ESP_LOGI(TAG, "Sending CSS...");
httpd_resp_sendstr_chunk(req, LUMIGRID_MINIMAL_CSS);
```

**Problem: Flash overflow**
```bash
# Check current usage
idf.py size

# Enable GZIP in menuconfig if needed
idf.py menuconfig
# -> Component config -> HTTP Server -> Enable compression
```

**Problem: Touch targets too small**
```c
// Verify --touch-target-min is applied
".btn{min-height:var(--touch-target-min);}"
```

### Raspberry Pi Issues

**Problem: Static files not loading**
```python
# Check static folder configuration
app = Flask(__name__, static_folder='static', static_url_path='/static')

# Verify file exists
import os
print(os.path.exists('static/css/lumigrid-styles.css'))
```

**Problem: Tokens not updating**
```bash
# Clear Flask cache
rm -rf __pycache__
rm -rf templates/__pycache__

# Hard refresh browser (Ctrl+Shift+R)
```

**Problem: Port already in use**
```bash
# Find and kill process
sudo lsof -i :5000
sudo kill -9 <PID>

# Or use different port
python app.py --port 5001
```

### General Issues

**Problem: Colors look different across platforms**
- Verify token values are identical
- Check for color profile differences
- Test on same display if possible

**Problem: Layout breaks on mobile**
- Check viewport meta tag
- Test responsive breakpoints
- Verify touch target sizes

**Problem: Animations not respecting reduced-motion**
- Verify media query is included
- Test with browser setting enabled
- Check CSS specificity

---

## Best Practices

### ESP32
1. **Minimize CSS size** - Every byte counts on flash-constrained devices
2. **Test on real hardware** - Emulators don't catch flash/memory issues
3. **Use GZIP** - Reduces transmitted size by ~70%
4. **Cache aggressively** - Static styles rarely change
5. **Monitor flash usage** - Keep UI under 16KB total

### Raspberry Pi
1. **Use full token system** - No size constraints
2. **Leverage Python** - Generate styles dynamically when helpful
3. **Enable debug mode** - During development for auto-reload
4. **Set up systemd** - For production auto-start
5. **Use NGINX** - Reverse proxy for production deployment

### General
1. **Version control tokens** - Track changes in git
2. **Document customizations** - Note any platform-specific tweaks
3. **Test before deploying** - Always test token changes locally
4. **Keep platforms in sync** - Use automated sync workflow
5. **Monitor performance** - Ensure tokens don't impact load times

---

## Next Steps

### Short Term
- [ ] Implement ESP32 web UI with minimal tokens
- [ ] Set up Pi Gateway with full token system
- [ ] Test cross-platform consistency
- [ ] Document any platform-specific issues

### Long Term
- [ ] Add theme switching capability (Pi only)
- [ ] Create visual regression tests
- [ ] Build component showcase page
- [ ] Implement token versioning system
- [ ] Add automated deployment pipeline

---

**Last Updated:** 2026-01-08  
**Version:** 1.0.0
