<script lang="ts">
  import { onMount } from 'svelte';
  import { getSequence, saveSequence, getPresets } from '$lib/api/node';
  import type { Sequence, SequenceTrack, SequenceStep, ScenePreset } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';

  let nodeIp = $state('');
  let loading = $state(true);
  let saving = $state(false);
  let error = $state('');
  let toast = $state('');

  let seq = $state<Sequence | null>(null);
  let presets = $state<ScenePreset[]>([]);

  let selectedTrack = $state(0);
  let selectedStep = $state(0);

  let editTrack = $derived(seq?.tracks[selectedTrack] ?? null);
  let editStep = $derived(editTrack?.steps[selectedStep] ?? null);

  const TRACK_COUNT = 8;
  const STEP_COUNT = 16;
  const MAX_PRESETS = 32;

  function defaultStep(): SequenceStep {
    return { presetIndex: 0, lengthBeats: 4 };
  }

  function defaultTrack(ch: number): SequenceTrack {
    return {
      channelId: ch,
      numSteps: 4,
      enabled: true,
      steps: Array.from({ length: STEP_COUNT }, () => defaultStep()),
    };
  }

  function defaultSequence(): Sequence {
    return {
      bpm: 120,
      enabled: false,
      tracks: Array.from({ length: TRACK_COUNT }, (_, i) => defaultTrack(i)),
    };
  }

  async function load() {
    loading = true;
    error = '';
    try {
      const [s, p] = await Promise.all([
        getSequence(nodeIp).catch(() => null),
        getPresets(nodeIp).catch(() => [] as ScenePreset[]),
      ]);
      if (s && s.tracks) {
        for (let i = 0; i < TRACK_COUNT; i++) {
          if (!s.tracks[i]) s.tracks[i] = defaultTrack(i);
          while (s.tracks[i].steps.length < STEP_COUNT) s.tracks[i].steps.push(defaultStep());
        }
        seq = s;
      } else {
        seq = defaultSequence();
      }
      presets = p;
    } catch (e) {
      error = String(e);
    }
    loading = false;
  }

  async function save() {
    if (!seq || saving) return;
    saving = true;
    error = '';
    try {
      await saveSequence(nodeIp, seq);
      toast = 'Sequence saved';
      setTimeout(() => (toast = ''), 2000);
    } catch (e) {
      error = String(e);
    }
    saving = false;
  }

  function presetName(idx: number): string {
    const p = presets.find(pr => pr.index === idx);
    return p ? p.name : `Preset ${idx}`;
  }

  function toastFlash(msg: string) {
    toast = msg;
    setTimeout(() => (toast = ''), 2000);
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    if (nodeIp) load();
    else loading = false;
  });
</script>

