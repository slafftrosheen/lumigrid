<script lang="ts">
  /**
   * CueCard — storyboard cue editor card (LG-034 §47/§49/§54).
   * Human-readable seconds editing; run-mode-aware control visibility.
   */
  import { msToSeconds, secondsToMs, MAX_TIMING_LIMITS } from './cueCardTiming';
  import { MAX_CUE_LABEL_BYTES, utf8ByteLength } from '$lib/cues/document';
  import { TRANSITION_CURVES, type TransitionCurve } from '$lib/runtime/curves';

  interface Props {
    index: number;
    sceneIndex: number;
    sceneName: string;
    sceneThumbnail?: string | null;
    label: string;
    holdMs: number;
    fadeMs: number;
    curve: TransitionCurve;
    selected?: boolean;
    isCurrent?: boolean;
    /** Run-mode-aware visibility (§54). */
    showTiming?: boolean;
    /** Scene picker: scene index -> {name, thumbnail} lookup. */
    scenes: Array<{ index: number; name: string; thumbnail?: string | null }>;
    onselect?: () => void;
    onupdate?: (patch: Partial<{ sceneIndex: number; label: string; holdMs: number; fadeMs: number; curve: TransitionCurve }>) => void;
    onmove?: (direction: 'up' | 'down') => void;
    onduplicate?: () => void;
    oncopy?: () => void;
    onpasteafter?: () => void;
    ondelete?: () => void;
    onjumpto?: () => void;
    canMoveUp?: boolean;
    canMoveDown?: boolean;
    disableScenePicker?: boolean;
  }

  let {
    index,
    sceneIndex,
    sceneName,
    sceneThumbnail = null,
    label = '',
    holdMs = 3000,
    fadeMs = 1000,
    curve = 'easeInOut',
    selected = false,
    isCurrent = false,
    showTiming = true,
    scenes = [],
    onselect,
    onupdate,
    onmove,
    onduplicate,
    oncopy,
    onpasteafter,
    ondelete,
    onjumpto,
    canMoveUp = true,
    canMoveDown = true,
    disableScenePicker = false,
  }: Props = $props();

  let holdSeconds = $derived(msToSeconds(holdMs));
  let fadeSeconds = $derived(msToSeconds(fadeMs));
  let labelBytes = $derived(utf8ByteLength(label));
  let labelOver = $derived(labelBytes > MAX_CUE_LABEL_BYTES);
  let slot = $derived(String(index + 1).padStart(2, '0'));

  function updateHold(event: Event) {
    const value = parseFloat((event.target as HTMLInputElement).value);
    onupdate?.({ holdMs: secondsToMs(value, MAX_TIMING_LIMITS.holdMs) });
  }

  function updateFade(event: Event) {
    const value = parseFloat((event.target as HTMLInputElement).value);
    onupdate?.({ fadeMs: secondsToMs(value, MAX_TIMING_LIMITS.fadeMs) });
  }

  function updateLabel(event: Event) {
    onupdate?.({ label: (event.target as HTMLInputElement).value });
  }

  function updateCurve(event: Event) {
    onupdate?.({ curve: (event.target as HTMLSelectElement).value as TransitionCurve });
  }

  function updateScene(event: Event) {
    onupdate?.({ sceneIndex: parseInt((event.target as HTMLSelectElement).value, 10) });
  }
</script>

<div
  class="cue-card"
  class:selected
  class:current={isCurrent}
  role="button"
  tabindex="0"
  aria-pressed={selected}
  aria-label={`Cue ${slot}: ${label || sceneName}`}
  onclick={onselect}
  onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); onselect?.(); } }}
