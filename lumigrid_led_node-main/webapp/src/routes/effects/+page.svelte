<script>
  import { onMount } from 'svelte';
  import LEDPreview from '$lib/LEDPreview.svelte';
  import { effects as effectsApi } from '$lib/api.js';

  let effectsList = [];
  let selectedEffect = null;
  let effectParams = {
    speed: 1.0,
    intensity: 0.5,
    brightness: 1.0,
    color: '#FF0000'
  };
  let isApplying = false;
  let errorMessage = '';

  const effectCategories = {
    'Pattern': ['scanner', 'comet', 'theater_chase', 'running_lights', 'heartbeat', 
                'breathing', 'police_strobe', 'confetti', 'bpm_strobe', 'ripple'],
    'Noise': ['perlin_flow', 'plasma', 'lava_lamp', 'clouds', 'marble', 'electric', 'cellular'],
    'Classic': ['rainbow_flow', 'twinkle', 'meteor', 'fire2012'],
    'Extra': ['strobe_sync', 'barber_pole', 'twinkle_dust', 'vu_bars']
  };

  onMount(async () => {
    await fetchEffects();
  });

  async function fetchEffects() {
    try {
      const data = await effectsApi.list();
      // API returns { effects: [...] }
      effectsList = data.effects || data || [];
      errorMessage = '';
    } catch (error) {
      console.error('Failed to fetch effects:', error);
      errorMessage = 'Failed to load effects list';
    }
  }

  async function applyEffect() {
    if (!selectedEffect || isApplying) return;
    
    isApplying = true;
    errorMessage = '';

    try {
      // Convert hex color to params
      const colorValue = effectParams.color;
      
      await effectsApi.apply(selectedEffect, {
        speed: effectParams.speed,
        intensity: effectParams.intensity,
        brightness: effectParams.brightness,
        color: colorValue
      });
      
      console.log('Effect applied successfully');
    } catch (error) {
      console.error('Failed to apply effect:', error);
      errorMessage = 'Failed to apply effect';
    } finally {
      isApplying = false;
    }
  }
  
  async function stopEffect() {
    try {
      await effectsApi.stop();
      selectedEffect = null;
    } catch (error) {
      console.error('Failed to stop effect:', error);
      errorMessage = 'Failed to stop effect';
    }
  }

  function selectEffect(effect) {
    selectedEffect = effect;
    errorMessage = '';
  }
</script>

<svelte:head>
  <title>Effects - LumiGrid</title>
</svelte:head>

<div class="space-y-6">
  <h1 class="text-3xl font-bold">LED Effects</h1>

  <!-- Preview -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <h2 class="card-title">Live Preview</h2>
      <LEDPreview />
    </div>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
    <!-- Effect Browser -->
    <div class="lg:col-span-2 space-y-4">
      {#each Object.entries(effectCategories) as [category, categoryEffects]}
        <div class="card bg-base-200 shadow-xl">
          <div class="card-body">
            <h3 class="card-title text-lg">{category} Effects</h3>
            <div class="grid grid-cols-2 md:grid-cols-3 gap-2">
              {#each categoryEffects as effect}
                <button
                  class="btn btn-sm"
                  class:btn-primary={selectedEffect === effect}
                  on:click={() => selectEffect(effect)}
                >
                  {effect.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase())}
                </button>
              {/each}
            </div>
          </div>
        </div>
      {/each}
    </div>

    <!-- Effect Parameters -->
    <div class="lg:col-span-1">
      <div class="card bg-base-200 shadow-xl sticky top-4">
        <div class="card-body">
          <h3 class="card-title">Parameters</h3>

          {#if selectedEffect}
            <div class="space-y-4">
              <!-- Speed -->
              <div class="form-control">
                <label class="label">
                  <span class="label-text">Speed</span>
                  <span class="label-text-alt">{effectParams.speed.toFixed(2)}</span>
                </label>
                <input 
                  type="range" 
                  min="0.1" 
                  max="3.0" 
                  step="0.1"
                  bind:value={effectParams.speed}
                  class="range range-primary range-sm" 
                />
              </div>

              <!-- Intensity -->
              <div class="form-control">
                <label class="label">
                  <span class="label-text">Intensity</span>
                  <span class="label-text-alt">{effectParams.intensity.toFixed(2)}</span>
                </label>
                <input 
                  type="range" 
                  min="0.0" 
                  max="1.0" 
                  step="0.05"
                  bind:value={effectParams.intensity}
                  class="range range-secondary range-sm" 
                />
              </div>

              <!-- Brightness -->
              <div class="form-control">
                <label class="label">
                  <span class="label-text">Brightness</span>
                  <span class="label-text-alt">{effectParams.brightness.toFixed(2)}</span>
                </label>
                <input 
                  type="range" 
                  min="0.0" 
                  max="1.0" 
                  step="0.05"
                  bind:value={effectParams.brightness}
                  class="range range-accent range-sm" 
                />
              </div>

              <!-- Color -->
              <div class="form-control">
                <label class="label">
                  <span class="label-text">Color</span>
                </label>
                <input 
                  type="color" 
                  bind:value={effectParams.color}
                  class="w-full h-12 rounded-lg cursor-pointer" 
                />
              </div>

              <div class="divider"></div>

              <!-- Actions -->
              <div class="space-y-2">
                {#if errorMessage}
                  <div class="alert alert-error text-sm">
                    {errorMessage}
                  </div>
                {/if}
                <button 
                  class="btn btn-primary btn-block" 
                  on:click={applyEffect}
                  disabled={isApplying}
                >
                  {isApplying ? 'Applying...' : 'Apply Effect'}
                </button>
                <button 
                  class="btn btn-outline btn-block" 
                  on:click={stopEffect}
                >
                  Stop Effect
                </button>
              </div>
            </div>
          {:else}
            <div class="text-center py-8 text-base-content/50">
              Select an effect to configure
            </div>
          {/if}
        </div>
      </div>
    </div>
  </div>
</div>
