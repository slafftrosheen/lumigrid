<script lang="ts">
  /**
   * CueInspector — list-level settings panel (LG-034 §54/§55).
   * End action is only meaningful for natural Single Shot completion.
   */
  import type { CueEndAction, CueRunMode } from '$lib/cues/document';
  import { formatDuration, cueListNominalDurationMs } from '$lib/cues/document';

  interface Props {
    mode: CueRunMode;
    endAction: CueEndAction;
    cueCount: number;
    nominalMs: number;
    onmodechange: (mode: CueRunMode) => void;
    onendactionchange: (action: CueEndAction) => void;
  }

  let { mode, endAction, cueCount, nominalMs, onmodechange, onendactionchange }: Props = $props();

  const RUN_MODES: Array<{ id: CueRunMode; label: string; hint: string }> = [
    { id: 'single_shot', label: 'Single Shot', hint: 'Plays once and stops at the end action.' },
    { id: 'loop', label: 'Loop', hint: 'Repeats from the top forever.' },
    { id: 'ping_pong', label: 'Ping Pong', hint: '1 → 2 → 3 → 2 → 1 …' },
    { id: 'random', label: 'Random', hint: 'Leader chooses the next cue at runtime.' },
    { id: 'manual', label: 'Manual', hint: 'Operator fades with the T-Bar.' },
  ];

  const END_ACTIONS: Array<{ id: CueEndAction; label: string }> = [
    { id: 'hold_last', label: 'Hold last Scene' },
    { id: 'blackout', label: 'Blackout' },
    { id: 'restore_fallback', label: 'Restore fallback Scene' },
  ];

  let endActionRelevant = $derived(mode === 'single_shot');
  let modeHint = $derived(RUN_MODES.find((m) => m.id === mode)?.hint ?? '');
</script>

<div class="inspector">
  <label class="field">
    <span>Run mode</span>
    <select value={mode} onchange={(e) => onmodechange((e.target as HTMLSelectElement).value as CueRunMode)}>
      {#each RUN_MODES as m (m.id)}
        <option value={m.id}>{m.label}</option>
      {/each}
    </select>
  </label>
  <p class="hint">{modeHint}</p>

  {#if endActionRelevant}
    <label class="field">
      <span>End action</span>
      <select
        value={endAction}
        onchange={(e) => onendactionchange((e.target as HTMLSelectElement).value as CueEndAction)}
      >
        {#each END_ACTIONS as a (a.id)}
          <option value={a.id}>{a.label}</option>
        {/each}
      </select>
    </label>
    <p class="hint">Applied when the last cue finishes naturally.</p>
  {:else}
    <div class="field field-disabled">
      <span>End action</span>
      <div class="disabled-value">Unused in {RUN_MODES.find((m) => m.id === mode)?.label} mode</div>
    </div>
    <p class="hint">End action only applies to natural Single Shot completion.</p>
  {/if}

  <div class="stats">
    <div class="stat">
      <span class="stat-value">{cueCount}</span>
      <span class="stat-label">cues</span>
    </div>
    <div class="stat">
      <span class="stat-value">{formatDuration(nominalMs)}</span>
      <span class="stat-label">nominal duration</span>
    </div>
  </div>
</div>

<style>
  .inspector {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .field > span {
    font-size: 0.66rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: rgba(255, 255, 255, 0.5);
  }
  .field select {
    background: rgba(255, 255, 255, 0.055);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 6px;
    color: #fff;
    padding: 7px 10px;
    font-size: 0.88rem;
  }
  .field-disabled .disabled-value {
    background: rgba(255, 255, 255, 0.02);
    border: 1px dashed rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: rgba(255, 255, 255, 0.35);
    padding: 7px 10px;
    font-size: 0.84rem;
  }
  .hint {
    margin: 0 0 4px 0;
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.4);
  }
  .stats {
    display: flex;
    gap: 14px;
    margin-top: 6px;
    padding-top: 10px;
    border-top: 1px solid rgba(255, 255, 255, 0.08);
  }
  .stat {
    display: flex;
    flex-direction: column;
  }
  .stat-value {
    font-family: monospace;
    font-size: 1rem;
    font-weight: 700;
    color: var(--dash-accent);
  }
  .stat-label {
    font-size: 0.66rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: rgba(255, 255, 255, 0.45);
  }
</style>
