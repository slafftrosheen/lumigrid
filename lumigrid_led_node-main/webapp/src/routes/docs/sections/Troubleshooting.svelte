<h1>🔧 Troubleshooting</h1>

<h2>Common Issues</h2>

<div class="not-prose">
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" checked />
    <div class="collapse-title text-xl font-medium">
      Device won't flash / Serial port not found
    </div>
    <div class="collapse-content">
      <p><strong>Solutions:</strong></p>
      <ul>
        <li>Check USB cable is data-capable (not charge-only)</li>
        <li>Install CH340/CP210x drivers</li>
        <li>Add user to dialout group: <code>sudo usermod -a -G dialout $USER</code></li>
        <li>Try different USB port</li>
        <li>Check port with: <code>ls /dev/ttyUSB*</code> or <code>ls /dev/ttyACM*</code></li>
      </ul>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      Build fails with "command not found: idf.py"
    </div>
    <div class="collapse-content">
      <p><strong>Solution:</strong></p>
      <p>ESP-IDF environment not activated. Run:</p>
      <pre><code>. ~/esp/esp-idf/export.sh</code></pre>
      <p>Add to <code>~/.bashrc</code> or <code>~/.zshrc</code> for automatic activation.</p>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      LEDs not working or showing wrong colors
    </div>
    <div class="collapse-content">
      <p><strong>Checklist:</strong></p>
      <ul>
        <li>Verify LED power supply is 5V with sufficient current</li>
        <li>Check LED type in config matches your strips (WS2812B vs SK6812)</li>
        <li>Ensure data line has 470Ω resistor</li>
        <li>Check wiring: ESP32 GND connected to LED GND</li>
        <li>Try lower brightness to reduce current draw</li>
        <li>Verify GPIO pin assignments match your wiring</li>
      </ul>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      Can't connect to Wi-Fi AP
    </div>
    <div class="collapse-content">
      <p><strong>Solutions:</strong></p>
      <ul>
        <li>Default SSID: LumiGrid-XXXXXX (XXXXXX = last 6 chars of MAC)</li>
        <li>Default password: lumigrid123</li>
        <li>Check serial monitor for actual SSID</li>
        <li>Factory reset: Hold boot button during power-on</li>
        <li>Some phones hide 2.4GHz-only networks - check Wi-Fi settings</li>
      </ul>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      Web UI not loading
    </div>
    <div class="collapse-content">
      <p><strong>Solutions:</strong></p>
      <ul>
        <li>Clear browser cache and hard refresh (Ctrl+Shift+R)</li>
        <li>Try different browser</li>
        <li>Check serial monitor for IP address</li>
        <li>Verify you're on the same network as the device</li>
        <li>Try http:// explicitly (not https://)</li>
      </ul>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      Effects running slowly / Low FPS
    </div>
    <div class="collapse-content">
      <p><strong>Solutions:</strong></p>
      <ul>
        <li>Reduce LED count per channel (max 300 recommended)</li>
        <li>Lower effect refresh rate</li>
        <li>Disable unused channels</li>
        <li>Check <code>/api/status</code> for heap memory warnings</li>
        <li>Simplify effect parameters</li>
      </ul>
    </div>
  </div>
  
  <div class="collapse collapse-arrow bg-base-200 mb-2">
    <input type="radio" name="troubleshooting-accordion" />
    <div class="collapse-title text-xl font-medium">
      Master/Slave sync not working
    </div>
    <div class="collapse-content">
      <p><strong>Checklist:</strong></p>
      <ul>
        <li>All devices on same subnet</li>
        <li>Multicast enabled on network (some routers block it)</li>
        <li>Firewall allows UDP port 4210</li>
        <li>Check role configuration via <code>/api/status</code></li>
        <li>Verify multicast address 239.1.1.1:4210</li>
      </ul>
    </div>
  </div>
</div>

<h2>Diagnostic Commands</h2>

<h3>Check System Status</h3>
<pre><code>curl http://192.168.1.100/api/status | jq</code></pre>

<h3>View Serial Debug Output</h3>
<pre><code>idf.py -p /dev/ttyUSB0 monitor</code></pre>

<h3>Check Heap Memory</h3>
<p>In serial monitor, look for:</p>
<pre><code>I (12345) main: Free heap: 112KB</code></pre>
<p>Low memory (&lt;50KB) can cause instability.</p>

<h2>Factory Reset</h2>
<p>To reset to factory defaults:</p>
<ol>
  <li>Power off the device</li>
  <li>Hold the BOOT button (GPIO0)</li>
  <li>Power on while holding BOOT</li>
  <li>Release after 3 seconds</li>
  <li>Device will create default AP</li>
</ol>

<div class="alert alert-error not-prose">
  <svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 14l2-2m0 0l2-2m-2 2l-2-2m2 2l2 2m7-2a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
  <span><strong>Warning:</strong> Factory reset erases all presets, configuration, and stored Wi-Fi credentials.</span>
</div>

<h2>Getting Help</h2>
<p>If you're still experiencing issues:</p>
<ul>
  <li>Check <a href="https://github.com/algorhythmicdev/lumigrid_led_node/issues" target="_blank" class="link">GitHub Issues</a></li>
  <li>Include serial monitor output when reporting issues</li>
  <li>Provide <code>/api/status</code> output</li>
  <li>Specify ESP-IDF version: <code>idf.py --version</code></li>
</ul>
