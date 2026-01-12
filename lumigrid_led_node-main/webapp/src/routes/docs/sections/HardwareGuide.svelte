<h1>🔌 Hardware Guide</h1>

<h2>GPIO Pinout</h2>

<h3>Addressable LED Channels (WS2812B/SK6812)</h3>
<table class="table table-zebra not-prose">
  <thead>
    <tr>
      <th>Channel</th>
      <th>GPIO</th>
      <th>Notes</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>ALED ch1</td><td>GPIO16</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch2</td><td>GPIO4</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch3</td><td>GPIO17</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch4</td><td>GPIO18</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch5</td><td>GPIO19</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch6</td><td>GPIO23</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch7</td><td>GPIO26</td><td>RMT peripheral</td></tr>
    <tr><td>ALED ch8</td><td>GPIO27</td><td>RMT peripheral</td></tr>
  </tbody>
</table>

<h3>I²C Bus (PCA9685 PWM Controller)</h3>
<table class="table table-zebra not-prose">
  <thead>
    <tr>
      <th>Function</th>
      <th>GPIO</th>
      <th>Speed</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>SDA</td><td>GPIO21</td><td>400kHz</td></tr>
    <tr><td>SCL</td><td>GPIO22</td><td>400kHz</td></tr>
    <tr><td>PCA9685 OE#</td><td>GPIO25</td><td>Active-low enable</td></tr>
  </tbody>
</table>

<h3>Serial Console</h3>
<ul>
  <li><strong>TX:</strong> GPIO1</li>
  <li><strong>RX:</strong> GPIO3</li>
  <li><strong>Baud Rate:</strong> 115200</li>
</ul>

<h2>Power Requirements</h2>

<div class="stats shadow not-prose">
  <div class="stat">
    <div class="stat-title">Voltage</div>
    <div class="stat-value text-primary">5V DC</div>
    <div class="stat-desc">±5% tolerance</div>
  </div>
  
  <div class="stat">
    <div class="stat-title">Current</div>
    <div class="stat-value text-secondary">10A min</div>
    <div class="stat-desc">For full LED load</div>
  </div>
  
  <div class="stat">
    <div class="stat-title">Controller</div>
    <div class="stat-value">0.5A</div>
    <div class="stat-desc">ESP32 allocation</div>
  </div>
</div>

<h2>Safety Guidelines</h2>

<div class="alert alert-warning not-prose">
  <svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" /></svg>
  <div>
    <h3 class="font-bold">Important Safety Information</h3>
    <ul class="list-disc list-inside">
      <li>Always disconnect power before wiring</li>
      <li>Use appropriate wire gauge for current (18 AWG for 10A)</li>
      <li>Connect power supply ground to ESP32 ground</li>
      <li>Add 470Ω resistor on LED data lines</li>
      <li>Use 1000µF capacitor across power supply</li>
      <li>Never exceed 8A total LED current (firmware enforced)</li>
    </ul>
  </div>
</div>

<h2>Wiring Diagram</h2>
<p>For detailed wiring diagrams and connection instructions, see the <a href="https://github.com/algorhythmicdev/lumigrid_led_node/blob/main/Docs/HARDWARE_GUIDE.md" target="_blank" class="link">complete Hardware Guide</a>.</p>

<h2>LED Strip Specifications</h2>
<ul>
  <li><strong>Supported Types:</strong> WS2812B, SK6812, WS2813</li>
  <li><strong>Max LEDs per Channel:</strong> 300 (recommended for 60 FPS)</li>
  <li><strong>Power per LED:</strong> ~60mA at full white brightness</li>
  <li><strong>Data Protocol:</strong> 800kHz single-wire</li>
</ul>
