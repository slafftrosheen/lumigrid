<script lang="ts">
  import { onMount } from 'svelte';
  import { getSequence, saveSequence, getPresets } from '$lib/api/node';
  import type { Sequence, ScenePreset } from '$lib/api/node';
  import {
    createDefaultTrack,
    createDefaultSequence,
    cloneSequence,
    isSequenceDirty,
    stepDurationSeconds,
    TRACK_COUNT,
    STEP_COUNT,
    MIN_BPM,
    MAX_BPM,
    CHANNEL_PORTS,
  } from '$lib/api/timeline';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let loading = $state(true);
  let saving = $state(false);
  let activating = $state(false);
  let error = $state('');
  let toast = $state('');

  let seq = $state<Sequence | null>(null);
  let savedSnapshot = $state<Sequence | null>(null);
  let presets = $state<ScenePreset[]>([]);

  let selectedTrack = $state(0);
  let selectedStep = $state(0);

  const editTrack = $derived(seq?.tracks[selectedTrack] ?? null);
  const editStep = $derived(editTrack?.steps[selectedStep] ?? null);
  const isDirty = $derived(isSequenceDirty(seq, savedSnapshot));
  const isPlaying = $derived(Boolean(seq?.enabled));

  function presetName(idx: number): string {
    const p = presets.find(pr => pr.index === idx);
    return p ? p.name : `Scene ${idx + 1}`;
  }

  function selectStep(t: number, s: number) {
    selectedTrack = t;
    selectedStep = s;
  }

  let loadGen = 0;

  async function loadData(targetIp: string, gen: number) {
    loading = true;
    error = '';
    seq = null;
    savedSnapshot = null;
    presets = [];
    try {
      const [s, p] = await Promise.all([
        getSequence(targetIp).catch(() => null),
        getPresets(targetIp).catch(() => [] as ScenePreset[]),
      ]);
      if (gen !== loadGen) return;

      if (s && s.tracks) {
        for (let i = 0; i < TRACK_COUNT; i++) {
          if (!s.tracks[i]) s.tracks[i] = createDefaultTrack(i);
          while (s.tracks[i].steps.length < STEP_COUNT) {
            s.tracks[i].steps.push({ presetIndex: 0, lengthBeats: 4 });
          }
        }
        seq = cloneSequence(s);
        savedSnapshot = cloneSequence(s);
      } else {
        const def = createDefaultSequence();
        seq = cloneSequence(def);
        savedSnapshot = null;
      }
      presets = p;
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) {
        loading = false;
      }
    }
  }

  async function save() {
    if (!seq || saving) return;
    saving = true;
    error = '';
    try {
      await saveSequence(nodeIp, seq);
      savedSnapshot = cloneSequence(seq);
      toastMsg('Timeline saved to hardware');
    } catch (e) {
      error = String(e);
    } finally {
      saving = false;
    }
  }

  async function togglePlay() {
    if (!seq || activating) return;
    activating = true;
    error = '';
    try {
      seq.enabled = !seq.enabled;
      await saveSequence(nodeIp, seq);
      savedSnapshot = cloneSequence(seq);
      toastMsg(seq.enabled ? 'Timeline playback started' : 'Timeline playback stopped');
    } catch (e) {
      error = String(e);
    } finally {
      activating = false;
    }
  }

  function revert() {
    if (savedSnapshot) {
      seq = cloneSequence(savedSnapshot);
      toastMsg('Reverted edits to saved timeline');
    } else {
      seq = createDefaultSequence();
      toastMsg('Reset to default timeline');
    }
  }

  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => {
      if (toast === msg) toast = '';
    }, 2500);
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        if (nodeIp) {
          loadData(nodeIp, loadGen);
        } else {
          seq = null;
          savedSnapshot = null;
          presets = [];
          loading = false;
          error = '';
        }
      }
    });
    return unsub;
  });
</script>

<svelte:head><title>Timeline Studio — LumiGrid</title></svelte:head>