{#if !nodeIp}
  <p class="loading">No node selected. Add a node IP in the sidebar.</p>
{:else if loading}
  <p class="loading">Loading timeline…</p>
{:else if seq}
  <div class="tl-root">
    <!-- ── Transport bar ─────────────────────────────────────────────────────── -->
    <header class="tl-transport">
      <div class="transport-left">
        <h2>Timeline</h2>
        <label class="enable-toggle">
          <input type="checkbox" bind:checked={seq.enabled} />
          <span>Enabled</span>
        </label>
      </div>
      <div class="transport-center">
        <label class="bpm-control">
          <span>BPM</span>
          <input type="number" min="10" max="500" bind:value={seq.bpm} />
        </label>
        <div class="beat-indicator">
          {#each Array(4) as _, i}
            <span class="beat-dot" style="animation-delay: {i * (60 / seq.bpm)}s"></span>
          {/each}
        </div>
      </div>
      <div class="transport-right">
        <button class="btn-save" onclick={save} disabled={saving}>
          {saving ? 'Saving…' : 'Save'}
        </button>
      </div>
    </header>

    <!-- ── Track grid ────────────────────────────────────────────────────────── -->
    <div class="tl-grid-wrap">
      <table class="tl-grid">
        <thead>
          <tr>
            <th class="col-track">Track</th>
            <th class="col-ch">Ch</th>
            {#each Array(STEP_COUNT) as _, i}
              <th class="col-step">{i + 1}</th>
            {/each}
          </tr>
        </thead>
        <tbody>
          {#each seq.tracks.slice(0, TRACK_COUNT) as track, t}
            <tr class="track-row" class:disabled={!track.enabled} class:selected={selectedTrack === t}>
              <td class="cell-track">
                <button class="track-label" onclick={() => { selectedTrack = t; selectedStep = 0; }}>
                  <span class="track-enable-dot" class:active={track.enabled}
                    onclick={(e) => { e.stopPropagation(); track.enabled = !track.enabled; }}></span>
                  <span>T{t + 1}</span>
                </button>
              </td>
              <td class="cell-ch">
                <select class="ch-select" bind:value={track.channelId}>
                  {#each Array(8) as _, ci}
                    <option value={ci}>{ci}</option>
                  {/each}
                </select>
              </td>
              {#each track.steps.slice(0, STEP_COUNT) as step, s}
                <td
                  class="cell-step"
                  class:active={s < track.numSteps && track.enabled}
                  class:selected={selectedTrack === t && selectedStep === s}
                  class:current={false}
                  onclick={() => { selectedTrack = t; selectedStep = s; }}
                >
                  {#if s < track.numSteps}
                    <div class="step-inner" title={presetName(step.presetIndex)}>
                      <span class="step-preset">{step.presetIndex}</span>
                      <span class="step-beats">{step.lengthBeats}b</span>
                    </div>
                  {:else}
                    <span class="step-empty">·</span>
                  {/if}
                </td>
              {/each}
            </tr>
          {/each}
        </tbody>
      </table>
    </div>

    <!-- ── Step editor ───────────────────────────────────────────────────────── -->
    <section class="step-editor">
      {#if editTrack && editStep}
        <div class="editor-header">
          <h3>Track {selectedTrack + 1} · Step {selectedStep + 1}</h3>
          <div class="editor-header-actions">
            <label class="inline-toggle">
              <input type="checkbox" checked={selectedStep < editTrack.numSteps}
                onchange={(e) => {
                  if ((e.target as HTMLInputElement).checked) {
                    if (selectedStep >= editTrack.numSteps) editTrack.numSteps = selectedStep + 1;
                  } else {
                    if (selectedStep < editTrack.numSteps) editTrack.numSteps = selectedStep;
                  }
                }} />
              <span>Active</span>
            </label>
            <label class="inline-field">
              <span>Steps</span>
              <input type="number" min="1" max="16" bind:value={editTrack.numSteps} />
            </label>
          </div>
        </div>

        {#if selectedStep < editTrack.numSteps}
          <div class="editor-fields">
            <label class="field">
              <span>Preset</span>
              <select bind:value={editStep.presetIndex}>
                {#each Array(MAX_PRESETS) as _, i}
                  <option value={i}>{presetName(i)}</option>
                {/each}
              </select>
            </label>
            <label class="field">
              <span>Length (beats)</span>
              <input type="number" min="1" max="64" bind:value={editStep.lengthBeats} />
            </label>
            <div class="field field-duration-preview">
              <span>Duration</span>
              <span class="dur-val">{((editStep.lengthBeats * 60) / seq!.bpm).toFixed(2)}s</span>
            </div>
          </div>
        {:else}
          <p class="editor-inactive">Step {selectedStep + 1} is outside the active range (1–{editTrack.numSteps}). Enable it to edit.</p>
        {/if}
      {/if}
    </section>

    <!-- ── Track detail ──────────────────────────────────────────────────────── -->
    <section class="track-detail">
      {#if editTrack}
        <h3>Track {selectedTrack + 1} Settings</h3>
        <div class="track-fields">
          <label class="field">
            <span>Channel</span>
            <select bind:value={editTrack.channelId}>
              {#each Array(8) as _, ci}
                <option value={ci}>Channel {ci}</option>
              {/each}
            </select>
          </label>
          <label class="field">
            <span>Active steps</span>
            <input type="number" min="1" max="16" bind:value={editTrack.numSteps} />
          </label>
          <label class="inline-toggle">
            <input type="checkbox" bind:checked={editTrack.enabled} />
            <span>Track enabled</span>
          </label>
        </div>
        <div class="track-steps-list">
          {#each editTrack.steps.slice(0, editTrack.numSteps) as step, s}
            <div class="mini-step" class:selected={selectedStep === s}
              onclick={() => selectedStep = s}>
              <span class="mini-idx">{s + 1}</span>
              <span class="mini-preset" title={presetName(step.presetIndex)}>P{step.presetIndex}</span>
              <span class="mini-beats">{step.lengthBeats}b</span>
            </div>
          {/each}
        </div>
      {/if}
    </section>

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
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    position: relative;
    color: #f5f5f7;
  }
  .loading { color: #a1a1a6; padding: 2rem; }

  /* Transport bar */
  .tl-transport {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: .75rem 1.25rem;
    gap: 1rem;
  }
  .transport-left { display: flex; align-items: center; gap: 1rem; }
  .transport-left h2 { margin: 0; font-size: 1.15rem; font-weight: 700; }
  .transport-center { display: flex; align-items: center; gap: 1rem; }
  .transport-right { display: flex; align-items: center; gap: .75rem; }

  .enable-toggle {
    display: flex; align-items: center; gap: .4rem;
    font-size: .82rem; color: #a1a1a6; cursor: pointer;
  }
  .enable-toggle input[type="checkbox"] {
    accent-color: #ff2d55; width: 16px; height: 16px;
  }

  .bpm-control {
    display: flex; align-items: center; gap: .4rem;
    font-size: .82rem; color: #a1a1a6;
  }
  .bpm-control input {
    width: 64px; background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.12); border-radius: 8px;
    color: #f5f5f7; font-size: .9rem; padding: .3rem .5rem;
    font-family: monospace; text-align: center;
  }
  .bpm-control input:focus { outline: none; border-color: #ff2d55; }

  .beat-indicator { display: flex; gap: .35rem; }
  .beat-dot {
    width: 8px; height: 8px; border-radius: 50%;
    background: rgba(255,45,85,.25);
    animation: beat-pulse calc(4 * 60s / var(--bpm, 120) * 1s) infinite;
  }
  @keyframes beat-pulse {
    0%, 100% { background: rgba(255,45,85,.2); transform: scale(1); }
    10% { background: #ff2d55; transform: scale(1.3); }
  }

  .btn-save {
    background: rgba(255,45,85,.88); color: #fff; border: none;
    border-radius: 10px; padding: .5rem 1.25rem; font-size: .88rem;
    font-weight: 600; cursor: pointer; transition: background .2s, opacity .2s;
  }
  .btn-save:hover:not(:disabled) { background: #ff3b30; }
  .btn-save:disabled { opacity: .5; cursor: default; }

  /* Grid */
  .tl-grid-wrap {
    overflow-x: auto;
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: .5rem;
  }
  .tl-grid {
    width: 100%;
    border-collapse: collapse;
    table-layout: fixed;
    font-size: .78rem;
  }
  .tl-grid th {
    padding: .35rem .2rem;
    font-size: .68rem;
    font-weight: 600;
    color: #a1a1a6;
    text-align: center;
    border-bottom: 1px solid rgba(255,255,255,.06);
    white-space: nowrap;
  }
  .col-track { width: 60px; text-align: left !important; padding-left: .5rem !important; }
  .col-ch { width: 44px; }
  .col-step { width: 52px; }

  .track-row { transition: background .15s; }
  .track-row:hover { background: rgba(255,255,255,.03); }
  .track-row.disabled { opacity: .4; }
  .track-row.selected { background: rgba(255,45,85,.06); }

  .cell-track { padding: .25rem .4rem; }
  .track-label {
    display: flex; align-items: center; gap: .35rem;
    background: none; border: none; color: #f5f5f7;
    font-size: .8rem; font-weight: 600; cursor: pointer; padding: .2rem;
  }
  .track-enable-dot {
    width: 8px; height: 8px; border-radius: 50%;
    background: rgba(255,255,255,.15); cursor: pointer;
    transition: background .2s;
  }
  .track-enable-dot.active { background: #30d158; }

  .cell-ch { padding: .15rem; }
  .ch-select {
    width: 100%; background: rgba(255,255,255,.05);
    border: 1px solid rgba(255,255,255,.08); border-radius: 4px;
    color: #a1a1a6; font-size: .72rem; padding: .15rem; text-align: center;
  }

  .cell-step {
    padding: .2rem;
    text-align: center;
    cursor: pointer;
    border: 1px solid transparent;
    border-radius: 6px;
    transition: background .12s, border-color .12s;
  }
  .cell-step:hover { background: rgba(255,255,255,.04); }
  .cell-step.active { background: rgba(255,45,85,.10); }
  .cell-step.selected { border-color: #ff2d55; background: rgba(255,45,85,.18); }
  .cell-step.current { box-shadow: inset 0 0 0 2px #00d4ff; }

  .step-inner { display: flex; flex-direction: column; align-items: center; gap: 1px; }
  .step-preset { font-size: .78rem; font-weight: 600; color: #f5f5f7; }
  .step-beats { font-size: .6rem; color: #a1a1a6; }
  .step-empty { color: rgba(255,255,255,.12); font-size: .9rem; }

  /* Step editor */
  .step-editor {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1rem 1.25rem;
  }
  .editor-header {
    display: flex; align-items: center; justify-content: space-between;
    margin-bottom: .75rem;
  }
  .editor-header h3 { margin: 0; font-size: .95rem; font-weight: 600; }
  .editor-header-actions { display: flex; align-items: center; gap: .75rem; }

  .inline-toggle {
    display: flex; align-items: center; gap: .35rem;
    font-size: .8rem; color: #a1a1a6; cursor: pointer;
  }
  .inline-toggle input[type="checkbox"] { accent-color: #ff2d55; width: 14px; height: 14px; }

  .inline-field {
    display: flex; align-items: center; gap: .3rem;
    font-size: .8rem; color: #a1a1a6;
  }
  .inline-field input {
    width: 48px; background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.10); border-radius: 6px;
    color: #f5f5f7; font-size: .82rem; padding: .2rem .3rem;
    text-align: center; font-family: monospace;
  }
  .inline-field input:focus { outline: none; border-color: #ff2d55; }

  .editor-fields { display: flex; gap: 1.25rem; align-items: flex-end; flex-wrap: wrap; }
  .field { display: flex; flex-direction: column; gap: .3rem; }
  .field span { font-size: .72rem; font-weight: 600; color: #a1a1a6; text-transform: uppercase; letter-spacing: .04em; }
  .field select, .field input[type="number"] {
    background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.12); border-radius: 8px;
    color: #f5f5f7; font-size: .88rem; padding: .45rem .6rem;
    min-width: 140px; font-family: monospace;
  }
  .field select:focus, .field input:focus { outline: none; border-color: #ff2d55; }

  .field-duration-preview {
    flex-direction: row; align-items: center; gap: .5rem;
    padding: .45rem .6rem;
    background: rgba(0,212,255,.06);
    border: 1px solid rgba(0,212,255,.15);
    border-radius: 8px;
  }
  .field-duration-preview span:first-child { font-size: .72rem; color: #a1a1a6; }
  .dur-val { font-size: .95rem; font-weight: 700; color: #00d4ff; font-family: monospace; }

  .editor-inactive {
    color: #a1a1a6; font-size: .85rem; margin: 0;
    padding: .5rem 0;
  }

  /* Track detail */
  .track-detail {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1rem 1.25rem;
  }
  .track-detail h3 { margin: 0 0 .75rem; font-size: .95rem; font-weight: 600; }
  .track-fields { display: flex; gap: 1.25rem; align-items: flex-end; flex-wrap: wrap; margin-bottom: .75rem; }

  .track-steps-list { display: flex; gap: .4rem; flex-wrap: wrap; }
  .mini-step {
    display: flex; flex-direction: column; align-items: center; gap: 2px;
    background: rgba(255,255,255,.04);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 8px; padding: .35rem .5rem; min-width: 52px;
    cursor: pointer; transition: background .12s, border-color .12s;
  }
  .mini-step:hover { background: rgba(255,255,255,.08); }
  .mini-step.selected { border-color: #ff2d55; background: rgba(255,45,85,.12); }
  .mini-idx { font-size: .6rem; color: #a1a1a6; }
  .mini-preset { font-size: .78rem; font-weight: 600; color: #f5f5f7; }
  .mini-beats { font-size: .6rem; color: #a1a1a6; }

  /* Toast / error */
  .toast {
    position: fixed; bottom: 1.5rem; right: 1.5rem;
    background: rgba(48,209,88,.92); color: #fff;
    padding: .6rem 1.25rem; border-radius: 10px;
    font-size: .88rem; font-weight: 600;
    box-shadow: 0 4px 16px rgba(0,0,0,.4);
    animation: toast-in .25s ease-out;
  }
  .error-bar {
    position: fixed; bottom: 1.5rem; left: 50%; transform: translateX(-50%);
    background: rgba(255,69,58,.92); color: #fff;
    padding: .6rem 1.25rem; border-radius: 10px;
    font-size: .85rem; max-width: 90vw;
    box-shadow: 0 4px 16px rgba(0,0,0,.4);
  }
  @keyframes toast-in {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }
</style>
