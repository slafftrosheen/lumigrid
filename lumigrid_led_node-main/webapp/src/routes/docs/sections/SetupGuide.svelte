<h1>🚀 Setup Guide</h1>

<h2>Prerequisites</h2>
<h3>Required Software</h3>
<ul>
  <li><strong>Operating System:</strong> Linux (Ubuntu 20.04+), macOS, or Windows with WSL2</li>
  <li><strong>Python:</strong> 3.8 or newer</li>
  <li><strong>Git:</strong> Latest version</li>
  <li><strong>USB Drivers:</strong> CH340/CP210x for ESP32 serial connection</li>
</ul>

<h3>Hardware Requirements</h3>
<ul>
  <li>ESP32-WROOM-32U development board</li>
  <li>USB cable (data capable, not charging-only)</li>
  <li>Power supply: 5V, minimum 2A (10A recommended for full LED load)</li>
  <li>LED strips: WS2812B or SK6812 addressable LEDs</li>
  <li>(Optional) PCA9685 PWM controller for additional PWM channels</li>
</ul>

<h2>System Dependencies</h2>

<h3>Linux (Ubuntu/Debian)</h3>
<pre><code>sudo apt-get update && sudo apt-get upgrade -y

sudo apt-get install -y \
  git wget flex bison gperf cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  python3 python3-venv python3-pip python3-setuptools

sudo usermod -a -G dialout $USER
# Log out and back in for group changes to take effect</code></pre>

<h3>macOS</h3>
<pre><code>brew install cmake ninja dfu-util python3</code></pre>

<h2>ESP-IDF Installation</h2>
<pre><code>mkdir -p ~/esp
cd ~/esp

git clone --depth=1 --branch v5.3 https://github.com/espressif/esp-idf.git

cd ~/esp/esp-idf
./install.sh esp32

. ~/esp/esp-idf/export.sh</code></pre>

<h2>Building the Firmware</h2>
<pre><code># Clone the repository
git clone https://github.com/algorhythmicdev/lumigrid_led_node.git
cd lumigrid_led_node

# Build using the build script (builds webapp + firmware)
./BUILD.sh

# OR manual build
cd firmware
idf.py set-target esp32
idf.py build</code></pre>

<h2>Flashing to Hardware</h2>
<pre><code>cd firmware
idf.py -p /dev/ttyUSB0 flash monitor</code></pre>

<div class="alert alert-info not-prose">
  <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path></svg>
  <span>Exit monitor with <kbd>Ctrl+]</kbd></span>
</div>

<h2>First Boot Configuration</h2>
<p>After flashing, the device will create a Wi-Fi access point:</p>
<ul>
  <li><strong>SSID:</strong> LumiGrid-XXXXXX (where XXXXXX is the device MAC)</li>
  <li><strong>Password:</strong> lumigrid123</li>
</ul>

<p>Connect to the AP and navigate to <code>http://192.168.4.1</code> to configure:</p>
<ol>
  <li>Wi-Fi credentials for your network</li>
  <li>Node type (Master/Slave)</li>
  <li>MQTT settings (optional)</li>
  <li>LED configuration</li>
</ol>

<h2>Next Steps</h2>
<p>Once configured, proceed to:</p>
<ul>
  <li><a href="#hardware" on:click|preventDefault={() => window.location.hash = 'hardware'}>Hardware Guide</a> - Wire up your LEDs</li>
  <li><a href="#api" on:click|preventDefault={() => window.location.hash = 'api'}>API Reference</a> - Start controlling via API</li>
  <li><a href="#troubleshooting" on:click|preventDefault={() => window.location.hash = 'troubleshooting'}>Troubleshooting</a> - If you encounter issues</li>
</ul>
