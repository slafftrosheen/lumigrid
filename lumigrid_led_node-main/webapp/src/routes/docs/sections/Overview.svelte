<script>
  import { createEventDispatcher } from 'svelte';
  const dispatch = createEventDispatcher();
  
  function navigate(section) {
    window.location.hash = section;
    window.scrollTo(0, 0);
  }
</script>

<h1>📚 LumiGrid Documentation</h1>
<p class="lead">Welcome to the LumiGrid LED Node documentation. Choose a topic from the sidebar to get started.</p>

<div class="grid grid-cols-1 md:grid-cols-2 gap-4 not-prose">
  <div class="card bg-base-200 shadow-md">
    <div class="card-body">
      <h2 class="card-title">🚀 New to LumiGrid?</h2>
      <p>Start with the Setup Guide to get your device up and running.</p>
      <div class="card-actions">
        <button class="btn btn-primary btn-sm" on:click={() => navigate('setup')}>
          Setup Guide
        </button>
      </div>
    </div>
  </div>
  
  <div class="card bg-base-200 shadow-md">
    <div class="card-body">
      <h2 class="card-title">📡 API Integration</h2>
      <p>Learn how to control LumiGrid via REST API.</p>
      <div class="card-actions">
        <button class="btn btn-primary btn-sm" on:click={() => navigate('api')}>
          API Reference
        </button>
      </div>
    </div>
  </div>
  
  <div class="card bg-base-200 shadow-md">
    <div class="card-body">
      <h2 class="card-title">🔌 Hardware Setup</h2>
      <p>Wiring diagrams and safety guidelines for your LED installation.</p>
      <div class="card-actions">
        <button class="btn btn-primary btn-sm" on:click={() => navigate('hardware')}>
          Hardware Guide
        </button>
      </div>
    </div>
  </div>
  
  <div class="card bg-base-200 shadow-md">
    <div class="card-body">
      <h2 class="card-title">💻 Developer Docs</h2>
      <p>Contributing guidelines and architecture documentation.</p>
      <div class="card-actions">
        <button class="btn btn-primary btn-sm" on:click={() => navigate('developer')}>
          Developer Guide
        </button>
      </div>
    </div>
  </div>
</div>

<h2>Quick Links</h2>
<ul>
  <li><a href="https://github.com/algorhythmicdev/lumigrid_led_node" target="_blank" rel="noopener noreferrer">GitHub Repository</a></li>
  <li><a href="https://github.com/algorhythmicdev/lumigrid_led_node/issues" target="_blank" rel="noopener noreferrer">Issue Tracker</a></li>
  <li><a href="https://docs.espressif.com/projects/esp-idf/en/latest/" target="_blank" rel="noopener noreferrer">ESP-IDF Documentation</a></li>
</ul>

<h2>Documentation Version</h2>
<p>This documentation is for LumiGrid v1.0.0 targeting ESP32-WROOM-32U with ESP-IDF v5.3+.</p>
