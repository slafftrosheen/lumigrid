<h1>🌐 Networking Guide</h1>

<h2>Wi-Fi Configuration</h2>

<h3>Station Mode (Connect to existing network)</h3>
<p>Configure via web interface or API:</p>
<pre><code>{@html `curl -X POST http://192.168.4.1/api/config \\
  -H "Content-Type: application/json" \\
  -d '{
    "wifi": {
      "ssid": "YourNetwork",
      "password": "YourPassword",
      "mode": "sta"
    }
  }'`}</code></pre>

<h3>Access Point Mode (Default)</h3>
<ul>
  <li><strong>SSID:</strong> LumiGrid-XXXXXX</li>
  <li><strong>Password:</strong> lumigrid123</li>
  <li><strong>IP Address:</strong> 192.168.4.1</li>
</ul>

<h2>MQTT Configuration</h2>
<p>Enable MQTT for remote control and telemetry:</p>
<pre><code>{@html `curl -X POST http://192.168.1.100/api/config \\
  -H "Content-Type: application/json" \\
  -d '{
    "mqtt": {
      "enabled": true,
      "broker": "mqtt.example.com",
      "port": 1883,
      "username": "user",
      "password": "pass",
      "topic_prefix": "lumigrid"
    }
  }'`}</code></pre>

<h3>MQTT Topics</h3>
<table class="table table-zebra not-prose">
  <thead>
    <tr>
      <th>Topic</th>
      <th>Direction</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>lumigrid/status</td><td>Publish</td><td>Status updates</td></tr>
    <tr><td>lumigrid/cmd</td><td>Subscribe</td><td>Commands</td></tr>
    <tr><td>lumigrid/effect</td><td>Subscribe</td><td>Effect changes</td></tr>
  </tbody>
</table>

<h2>Master/Slave Synchronization</h2>

<h3>UDP Multicast Protocol</h3>
<ul>
  <li><strong>Multicast Address:</strong> 239.1.1.1</li>
  <li><strong>Port:</strong> 4210</li>
  <li><strong>Protocol:</strong> Binary packet format</li>
</ul>

<h3>Configuring Roles</h3>
<pre><code>{@html `// Set as Master
curl -X POST http://192.168.1.100/api/config \\
  -H "Content-Type: application/json" \\
  -d '{"role": "Master"}'

// Set as Slave
curl -X POST http://192.168.1.101/api/config \\
  -H "Content-Type: application/json" \\
  -d '{"role": "Slave"}'`}</code></pre>

<div class="alert alert-info not-prose">
  <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path></svg>
  <span>All nodes must be on the same network subnet for multicast synchronization to work.</span>
</div>

<h2>Firewall Configuration</h2>
<p>Required ports:</p>
<ul>
  <li><strong>80/TCP:</strong> HTTP API and Web UI</li>
  <li><strong>1883/TCP:</strong> MQTT (if enabled)</li>
  <li><strong>4210/UDP:</strong> Sync protocol (multicast)</li>
</ul>

<h2>Network Integration Examples</h2>

<h3>Raspberry Pi Integration</h3>
<p>Control LumiGrid from Raspberry Pi:</p>
<pre><code>{@html `import requests

# Get status
response = requests.get('http://192.168.1.100/api/status')
print(response.json())

# Apply effect
requests.post('http://192.168.1.100/api/effect/apply', json={
    'effect': 'rainbow_flow',
    'channels': [1, 2, 3]
})`}</code></pre>

<h3>Home Assistant Integration</h3>
<p>Use MQTT for Home Assistant integration. See <a href="https://github.com/algorhythmicdev/lumigrid_led_node/blob/main/Docs/NETWORKING_GUIDE.md" target="_blank" class="link">Networking Guide</a> for details.</p>