>
  <div class="card-head">
    <span class="cue-slot">{slot}</span>
    <input
      class="cue-label-input"
      type="text"
      value={label}
      maxlength={64}
      placeholder="Cue label"
      aria-label={`Cue ${slot} label`}
      onclick={(e) => e.stopPropagation()}
      oninput={updateLabel}
    />
    {#if labelOver}
      <span class="byte-warn" title="Label exceeds 23 UTF-8 bytes">{labelBytes}B</span>
    {/if}
    <span class="cue-scene-slot">S{String(sceneIndex + 1).padStart(2, '0')}</span>
  </div>

  {#if isCurrent}
    <div class="live-pill">● LIVE</div>
  {/if}

  <div class="card-body">
    <div class="scene-block">
      {#if sceneThumbnail}
        <img class="scene-thumb" src={sceneThumbnail} alt={sceneName} />
      {:else}
        <div class="scene-thumb scene-thumb-placeholder" aria-hidden="true"></div>
      {/if}
      <div class="scene-meta">
        <select
          class="scene-select"
          value={sceneIndex}
          disabled={disableScenePicker}
          aria-label={`Cue ${slot} scene`}
          onclick={(e) => e.stopPropagation()}
          onchange={updateScene}
        >
          {#each scenes as s (s.index)}
            <option value={s.index}>{s.name} (#{s.index + 1})</option>
          {/each}
          {#if !scenes.some((s) => s.index === sceneIndex)}
            <option value={sceneIndex}>Scene #{sceneIndex + 1} (missing)</option>
          {/if}
        </select>
      </div>
    </div>

    {#if showTiming}
      <div class="timing-block">
        <label class="timing-field">
          <span>Hold</span>
          <input
            type="number"
            min="0"
            step="0.1"
            value={holdSeconds}
            aria-label={`Cue ${slot} hold seconds`}
            onclick={(e) => e.stopPropagation()}
            onchange={updateHold}
          />
          <em>s</em>
        </label>
        <label class="timing-field">
          <span>Fade</span>
          <input
            type="number"
            min="0"
            step="0.1"
            value={fadeSeconds}
            aria-label={`Cue ${slot} fade seconds`}
            onclick={(e) => e.stopPropagation()}
            onchange={updateFade}
          />
          <em>s</em>
        </label>
        <label class="timing-field">
          <span>Curve</span>
          <select
            value={curve}
            aria-label={`Cue ${slot} curve`}
            onclick={(e) => e.stopPropagation()}
            onchange={updateCurve}
          >
            {#each TRANSITION_CURVES as c (c)}
              <option value={c}>{c}</option>
            {/each}
          </select>
        </label>
      </div>
    {:else}
      <div class="timing-note">Manual mode — crossfade via T-Bar at runtime</div>
    {/if}
  </div>

  <div
    class="card-actions"
    role="toolbar"
    tabindex="-1"
    aria-label={`Cue ${slot} actions`}
    onclick={(e) => e.stopPropagation()}
    onkeydown={(e) => e.stopPropagation()}
  >
    <button class="abtn" title="Move up" aria-label="Move cue up" disabled={!canMoveUp} onclick={() => onmove?.('up')}>▲</button>
    <button class="abtn" title="Move down" aria-label="Move cue down" disabled={!canMoveDown} onclick={() => onmove?.('down')}>▼</button>
    <button class="abtn" title="Duplicate cue" aria-label="Duplicate cue" onclick={() => onduplicate?.()}>⧉</button>
    <button class="abtn" title="Copy cue" aria-label="Copy cue" onclick={() => oncopy?.()}>❐</button>
    <button class="abtn" title="Paste after" aria-label="Paste after" onclick={() => onpasteafter?.()}>📋</button>
    {#if onjumpto}
      <button class="abtn abtn-jump" title="Jump to cue during playback" aria-label="Jump to cue" onclick={() => onjumpto?.()}>⇥</button>
    {/if}
    <button class="abtn abtn-danger" title="Delete cue" aria-label="Delete cue" onclick={() => ondelete?.()}>✕</button>
  </div>
</div>

<style>
  .cue-card {
    position: relative;
    display: flex;
    flex-direction: column;
    gap: 10px;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 12px;
    padding: 12px 14px;
    cursor: pointer;
    transition: border-color 0.15s ease, background 0.15s ease;
  }
  .cue-card:hover {
    background: rgba(255, 255, 255, 0.05);
  }
  .cue-card.selected {
    border-color: rgba(var(--dash-accent-rgb), 0.5);
    background: linear-gradient(180deg, rgba(var(--dash-accent-rgb), 0.08), rgba(255, 255, 255, 0.03));
  }
  .cue-card.current {
    border-color: var(--dash-ok);
  }
  .card-head {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .cue-slot {
    font-family: monospace;
    font-size: 1rem;
    font-weight: 700;
    color: var(--dash-accent);
    min-width: 26px;
  }
  .cue-label-input {
    flex: 1;
    min-width: 0;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    color: #fff;
    font-size: 0.92rem;
    font-weight: 600;
    padding: 4px 8px;
  }
  .cue-label-input:hover,
  .cue-label-input:focus-visible {
    border-color: rgba(255, 255, 255, 0.15);
    background: rgba(255, 255, 255, 0.04);
  }
  .byte-warn {
    font-family: monospace;
    font-size: 0.66rem;
    color: #ffd60a;
    border: 1px solid rgba(255, 214, 10, 0.4);
    border-radius: 4px;
    padding: 1px 5px;
  }
  .cue-scene-slot {
    font-family: monospace;
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.5);
  }
  .live-pill {
    position: absolute;
    top: -9px;
    right: 12px;
    font-size: 0.6rem;
    font-weight: 800;
    letter-spacing: 0.06em;
    color: var(--dash-ok);
    background: rgba(var(--dash-ok-rgb, 48, 209, 88), 0.15);
    border: 1px solid rgba(var(--dash-ok-rgb, 48, 209, 88), 0.5);
    border-radius: 4px;
    padding: 1px 7px;
  }
  .card-body {
    display: flex;
    gap: 14px;
    align-items: flex-start;
    flex-wrap: wrap;
  }
  .scene-block {
    display: flex;
    gap: 10px;
    align-items: center;
    min-width: 220px;
    flex: 1 1 220px;
  }
  .scene-thumb {
    width: 56px;
    height: 34px;
    border-radius: 6px;
    object-fit: cover;
    border: 1px solid rgba(255, 255, 255, 0.12);
    background: linear-gradient(135deg, rgba(var(--dash-accent-rgb), 0.25), rgba(255, 255, 255, 0.05));
  }
  .scene-thumb-placeholder {
    display: inline-block;
  }
  .scene-select {
    flex: 1;
    min-width: 0;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: 6px;
    color: #fff;
    padding: 6px 8px;
    font-size: 0.84rem;
  }
  .timing-block {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
  }
  .timing-field {
    display: flex;
    align-items: center;
    gap: 5px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 6px;
    padding: 4px 8px;
  }
  .timing-field span {
    font-size: 0.66rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: rgba(255, 255, 255, 0.5);
  }
  .timing-field input {
    width: 58px;
    background: transparent;
    border: none;
    color: #fff;
    font-family: monospace;
    font-size: 0.86rem;
    text-align: right;
  }
  .timing-field select {
    background: transparent;
    border: none;
    color: #fff;
    font-size: 0.8rem;
  }
  .timing-field em {
    font-style: normal;
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.4);
  }
  .timing-note {
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.4);
    font-style: italic;
    align-self: center;
  }
  .card-actions {
    display: flex;
    gap: 5px;
    justify-content: flex-end;
  }
  .abtn {
    width: 28px;
    height: 26px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 5px;
    color: #fff;
    cursor: pointer;
    font-size: 0.78rem;
    line-height: 1;
  }
  .abtn:hover:not(:disabled) {
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }
  .abtn:disabled {
    opacity: 0.3;
    cursor: not-allowed;
  }
  .abtn-danger:hover:not(:disabled) {
    border-color: var(--dash-danger);
    color: var(--dash-danger);
  }
  .abtn-jump:hover:not(:disabled) {
    border-color: var(--dash-ok);
    color: var(--dash-ok);
  }
</style>
