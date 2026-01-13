<script>
  import { onMount } from 'svelte';
  import { system, display, trigger } from '$lib/api.js';
  import DisplayPreview from '$lib/DisplayPreview.svelte';
  
  let status = {
    connected: false,
    uptime: 0,
    heap_free: 0,
    mode: 'frames',
    brightness: 50
  };

  let stats = {
    fps: 0,
    cpu: 0,
    wifi_rssi: 0
  };

  let controls = {
    brightness: 50,
    mode: 'frames'
  };

  let isApplying = false;
  let logs = [
    { prefix: '>', text: 'System initialized', type: 'success' },
    { prefix: '>', text: 'Connecting to device...', type: 'info' }
  ];

  async function fetchStatus() {
    try {
      const data = await system.getStatus();
      status = { 
        ...status, 
        ...data, 
        connected: true
      };
      
      // Update controls based on current status
      controls.brightness = data.brightness || 50;
      controls.mode = data.mode || 'frames';
    } catch (error) {
      console.error('Failed to fetch status:', error);
      status.connected = false;
    }
  }

  async function applyControls() {
    if (isApplying) return;
    isApplying = true;
    
    try {
      // Apply brightness
      await display.setBrightness(controls.brightness);
      
      // Apply mode
      await display.setMode(controls.mode);
      
      addLog('Controls applied', 'success');
    } catch (error) {
      console.error('Failed to apply controls:', error);
      addLog('Failed to apply controls', 'error');
    } finally {
      isApplying = false;
    }
  }

  async function resetControls() {
    controls = { brightness: 50, mode: 'frames' };
    await applyControls();
  }

  function addLog(text, type = 'info') {
    logs = [...logs, { prefix: '>', text, type }];
    // Keep only the last 10 logs
    if (logs.length > 10) {
      logs = logs.slice(-10);
    }
  }

  onMount(() => {
    fetchStatus();
    const interval = setInterval(fetchStatus, 2000);
    
    // Initial log
    setTimeout(() => addLog('Connected to device', 'success'), 500);
    setTimeout(() => addLog('Ready for commands', 'success'), 1000);
    
    return () => clearInterval(interval);
  });
</script>

<svelte:head>
  <title>LumiGrid Display Dashboard</title>
</svelte:head>

<div class="space-y-6">
  <!-- Status Cards -->
  <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
    <!-- Connection Status -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-primary">
        {#if status.connected}
          <div class="badge badge-success badge-lg">●</div>
        {:else}
          <div class="badge badge-error badge-lg">●</div>
        {/if}
      </div>
      <div class="stat-title">Connection</div>
      <div class="stat-value text-sm">
        {status.connected ? 'Connected' : 'Disconnected'}
      </div>
      <div class="stat-desc">
        Uptime: {Math.floor((status.uptime || 0) / 60000)}m
      </div>
    </div>

    <!-- Brightness -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-secondary">
        💡
      </div>
      <div class="stat-title">Brightness</div>
      <div class="stat-value text-2xl">
        {status.brightness || 0}%
      </div>
      <div class="stat-desc">Display Level</div>
    </div>

    <!-- Mode -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-accent">
        🎨
      </div>
      <div class="stat-title">Display Mode</div>
      <div class="stat-value text-sm">{status.mode}</div>
      <div class="stat-desc">Current Layout</div>
    </div>

    <!-- Temperature -->
    {#if status.temperature !== undefined && status.temperature !== null}
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-info">
        🌡️
      </div>
      <div class="stat-title">Temperature</div>
      <div class="stat-value text-2xl">{status.temperature.toFixed(1)}°C</div>
      <div class="stat-desc">Ambient Reading</div>
    </div>
    {/if}
  </div>

  <!-- Display Preview -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Display Preview</h2>
      <p class="text-sm opacity-75 mb-4">Visual representation of your LED matrix</p>
      <div class="flex justify-center">
        <DisplayPreview width={32} height={16} pixelSize={12} />
      </div>
    </div>
  </div>

  <!-- Quick Controls -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Quick Controls</h2>
      <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
        <!-- Brightness -->
        <div class="form-control">
          <label class="label">
            <span class="label-text">Brightness</span>
            <span class="label-text-alt">{controls.brightness}%</span>
          </label>
          <input 
            type="range" 
            min="0" 
            max="100" 
            bind:value={controls.brightness}
            class="range range-primary" 
          />
        </div>

        <!-- Mode Selection -->
        <div class="form-control">
          <label class="label">
            <span class="label-text">Display Mode</span>
          </label>
          <select 
            bind:value={controls.mode}
            class="select select-bordered"
          >
            <option value="clock">Clock</option>
            <option value="temperature">Temperature</option>
            <option value="message">Message</option>
            <option value="frames">Frames</option>
            <option value="slideshow">Slideshow</option>
          </select>
        </div>
      </div>

      <div class="card-actions justify-end mt-4">
        <button 
          class="btn btn-primary" 
          on:click={applyControls}
          disabled={isApplying}
        >
          {isApplying ? 'Applying...' : 'Apply'}
        </button>
        <button class="btn btn-outline" on:click={resetControls}>Reset</button>
      </div>
    </div>
  </div>

  <!-- Frames Management -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Frame Management</h2>
      <p class="text-sm opacity-75 mb-4">Manage content displayed on your LED matrix</p>
      
      <div class="flex flex-wrap gap-2">
        <button class="btn btn-sm btn-outline">Add Frame</button>
        <button class="btn btn-sm btn-outline">Edit Frames</button>
        <button class="btn btn-sm btn-outline">Schedule</button>
      </div>
    </div>
  </div>

  <!-- Recent Activity -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">System Log</h2>
      <div class="overflow-x-auto">
        <div class="mockup-code">
          {#each logs as log}
            <pre data-prefix={log.prefix} class:text-{log.type}><code>{log.text}</code></pre>
          {/each}
        </div>
      </div>
    </div>
  </div>
</div>
