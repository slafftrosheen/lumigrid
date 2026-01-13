<script>
  import { onMount } from 'svelte';
  import { display, frames, trigger } from '$lib/api.js';
  
  let availableModes = [
    { id: 'clock', name: 'Clock Display', description: 'Shows current time', icon: '🕒' },
    { id: 'date', name: 'Date Display', description: 'Shows current date', icon: '📅' },
    { id: 'datetime', name: 'DateTime Display', description: 'Shows date and time', icon: '🗓️' },
    { id: 'temperature', name: 'Temperature Display', description: 'Shows ambient temperature', icon: '🌡️' },
    { id: 'humidity', name: 'Humidity Display', description: 'Shows ambient humidity', icon: '💧' },
    { id: 'message', name: 'Message Display', description: 'Shows custom messages', icon: '💬' },
    { id: 'text', name: 'Text Display', description: 'Static text display', icon: '📝' },
    { id: 'frames', name: 'Frames Display', description: 'Rotating content display', icon: '🎬' },
    { id: 'slideshow', name: 'Slideshow', description: 'Cycle through modes', icon: '🔄' }
  ];
  
  let selectedMode = 'clock';
  let customMessage = '';
  let isLoading = false;
  let activeEffect = null;
  
  async function applyMode(modeId) {
    try {
      isLoading = true;
      await display.setMode(modeId);
      activeEffect = modeId;
      console.log(`Applied mode: ${modeId}`);
    } catch (error) {
      console.error('Error applying mode:', error);
    } finally {
      isLoading = false;
    }
  }
  
  async function applyCustomMessage() {
    if (!customMessage.trim()) return;
    
    try {
      isLoading = true;
      await display.setMessage(customMessage);
      await display.setMode('message');
      activeEffect = 'message';
      console.log(`Applied message: ${customMessage}`);
    } catch (error) {
      console.error('Error applying message:', error);
    } finally {
      isLoading = false;
    }
  }
  
  async function stopEffect() {
    try {
      isLoading = true;
      await display.setMode('frames'); // Return to default frames mode
      activeEffect = null;
      console.log('Stopped current effect');
    } catch (error) {
      console.error('Error stopping effect:', error);
    } finally {
      isLoading = false;
    }
  }
  
  function getActiveModeName() {
    const mode = availableModes.find(m => m.id === activeEffect);
    return mode ? mode.name : 'None';
  }
</script>

<div class="container mx-auto p-4">
  <h1 class="text-2xl font-bold mb-6">Display Effects</h1>
  
  <!-- Active Effect Indicator -->
  <div class="mb-6">
    <div class="card bg-primary text-primary-content">
      <div class="card-body">
        <h3 class="card-title">Active Effect</h3>
        <p>Currently running: <strong>{getActiveModeName()}</strong></p>
        {#if activeEffect}
          <div class="card-actions justify-end">
            <button class="btn btn-secondary btn-sm" on:click={stopEffect} disabled={isLoading}>
              Stop Effect
            </button>
          </div>
        {/if}
      </div>
    </div>
  </div>
  
  <!-- Custom Message Input -->
  <div class="card bg-base-100 shadow-xl mb-6">
    <div class="card-body">
      <h2 class="card-title">Custom Message</h2>
      <div class="flex flex-col sm:flex-row gap-2">
        <input 
          type="text" 
          class="input input-bordered flex-grow" 
          placeholder="Enter your custom message..."
          bind:value={customMessage}
        />
        <button 
          class="btn btn-primary" 
          on:click={applyCustomMessage}
          disabled={isLoading || !customMessage.trim()}
        >
          Show Message
        </button>
      </div>
    </div>
  </div>
  
  <!-- Effect Grid -->
  <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
    {#each availableModes as mode}
      <div class="card bg-base-100 shadow-xl">
        <div class="card-body">
          <h3 class="card-title">
            <span class="text-2xl mr-2">{mode.icon}</span>
            {mode.name}
          </h3>
          <p class="text-sm opacity-75">{mode.description}</p>
          <div class="card-actions justify-end">
            <button 
              class="btn btn-primary btn-sm" 
              on:click={() => applyMode(mode.id)}
              disabled={isLoading || activeEffect === mode.id}
            >
              {activeEffect === mode.id ? 'Active' : 'Apply'}
            </button>
          </div>
        </div>
      </div>
    {/each}
  </div>
</div>