{#if !nodeIp}
  <div class="tl-empty">
    <p>No node selected. Select an active node in the header or fleet list.</p>
  </div>
{:else if loading}
  <div class="tl-loading">
    <div class="spinner"></div>
    <p>Loading timeline workspace…</p>
  </div>
{:else if seq}
  <div class="tl-root">
    <!-- Top Transport Header -->
    <header class="tl-transport">
      <div class="transport-left">
        <div class="tl-title-row">
          <h2>Timeline Studio</h2>
          <div class="status-badges">
            {#if isPlaying}
              <span class="badge badge-active" title="Timeline loop is actively running on the selected node">
                <span class="pulse-dot"></span> PLAYING
              </span>
            {:else}
              <span class="badge badge-idle">STOPPED</span>
            {/if}
            {#if isDirty}
              <span class="badge badge-edited">EDITED</span>
            {:else if savedSnapshot}
              <span class="badge badge-saved">SAVED</span>
            {/if}
          </div>
        </div>
      </div>

      <div class="transport-center">
        <!-- Play / Stop Toggle Button -->
        <button
          class="btn-playback"
          class:is-playing={isPlaying}
          onclick={togglePlay}
          disabled={activating}
          type="button"
          title={isPlaying ? 'Stop timeline playback' : 'Start timeline playback on node'}
        >
          {#if activating}
            <span>...</span>
          {:else if isPlaying}
            <span>⏹ STOP</span>
          {:else}
            <span>▶ PLAY</span>
          {/if}
        </button>

        <!-- BPM Control -->
        <div class="bpm-control">
          <span class="bpm-label">BPM</span>
          <input
            type="number"
            min={MIN_BPM}
            max={MAX_BPM}
            bind:value={seq.bpm}
            title="Beats Per Minute"
          />
        </div>

        <div class="beat-dots" style="--anim-dur: {(60 / (seq.bpm || 120)).toFixed(3)}s;">
          {#each Array(4) as _, i}
            <span class="beat-dot" class:pulsing={isPlaying} style="animation-delay: {i * (60 / (seq.bpm || 120))}s;"></span>
          {/each}
        </div>
      </div>

      <div class="transport-right">
        <button
          class="btn-secondary"
          onclick={revert}
          disabled={!isDirty}
          title="Revert modifications to last saved timeline"
          type="button"
        >
          Revert
        </button>
        <button
          class="btn-save"
          onclick={save}
          disabled={saving}
          type="button"
          title="Save timeline to node"
        >
          {saving ? 'Saving...' : 'Save'}
        </button>
      </div>
    </header>

    <!-- Visual 16-Channel Beat Grid -->
    <div class="tl-grid-wrap">
      <table class="tl-grid">
        <thead>
          <tr>
            <th class="col-track">Track</th>
            <th class="col-ch">Channel</th>
            {#each Array(STEP_COUNT) as _, i}
              <th class="col-step">{(i + 1).toString().padStart(2, '0')}</th>
            {/each}
          </tr>
        </thead>
        <tbody>
          {#each seq.tracks.slice(0, TRACK_COUNT) as track, t}
            <tr
              class="track-row"
              class:disabled={!track.enabled}
              class:selected={selectedTrack === t}
              class:pwm-track={track.channelId >= 8}
            >
              <!-- Track label -->
              <td class="cell-track">
                <div class="track-header-cell">
                  <button
                    class="track-enable-btn"
                    class:active={track.enabled}
                    onclick={() => (track.enabled = !track.enabled)}
                    title={track.enabled ? 'Disable track' : 'Enable track'}
                    type="button"
                    aria-label="Toggle Track {t + 1}"
                  >
                    <span class="track-dot"></span>
                  </button>
                  <button
                    class="track-name-btn"
                    onclick={() => selectStep(t, 0)}
                    type="button"
                  >
                    Track {t + 1} ({CHANNEL_PORTS[track.channelId] ?? `Ch ${track.channelId}`})
                  </button>
                </div>
              </td>

              <!-- Output Channel mapping -->
              <td class="cell-ch">
                <select class="ch-select" bind:value={track.channelId}>
                  {#each CHANNEL_PORTS as port, ci}
                    <option value={ci}>{port}</option>
                  {/each}
                </select>
              </td>

              <!-- 16 Step cells -->
              {#each track.steps.slice(0, STEP_COUNT) as step, s}
                {@const isStepActive = s < track.numSteps && track.enabled}
                {@const isStepSelected = selectedTrack === t && selectedStep === s}
                <td class="cell-step">
                  <button
                    class="step-btn"
                    class:active={isStepActive}
                    class:selected={isStepSelected}
                    onclick={() => selectStep(t, s)}
                    type="button"
                    title="Track {t + 1} ({CHANNEL_PORTS[track.channelId] ?? track.channelId}), Step {s + 1}: {presetName(step.presetIndex)} ({step.lengthBeats} beats)"
                  >
                    {#if s < track.numSteps}
                      <span class="step-preset">S{step.presetIndex + 1}</span>
                      <span class="step-beats">{step.lengthBeats}b</span>
                    {:else}
                      <span class="step-empty">·</span>
                    {/if}
                  </button>
                </td>
              {/each}
            </tr>
          {/each}
        </tbody>
      </table>
    </div>

    <!-- Inspector / Step & Track Editor -->
    <div class="inspector-grid">
      <!-- Step Details -->
      <section class="panel step-editor">
        {#if editTrack && editStep}
          <div class="panel-header">
            <h3>Track {selectedTrack + 1} · {CHANNEL_PORTS[editTrack.channelId] ?? editTrack.channelId} · Step {selectedStep + 1}</h3>
            <div class="panel-header-actions">
              <label class="inline-toggle">
                <input
                  type="checkbox"
                  checked={selectedStep < editTrack.numSteps}
                  onchange={e => {
                    if ((e.target as HTMLInputElement).checked) {
                      if (selectedStep >= editTrack.numSteps) editTrack.numSteps = selectedStep + 1;
                    } else {
                      if (selectedStep < editTrack.numSteps) editTrack.numSteps = selectedStep;
                    }
                  }}
                />
                <span>Step Active</span>
              </label>
            </div>
          </div>

          {#if selectedStep < editTrack.numSteps}
            <div class="editor-fields">
              <label class="field">
                <span>Scene</span>
                <select bind:value={editStep.presetIndex}>
                  {#if presets.length > 0}
                    {#each presets as p}
                      <option value={p.index}>{p.index + 1}. {p.name}</option>
                    {/each}
                  {:else}
                    {#each Array(32) as _, i}
                      <option value={i}>Scene {i + 1}</option>
                    {/each}
                  {/if}
                </select>
              </label>

              <label class="field">
                <span>Length (beats)</span>
                <input type="number" min="1" max="64" bind:value={editStep.lengthBeats} />
              </label>

              <div class="field field-duration-preview">
                <span>Duration</span>
                <span class="dur-val">
                  {stepDurationSeconds(editStep.lengthBeats, seq.bpm).toFixed(2)}s
                </span>
              </div>
            </div>
          {:else}
            <p class="editor-inactive">
              Step {selectedStep + 1} is currently inactive for {CHANNEL_PORTS[editTrack.channelId] ?? `channel ${editTrack.channelId}`} (active range: 1–{editTrack.numSteps}). Check "Step Active" to enable it.
            </p>
          {/if}
        {/if}
      </section>

      <!-- Track Details -->
      <section class="panel track-editor">
        {#if editTrack}
          <div class="panel-header">
            <h3>Track {selectedTrack + 1} ({CHANNEL_PORTS[editTrack.channelId] ?? editTrack.channelId}) Settings</h3>
            <label class="inline-toggle">
              <input type="checkbox" bind:checked={editTrack.enabled} />
              <span>Track Enabled</span>
            </label>
          </div>

          <div class="track-fields">
            <label class="field">
              <span>Channel Output</span>
              <select bind:value={editTrack.channelId}>
                {#each CHANNEL_PORTS as port, ci}
                  <option value={ci}>{port} ({ci < 8 ? 'Addressable Lane' : 'PWM Dimmer'})</option>
                {/each}
              </select>
            </label>

            <label class="field">
              <span>Active Steps (1-16)</span>
              <input type="number" min="1" max="16" bind:value={editTrack.numSteps} />
            </label>
          </div>

          <!-- Quick Step Selector Strip -->
          <div class="mini-steps-wrap">
            <span class="mini-label">Active Timeline Steps:</span>
            <div class="mini-steps-list">
              {#each editTrack.steps.slice(0, editTrack.numSteps) as st, s}
                <button
                  class="mini-step-btn"
                  class:selected={selectedStep === s}
                  onclick={() => selectStep(selectedTrack, s)}
                  type="button"
                >
                  <span class="mini-idx">{s + 1}</span>
                  <span class="mini-preset">S{st.presetIndex + 1}</span>
                  <span class="mini-beats">{st.lengthBeats}b</span>
                </button>
              {/each}
            </div>
          </div>
        {/if}
      </section>
    </div>

    {#if toast}
      <div class="toast">{toast}</div>
    {/if}
    {#if error}
      <div class="error-bar">{error}</div>
    {/if}
  </div>
{/if}

<style>
  .tl-root {
    width: 100%;
    max-width: 1440px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    position: relative;
    color: var(--dash-text);
  }

  .tl-loading, .tl-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    min-height: 300px;
    color: #a1a1a6;
  }
  .spinner {
    width: 32px;
    height: 32px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  /* Transport Bar */
  .tl-transport {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: var(--dash-surface);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 0.75rem 1.25rem;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .transport-left {
    display: flex;
    align-items: center;
    gap: 1rem;
  }
  .tl-title-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }
  .tl-title-row h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 600;
  }
  .status-badges {
    display: flex;
    align-items: center;
    gap: 0.35rem;
  }
  .badge {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    font-size: 0.68rem;
    font-weight: 700;
    padding: 0.2rem 0.5rem;
    border-radius: 6px;
    text-transform: uppercase;
  }
  .badge-active {
    background: rgba(48, 209, 88, 0.15);
    border: 1px solid rgba(48, 209, 88, 0.4);
    color: #30d158;
  }
  .badge-idle {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #86868b;
  }
  .badge-edited {
    background: rgba(255, 159, 10, 0.15);
    border: 1px solid rgba(255, 159, 10, 0.4);
    color: #ff9f0a;
  }
  .badge-saved {
    background: rgba(48, 209, 88, 0.12);
    border: 1px solid rgba(48, 209, 88, 0.3);
    color: var(--dash-ok);
  }
  .pulse-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #30d158;
    animation: pulse 1.2s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.3; transform: scale(0.8); }
  }

  .transport-center {
    display: flex;
    align-items: center;
    gap: 1.25rem;
  }
  .btn-playback {
    background: rgba(48, 209, 88, 0.15);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.4);
    border-radius: 8px;
    padding: 0.45rem 1rem;
    font-size: 0.82rem;
    font-weight: 700;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-playback:hover:not(:disabled) {
    background: rgba(48, 209, 88, 0.25);
  }
  .btn-playback.is-playing {
    background: rgba(255, 69, 58, 0.15);
    color: #ff453a;
    border-color: rgba(255, 69, 58, 0.4);
  }
  .btn-playback.is-playing:hover:not(:disabled) {
    background: rgba(255, 69, 58, 0.25);
  }

  .bpm-control {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.8rem;
    color: #86868b;
  }
  .bpm-label {
    font-weight: 600;
  }
  .bpm-control input {
    width: 65px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.3rem 0.4rem;
    font-family: monospace;
    text-align: center;
  }
  .bpm-control input:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }

  .beat-dots {
    display: flex;
    gap: 0.35rem;
  }
  .beat-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.15);
  }
  .beat-dot.pulsing {
    animation: beat-flash var(--anim-dur, 0.5s) infinite;
  }
  @keyframes beat-flash {
    0%, 100% { background: rgba(255, 255, 255, 0.15); transform: scale(1); }
    15% { background: #30d158; transform: scale(1.3); }
  }

  .transport-right {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .btn-save {
    background: var(--dash-accent);
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0.45rem 1rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .btn-save:hover:not(:disabled) {
    background: var(--dash-accent-hover);
  }
  .btn-save:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .btn-secondary {
    background: rgba(255, 255, 255, 0.06);
    color: #a1a1a6;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    padding: 0.45rem 0.75rem;
    font-size: 0.82rem;
    cursor: pointer;
  }
  .btn-secondary:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }

  /* Grid Wrap */
  .tl-grid-wrap {
    overflow: auto;
    scrollbar-color: rgba(255,255,255,.16) transparent;
    scrollbar-width: thin;
    background: rgba(20, 20, 22, .74);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 0.65rem;
    box-shadow: var(--dash-shadow);
    max-height: min(58dvh, 720px);
  }
  .tl-grid {
    width: 100%;
    min-width: 1080px;
    border-collapse: separate;
    border-spacing: 0;
    table-layout: fixed;
  }
  .tl-grid th {
    position: sticky;
    top: 0;
    z-index: 6;
    background: rgba(18,18,20,.985);
    padding: 0.4rem 0.2rem;
    font-size: 0.7rem;
    font-weight: 600;
    color: #86868b;
    text-align: center;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    font-family: monospace;
  }
  .col-track {
    width: 140px;
    text-align: left !important;
    padding-left: 0.5rem !important;
    position: sticky;
    left: 0;
    top: 0;
    z-index: 9;
    background: rgba(18,18,20,.99);
  }
  .col-ch {
    width: 74px;
    position: sticky;
    left: 140px;
    top: 0;
    z-index: 9;
    background: rgba(18,18,20,.99);
    box-shadow: 8px 0 18px rgba(0,0,0,.18);
  }
  .col-step {
    width: 50px;
  }

  .track-row {
    transition: background 0.15s ease;
  }
  .track-row:hover {
    background: rgba(255, 255, 255, 0.03);
  }
  .track-row.disabled {
    opacity: 0.4;
  }
  .track-row.selected {
    background: rgba(var(--dash-accent-rgb), 0.08);
  }
  .track-row.pwm-track.selected {
    background: color-mix(in oklab, var(--dash-warm) 10%, transparent);
  }

  .cell-track {
    padding: 0.3rem 0.4rem;
    position: sticky;
    left: 0;
    z-index: 5;
    background: rgba(18,18,20,.97);
  }
  .cell-ch {
    position: sticky;
    left: 140px;
    z-index: 5;
    background: rgba(18,18,20,.97);
    box-shadow: 8px 0 18px rgba(0,0,0,.14);
  }
  .track-row.selected .cell-track,
  .track-row.selected .cell-ch {
    background: color-mix(in oklab, rgba(18,18,20,.97) 86%, var(--dash-accent) 14%);
  }
  .track-row.pwm-track.selected .cell-track,
  .track-row.pwm-track.selected .cell-ch {
    background: color-mix(in oklab, rgba(18,18,20,.97) 86%, var(--dash-warm) 14%);
  }
  .track-header-cell {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .track-enable-btn {
    background: none;
    border: none;
    padding: 0;
    cursor: pointer;
    display: flex;
    align-items: center;
  }
  .track-dot {
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.15);
    transition: background 0.15s ease;
  }
  .track-enable-btn.active .track-dot {
    background: #30d158;
    box-shadow: 0 0 6px rgba(48, 209, 88, 0.6);
  }
  .track-name-btn {
    background: none;
    border: none;
    color: #f5f5f7;
    font-size: 0.8rem;
    font-weight: 600;
    cursor: pointer;
    padding: 0;
    text-align: left;
  }
  .cell-ch {
    padding: 0.2rem;
  }
  .ch-select {
    width: 100%;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 4px;
    color: #a1a1a6;
    font-size: 0.72rem;
    padding: 0.15rem;
    text-align: center;
  }

  .cell-step {
    padding: 0.2rem;
  }
  .step-btn {
    width: 100%;
    height: 44px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
    padding: 0;
  }
  .step-btn:hover {
    background: rgba(255, 255, 255, 0.08);
    border-color: rgba(255, 255, 255, 0.15);
  }
  .step-btn.active {
    background: rgba(var(--dash-accent-rgb), 0.12);
    border-color: rgba(var(--dash-accent-rgb), 0.3);
  }
  .step-btn.selected {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.22);
    box-shadow: 0 0 8px rgba(var(--dash-accent-rgb), 0.35);
  }
  .pwm-track .step-btn.active {
    background: color-mix(in oklab, var(--dash-warm) 12%, transparent);
    border-color: color-mix(in oklab, var(--dash-warm) 32%, transparent);
  }
  .pwm-track .step-btn.selected {
    border-color: var(--dash-warm);
    background: color-mix(in oklab, var(--dash-warm) 22%, transparent);
    box-shadow: 0 0 8px color-mix(in oklab, var(--dash-warm) 38%, transparent);
  }
  .step-preset {
    font-size: 0.75rem;
    font-weight: 700;
    color: #f5f5f7;
    font-family: monospace;
  }
  .step-beats {
    font-size: 0.62rem;
    color: #86868b;
    font-family: monospace;
  }
  .step-empty {
    color: rgba(255, 255, 255, 0.1);
    font-size: 0.8rem;
  }

  /* Inspector Grid */
  .inspector-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 1.25rem;
  }
  .panel {
    background: var(--dash-surface);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1rem 1.25rem;
    box-shadow: var(--dash-shadow);
  }
  .panel-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 0.85rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    padding-bottom: 0.5rem;
  }
  .panel-header h3 {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 600;
  }
  .inline-toggle {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.8rem;
    color: #86868b;
    cursor: pointer;
  }
  .inline-toggle input {
    accent-color: var(--dash-accent);
    width: 14px;
    height: 14px;
  }

  .editor-fields, .track-fields {
    display: flex;
    gap: 1rem;
    flex-wrap: wrap;
    align-items: flex-end;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .field span {
    font-size: 0.72rem;
    color: #86868b;
    font-weight: 600;
  }
  .field select, .field input[type='number'] {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.4rem 0.6rem;
    min-width: 140px;
  }
  .field-duration-preview {
    background: rgba(var(--dash-accent-rgb), 0.08);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.2);
    border-radius: 6px;
    padding: 0.4rem 0.75rem;
  }
  .dur-val {
    font-family: monospace;
    font-size: 0.9rem;
    font-weight: 700;
    color: var(--dash-accent);
  }
  .editor-inactive {
    color: #86868b;
    font-size: 0.85rem;
    margin: 0;
  }

  .mini-steps-wrap {
    margin-top: 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .mini-label {
    font-size: 0.72rem;
    color: #86868b;
  }
  .mini-steps-list {
    display: flex;
    gap: 0.35rem;
    flex-wrap: wrap;
  }
  .mini-step-btn {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    padding: 0.3rem 0.5rem;
    cursor: pointer;
    min-width: 48px;
    transition: all 0.12s ease;
  }
  .mini-step-btn:hover {
    background: rgba(255, 255, 255, 0.08);
  }
  .mini-step-btn.selected {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.18);
  }
  .mini-idx {
    font-size: 0.6rem;
    color: #86868b;
    font-family: monospace;
  }
  .mini-preset {
    font-size: 0.75rem;
    font-weight: 700;
    color: #f5f5f7;
    font-family: monospace;
  }
  .mini-beats {
    font-size: 0.6rem;
    color: #86868b;
    font-family: monospace;
  }

  .toast {
    position: fixed;
    bottom: 1.5rem;
    right: 1.5rem;
    background: rgba(48, 209, 88, 0.92);
    color: #fff;
    padding: 0.6rem 1.2rem;
    border-radius: 10px;
    font-size: 0.85rem;
    font-weight: 500;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
    z-index: 100;
  }
  .error-bar {
    position: fixed;
    bottom: 1.5rem;
    left: 50%;
    transform: translateX(-50%);
    background: rgba(255, 69, 58, 0.95);
    color: #fff;
    padding: 0.6rem 1.2rem;
    border-radius: 10px;
    font-size: 0.85rem;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
    z-index: 100;
  }

  @media (max-width: 900px) {
    .inspector-grid { grid-template-columns: 1fr; }
    .tl-transport { align-items: stretch; }
    .transport-left { flex: 1 1 100%; }
    .transport-center { flex: 1 1 auto; flex-wrap: wrap; }
    .transport-right { margin-left: auto; }
  }

  @media (max-width: 620px) {
    .tl-transport { padding: .85rem; }
    .tl-title-row {
      align-items: flex-start;
      flex-direction: column;
      gap: .45rem;
    }
    .transport-center,
    .transport-right {
      width: 100%;
      justify-content: space-between;
    }
    .btn-playback { flex: 1; }
    .transport-right button { flex: 1; min-height: 40px; }
    .tl-grid-wrap { margin-inline: -1rem; border-radius: 0; border-inline: 0; }
    .panel { padding: 1rem; }
    .panel-header {
      align-items: flex-start;
      flex-direction: column;
      gap: .65rem;
    }
    .editor-fields, .track-fields { align-items: stretch; flex-direction: column; }
    .field select, .field input[type='number'] { width: 100%; min-width: 0; }
  }
</style>
