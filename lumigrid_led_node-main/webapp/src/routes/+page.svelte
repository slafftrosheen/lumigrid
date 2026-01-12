<script>
  import { onMount } from 'svelte';
  import LEDPreview from '$lib/LEDPreview.svelte';
  import { system, effects, trigger } from '$lib/api.js';
  
  let status = {
    connected: false,
    uptime_ms: 0,
    heap_free_kb: 0,
    effect: 'None'
  };

  let stats = {
    fps: 0,
    cpu: 0,
    wifi_rssi: 0
  };

  let controls = {
    brightness: 100,
    speed: 50,
    intensity: 75
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
        connected: true,
        uptime: data.uptime_ms || 0,
        heap_free: data.heap_free_kb ? data.heap_free_kb * 1024 : 0
      };
      
      if (data.fps && data.fps.ch0) {
        stats.fps = data.fps.ch0;
      }
    } catch (error) {
      console.error('Failed to fetch status:', error);
      status.connected = false;
    }
  }

  async function applyControls() {
    if (isApplying) return;
    isApplying = true;
    
    try {
      // Apply brightness and speed to channel 0
      await effects.applyToChannel(0, null, {
        brightness: controls.brightness / 100,
        speed: controls.speed / 50, // Normalize to 0-2 range
        intensity: controls.intensity / 100
      });
      
      addLog('Controls applied', 'success');
    } catch (error) {
      console.error('Failed to apply controls:', error);
      addLog('Failed to apply controls', 'error');
    } finally {
      isApplying = false;
    }
  }

  async function resetControls() {
    controls = { brightness: 100, speed: 50, intensity: 75 };
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
  <title>LumiGrid Dashboard</title>
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
        Uptime: {Math.floor((status.uptime_ms || status.uptime || 0) / 60000)}m
      </div>
    </div>

    <!-- Memory -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-secondary">
        💾
      </div>
      <div class="stat-title">Free Heap</div>
      <div class="stat-value text-2xl">
        {Math.round((status.heap_free_kb || status.heap_free / 1024 || 0))}KB
      </div>
      <div class="stat-desc">ESP32 Memory</div>
    </div>

    <!-- FPS -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-accent">
        📊
      </div>
      <div class="stat-title">Frame Rate</div>
      <div class="stat-value text-2xl">{stats.fps} fps</div>
      <div class="stat-desc">Effect Rendering</div>
    </div>

    <!-- Current Effect -->
    <div class="stat bg-base-200 rounded-lg shadow">
      <div class="stat-figure text-info">
        ✨
      </div>
      <div class="stat-title">Active Effect</div>
      <div class="stat-value text-sm">{status.effect}</div>
      <div class="stat-desc">Current Pattern</div>
    </div>
  </div>

  <!-- LED Preview -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">LED Preview</h2>
      <LEDPreview />
    </div>
  </div>

  <!-- Quick Controls -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Quick Controls</h2>
      <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
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

        <!-- Speed -->
        <div class="form-control">
          <label class="label">
            <span class="label-text">Speed</span>
            <span class="label-text-alt">{controls.speed}%</span>
          </label>
          <input 
            type="range" 
            min="0" 
            max="100" 
            bind:value={controls.speed}
            class="range range-secondary" 
          />
        </div>

        <!-- Intensity -->
        <div class="form-control">
          <label class="label">
            <span class="label-text">Intensity</span>
            <span class="label-text-alt">{controls.intensity}%</span>
          </label>
          <input 
            type="range" 
            min="0" 
            max="100" 
            bind:value={controls.intensity}
            class="range range-accent" 
          />
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

  <!-- Recent Activity -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">System Log</h2>
      <div class="overflow-x-auto">
        <div class="mockup-code">
          {#each logs as log}
            <pre data-prefix={log.prefix} class="text-{log.type}"><code>{log.text}</code></pre>
          {/each}
        </div>
      </div>
    </div>
  </div>
</div>
