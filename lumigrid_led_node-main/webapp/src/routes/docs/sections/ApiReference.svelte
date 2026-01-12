<h1>📡 REST API Reference</h1>
<p class="lead">Base URL: <code>http://&lt;node-ip&gt;:80/</code></p>

<h2>Status Endpoints</h2>

<h3>GET /api/status</h3>
<p>Get current node status including health, performance metrics, and telemetry.</p>
<pre><code>curl http://192.168.1.100/api/status</code></pre>

<p><strong>Response (200 OK):</strong></p>
<pre><code>{@html `{
  "node_type": "led-node",
  "role": "Slave",
  "uptime_ms": 9234567,
  "fps": {
    "aled_ch1": 60,
    "aled_ch2": 60
  },
  "heap_free_kb": 112,
  "connected": true,
  "effect": "rainbow_flow"
}`}</code></pre>

<h2>Configuration Endpoints</h2>

<h3>GET /api/config</h3>
<p>Retrieve current configuration.</p>
<pre><code>curl http://192.168.1.100/api/config</code></pre>

<h3>POST /api/config</h3>
<p>Update configuration (partial updates allowed).</p>
<pre><code>{@html `curl -X POST http://192.168.1.100/api/config \\
  -H "Content-Type: application/json" \\
  -d '{
    "wifi": {
      "ssid": "MyNetwork",
      "password": "MyPassword"
    }
  }'`}</code></pre>

<h2>Effect Control</h2>

<h3>GET /api/fx</h3>
<p>List all available effects.</p>
<pre><code>curl http://192.168.1.100/api/fx</code></pre>

<h3>POST /api/effect/apply</h3>
<p>Apply an effect to one or more channels.</p>
<pre><code>{@html `curl -X POST http://192.168.1.100/api/effect/apply \\
  -H "Content-Type: application/json" \\
  -d '{
    "effect": "rainbow_flow",
    "channels": [1, 2],
    "params": {
      "speed": 50,
      "brightness": 200
    }
  }'`}</code></pre>

<h2>Preset Management</h2>

<h3>GET /api/presets</h3>
<p>List all saved presets.</p>

<h3>POST /api/presets</h3>
<p>Create or update a preset.</p>
<pre><code>{@html `curl -X POST http://192.168.1.100/api/presets \\
  -H "Content-Type: application/json" \\
  -d '{
    "name": "my_preset",
    "effect": "rainbow_flow",
    "channels": [1, 2, 3],
    "params": {
      "speed": 75,
      "brightness": 255
    }
  }'`}</code></pre>

<h3>DELETE /api/presets/&lt;name&gt;</h3>
<p>Delete a specific preset.</p>
<pre><code>curl -X DELETE http://192.168.1.100/api/presets/my_preset</code></pre>

<h2>OTA Updates</h2>

<h3>POST /api/ota/upload</h3>
<p>Upload firmware binary for OTA update.</p>
<pre><code>curl -X POST http://192.168.1.100/api/ota/upload \
  -F "file=@firmware.bin"</code></pre>

<h3>POST /api/ota/url</h3>
<p>Download firmware from URL.</p>
<pre><code>{@html `curl -X POST http://192.168.1.100/api/ota/url \\
  -H "Content-Type: application/json" \\
  -d '{"url": "http://example.com/firmware.bin"}'`}</code></pre>

<h2>Server-Sent Events</h2>

<h3>GET /events</h3>
<p>Subscribe to real-time events stream.</p>
<pre><code>curl -N http://192.168.1.100/events</code></pre>

<div class="alert alert-success not-prose">
  <svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>
  <span>For complete API documentation including all parameters and responses, see the <a href="https://github.com/algorhythmicdev/lumigrid_led_node/blob/main/Docs/API_REFERENCE.md" target="_blank" class="link">full API reference</a>.</span>
</div>
