<script>
  import { onMount } from 'svelte';
  import { frames, sequencer, trigger } from '$lib/api.js';
  
  let frameList = [];
  let timelineList = [];
  let isLoading = true;
  let error = null;
  let activeTimeline = null;
  
  async function loadFrames() {
    try {
      frameList = await frames.list();
    } catch (err) {
      error = err.message;
      console.error('Error loading frames:', err);
    }
  }
  
  async function loadTimelines() {
    try {
      timelineList = await sequencer.list();
      const status = await sequencer.getStatus();
      activeTimeline = status.active_timeline;
    } catch (err) {
      error = err.message;
      console.error('Error loading timelines:', err);
    }
  }
  
  async function loadData() {
    try {
      isLoading = true;
      error = null;
      await Promise.all([loadFrames(), loadTimelines()]);
    } finally {
      isLoading = false;
    }
  }
  
  async function playTimeline(id) {
    try {
      await sequencer.play(id);
      activeTimeline = id;
      console.log(`Started timeline: ${id}`);
    } catch (err) {
      error = err.message;
      console.error('Error starting timeline:', err);
    }
  }
  
  async function stopTimeline(id) {
    try {
      await sequencer.stop(id);
      if (activeTimeline === id) {
        activeTimeline = null;
      }
      console.log(`Stopped timeline: ${id}`);
    } catch (err) {
      error = err.message;
      console.error('Error stopping timeline:', err);
    }
  }
  
  onMount(loadData);
</script>

<div class="container mx-auto p-4">
  <h1 class="text-2xl font-bold mb-6">Sequencer</h1>
  
  {#if error}
    <div class="alert alert-error mb-4">
      <div>Error: {error}</div>
    </div>
  {/if}
  
  {#if isLoading}
    <div class="loading loading-spinner loading-lg"></div>
  {:else}
    <!-- Active Timeline Indicator -->
    {#if activeTimeline}
      <div class="mb-6">
        <div class="card bg-success text-success-content">
          <div class="card-body">
            <h3 class="card-title">Active Timeline</h3>
            <p>Currently running: <strong>{activeTimeline}</strong></p>
            <div class="card-actions justify-end">
              <button 
                class="btn btn-secondary btn-sm" 
                on:click={() => stopTimeline(activeTimeline)}
              >
                Stop Timeline
              </button>
            </div>
          </div>
        </div>
      </div>
    {/if}
    
    <!-- Timelines Section -->
    <div class="card bg-base-100 shadow-xl mb-6">
      <div class="card-body">
        <h2 class="card-title">Timelines</h2>
        <p class="text-sm opacity-75 mb-4">Scheduled sequences of display events</p>
        
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {#each timelineList.timelines || [] as timeline}
            <div class="card bg-base-200 shadow">
              <div class="card-body">
                <h3 class="card-title">{timeline.name || timeline.id}</h3>
                <p class="text-sm opacity-75">Duration: {timeline.duration || 'N/A'}s</p>
                <div class="card-actions justify-end">
                  {#if activeTimeline === timeline.id}
                    <button 
                      class="btn btn-secondary btn-sm" 
                      on:click={() => stopTimeline(timeline.id)}
                    >
                      Stop
                    </button>
                  {:else}
                    <button 
                      class="btn btn-primary btn-sm" 
                      on:click={() => playTimeline(timeline.id)}
                    >
                      Play
                    </button>
                  {/if}
                </div>
              </div>
            </div>
          {/each}
        </div>
      </div>
    </div>
    
    <!-- Frames Section -->
    <div class="card bg-base-100 shadow-xl">
      <div class="card-body">
        <h2 class="card-title">Frames</h2>
        <p class="text-sm opacity-75 mb-4">Individual display frames that can be sequenced</p>
        
        <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {#each frameList.frames || [] as frame}
            <div class="card bg-base-200 shadow">
              <div class="card-body">
                <h3 class="card-title">{frame.name || frame.id}</h3>
                <p class="text-sm opacity-75">Type: {frame.type}</p>
                <div class="card-actions justify-end">
                  <button 
                    class="btn btn-primary btn-sm" 
                    on:click={() => trigger.playPreset(frame.id)}
                  >
                    Play Now
                  </button>
                </div>
              </div>
            </div>
          {/each}
        </div>
      </div>
    </div>
  {/if}
</div>