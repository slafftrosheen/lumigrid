<script>
  import { onMount } from 'svelte';
  import Timeline from '$lib/Timeline.svelte';
  import { sequencer as sequencerApi } from '$lib/api.js';
  
  let sequences = [];
  let activeSequence = null;
  let playing = false;
  let playhead = 0;
  let duration = 10000; // 10 seconds default
  let isLoading = true;
  let errorMessage = '';

  onMount(async () => {
    await fetchSequences();
  });

  async function fetchSequences() {
    isLoading = true;
    errorMessage = '';
    try {
      const data = await sequencerApi.list();
      // API returns { sequences: [...] }
      sequences = data.sequences || data || [];
    } catch (error) {
      console.error('Failed to fetch sequences:', error);
      errorMessage = 'Failed to load sequences';
    } finally {
      isLoading = false;
    }
  }

  async function createSequence() {
    const name = prompt('Sequence name:');
    if (!name) return;

    try {
      await sequencerApi.create({
        name,
        bpm: 120,
        time_signature: 0x44
      });
      await fetchSequences();
    } catch (error) {
      console.error('Failed to create sequence:', error);
      errorMessage = 'Failed to create sequence';
    }
  }

  async function playSequence() {
    if (!activeSequence) return;

    try {
      await sequencerApi.play(activeSequence.id);
      playing = true;
    } catch (error) {
      console.error('Failed to play sequence:', error);
      errorMessage = 'Failed to play sequence';
    }
  }

  async function pauseSequence() {
    if (!activeSequence) return;

    try {
      await sequencerApi.pause(activeSequence.id);
      playing = false;
    } catch (error) {
      console.error('Failed to pause sequence:', error);
      errorMessage = 'Failed to pause sequence';
    }
  }

  async function stopSequence() {
    if (!activeSequence) return;

    try {
      await sequencerApi.stop(activeSequence.id);
      playing = false;
      playhead = 0;
    } catch (error) {
      console.error('Failed to stop sequence:', error);
      errorMessage = 'Failed to stop sequence';
    }
  }

  function selectSequence(seq) {
    activeSequence = seq;
    errorMessage = '';
  }
</script>

<svelte:head>
  <title>Sequencer - LumiGrid</title>
</svelte:head>

<div class="space-y-6">
  <div class="flex justify-between items-center">
    <h1 class="text-3xl font-bold">Multitrack Sequencer</h1>
    <button class="btn btn-primary" on:click={createSequence}>
      + New Sequence
    </button>
  </div>

  <!-- Transport Controls -->
  <div class="card bg-base-200 shadow-xl">
    <div class="card-body">
      <div class="flex items-center justify-between">
        <div class="btn-group">
          <button 
            class="btn"
            class:btn-active={playing}
            on:click={playing ? pauseSequence : playSequence}
            disabled={!activeSequence}
          >
            {#if playing}
              ⏸️ Pause
            {:else}
              ▶️ Play
            {/if}
          </button>
          <button class="btn" on:click={stopSequence} disabled={!activeSequence}>
            ⏹️ Stop
          </button>
        </div>

        <div class="flex items-center gap-4">
          <span class="text-sm">
            {Math.floor(playhead / 1000)}s / {Math.floor(duration / 1000)}s
          </span>
          
          {#if activeSequence}
            <div class="badge badge-outline">
              {activeSequence.bpm} BPM
            </div>
            <div class="badge badge-outline">
              {activeSequence.tracks?.length || 0} Tracks
            </div>
          {/if}
        </div>

        <div class="form-control">
          <label class="label cursor-pointer">
            <span class="label-text mr-2">Loop</span>
            <input type="checkbox" class="checkbox checkbox-primary" />
          </label>
        </div>
      </div>
    </div>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-4 gap-6">
    <!-- Sequence List -->
    <div class="lg:col-span-1">
      <div class="card bg-base-200 shadow-xl">
        <div class="card-body">
          <h3 class="card-title">Sequences</h3>
          <div class="space-y-2">
            {#each sequences as seq}
              <button
                class="btn btn-sm w-full justify-start"
                class:btn-primary={activeSequence?.id === seq.id}
                on:click={() => selectSequence(seq)}
              >
                {seq.name}
              </button>
            {:else}
              <div class="text-center py-4 text-base-content/50">
                No sequences yet
              </div>
            {/each}
          </div>
        </div>
      </div>
    </div>

    <!-- Timeline -->
    <div class="lg:col-span-3">
      <div class="card bg-base-200 shadow-xl">
        <div class="card-body">
          <h3 class="card-title">Timeline</h3>
          
          {#if activeSequence}
            <Timeline sequence={activeSequence} {playhead} {playing} />
            
            <!-- Track Controls -->
            <div class="mt-4 space-y-2">
              {#if activeSequence.tracks}
                {#each activeSequence.tracks as track}
                  <div class="flex items-center gap-2 p-2 bg-base-300 rounded">
                    <input type="checkbox" checked class="checkbox checkbox-sm" />
                    <span class="flex-1">{track.name}</span>
                    <span class="badge badge-sm">Ch {track.channel}</span>
                    <button class="btn btn-xs">M</button>
                    <button class="btn btn-xs">S</button>
                  </div>
                {/each}
              {/if}
            </div>
          {:else}
            <div class="text-center py-12 text-base-content/50">
              Select or create a sequence to start
            </div>
          {/if}
        </div>
      </div>
    </div>
  </div>
</div>
