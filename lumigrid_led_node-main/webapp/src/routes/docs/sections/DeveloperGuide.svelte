<h1>💻 Developer Guide</h1>

<h2>Repository Structure</h2>
<pre><code>lumigrid_led_node/
├── firmware/              # ESP-IDF project
│   ├── main/              # Application code
│   ├── components/        # Reusable components
│   │   ├── rest_api/      # HTTP endpoints
│   │   ├── led_effects/   # Effect engine
│   │   ├── pca9685_driver/# PWM controller
│   │   └── ...
│   └── assets/webapp/     # Web UI files
├── webapp/                # SvelteKit web UI source
│   └── src/
│       ├── lib/           # API wrappers
│       └── routes/        # Pages
├── Docs/                  # Documentation
└── scripts/               # Build scripts</code></pre>

<h2>Development Workflow</h2>

<h3>1. Fork and Clone</h3>
<pre><code>git clone https://github.com/YOUR_USERNAME/lumigrid_led_node.git
cd lumigrid_led_node</code></pre>

<h3>2. Create Feature Branch</h3>
<pre><code>git checkout -b feature/my-new-feature</code></pre>

<h3>3. Make Changes</h3>
<p>Follow the coding standards below.</p>

<h3>4. Test</h3>
<pre><code>cd firmware
idf.py build
idf.py test</code></pre>

<h3>5. Submit PR</h3>
<p>Push your branch and create a pull request on GitHub.</p>

<h2>Coding Standards</h2>

<h3>C/C++ (Firmware)</h3>
<ul>
  <li>Treat warnings as errors (<code>-Werror=format</code>, etc.)</li>
  <li>No single-byte buffers; use <code>char buf[N]</code> where N &gt; 1</li>
  <li>Use cJSON getters with type checks</li>
  <li>Follow existing code patterns in each file</li>
  <li>Add comments for complex logic only</li>
</ul>

<h3>JavaScript/Svelte (Web UI)</h3>
<ul>
  <li>Use SvelteKit conventions</li>
  <li>Leverage DaisyUI components</li>
  <li>Keep components small and focused</li>
  <li>Use the API wrapper in <code>lib/api.js</code></li>
</ul>

<h2>Adding New Effects</h2>
<ol>
  <li>Edit <code>firmware/components/led_effects/effects.c</code></li>
  <li>Add <code>fx_&lt;name&gt;_init()</code> and <code>fx_&lt;name&gt;_render()</code></li>
  <li>Register in <code>EFFECTS[]</code> array</li>
  <li>Document parameters and behavior</li>
</ol>

<h2>Testing</h2>

<h3>Unit Tests</h3>
<p>Component tests live under <code>firmware/components/*/test</code></p>
<pre><code>cd firmware
idf.py build
idf.py test</code></pre>

<h3>Web UI Testing</h3>
<pre><code>cd webapp
npm run check</code></pre>

<h2>Documentation</h2>
<p>When adding features:</p>
<ul>
  <li>Update relevant docs in <code>Docs/</code></li>
  <li>Add API examples to <code>API_EXAMPLES.md</code></li>
  <li>Update <code>api_spec.yaml</code> for new endpoints</li>
  <li>Add inline code comments for complex logic</li>
</ul>

<h2>Architecture</h2>
<p>For detailed system architecture, see:</p>
<ul>
  <li><a href="https://github.com/algorhythmicdev/lumigrid_led_node/blob/main/Docs/ARCHITECTURE.md" target="_blank" class="link">ARCHITECTURE.md</a></li>
  <li><a href="https://github.com/algorhythmicdev/lumigrid_led_node/blob/main/CONTRIBUTING.md" target="_blank" class="link">CONTRIBUTING.md</a></li>
</ul>

<h2>Component Overview</h2>

<h3>Firmware Components</h3>
<table class="table table-zebra not-prose">
  <thead>
    <tr>
      <th>Component</th>
      <th>Purpose</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>rest_api</td><td>HTTP server and REST endpoints</td></tr>
    <tr><td>led_effects</td><td>Effect rendering engine</td></tr>
    <tr><td>pca9685_driver</td><td>PWM controller interface</td></tr>
    <tr><td>wifi_manager</td><td>Wi-Fi configuration</td></tr>
    <tr><td>mqtt_client</td><td>MQTT integration</td></tr>
    <tr><td>sync_protocol</td><td>Master/slave synchronization</td></tr>
  </tbody>
</table>

<h2>Useful Resources</h2>
<ul>
  <li><a href="https://docs.espressif.com/projects/esp-idf/en/latest/" target="_blank" class="link">ESP-IDF Documentation</a></li>
  <li><a href="https://kit.svelte.dev/docs" target="_blank" class="link">SvelteKit Documentation</a></li>
  <li><a href="https://daisyui.com/components/" target="_blank" class="link">DaisyUI Components</a></li>
</ul>
