<script lang="ts">
  /**
   * CueListCard — sidebar library card (LG-034 §44).
   * Shows name, slot, mode, cue count, nominal duration, running and stale state.
   */
  import { formatDuration, cueListNominalDurationMs } from '$lib/cues/document';

  interface Props {
    index: number;
    name: string;
    mode: string;
    count: number;
    checksum?: number | null;
    /** Proportional widths for the ribbon (per-cue hold+fade ms). */
    cueMs?: number[];
    selected?: boolean;
    running?: boolean;
    contentStale?: boolean;
    onselect?: () => void;
  }

  let {
    index,
    name,
    mode,
    count,
    checksum = null,
    cueMs = [],
    selected = false,
    running = false,
    contentStale = false,
    onselect,
  }: Props = $props();

  const MODE_LABELS: Record<string, string> = {
    single_shot: 'Single Shot',
    loop: 'Loop',
    ping_pong: 'Ping Pong',
    random: 'Random',
    manual: 'Manual',
  };

  let nominal = $derived(cueListNominalDurationMs({ schema: 2, index, checksum: null, name, mode: mode as never, endAction: 'hold_last', cues: cueMs.map(() => ({ sceneIndex: 0, holdMs: 1, fadeMs: 0, curve: 'linear' as never, label: '' })) }));
  let total = $derived(cueMs.reduce((a, b) => a + b, 0));
  let slot = $derived(String(index + 1).padStart(2, '0'));
</script>

<button
  class="cue-list-card"
  class:selected
  class:running
  onclick={onselect}
  aria-pressed={selected}
>
  <div class="card-top">
    <span class="card-name">{name || `Cue List ${index + 1}`}</span>
    <span class="card-slot">{slot}</span>
  </div>

  <div class="card-mid">
    <span class="card-mode">{MODE_LABELS[mode] ?? mode}</span>
    <span class="card-meta">
      {count} cue{count === 1 ? '' : 's'}
      {#if total > 0}
        · {formatDuration(total)}
      {/if}
    </span>
  </div>

  {#if cueMs.length > 0}
    <div class="card-ribbon" aria-hidden="true">
      {#each cueMs as ms (ms)}
        <span class="ribbon-seg" style="flex-grow: {Math.max(1, ms)}"></span>
      {/each}
    </div>
  {/if}

  <div class="card-badges">
    {#if running}
      <span class="badge badge-running">RUNNING</span>
    {/if}
    {#if contentStale}
      <span class="badge badge-stale">STALE</span>
    {/if}
  </div>
</button>

<style>
  .cue-list-card {
    display: flex;
    flex-direction: column;
    gap: 6px;
    width: 100%;
    text-align: left;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 10px;
    padding: 12px 14px;
    color: #fff;
    cursor: pointer;
    transition: border-color 0.15s ease, background 0.15s ease;
  }
  .cue-list-card:hover {
    background: rgba(255, 255, 255, 0.06);
  }
  .cue-list-card.selected {
    background: linear-gradient(90deg, rgba(var(--dash-accent-rgb), 0.16), rgba(var(--dash-accent-rgb), 0.06));
    border-color: rgba(var(--dash-accent-rgb), 0.4);
    box-shadow: inset 3px 0 0 var(--dash-accent);
  }
  .card-top {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 8px;
  }
  .card-name {
    font-weight: 600;
    font-size: 0.92rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .card-slot {
    font-family: monospace;
    font-size: 0.8rem;
    color: rgba(255, 255, 255, 0.55);
  }
  .card-mid {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    font-size: 0.75rem;
  }
  .card-mode {
    color: var(--dash-accent);
    font-family: monospace;
  }
  .card-meta {
    color: rgba(255, 255, 255, 0.45);
    font-family: monospace;
  }
  .card-ribbon {
    display: flex;
    gap: 2px;
    height: 6px;
    border-radius: 3px;
    overflow: hidden;
  }
  .ribbon-seg {
    height: 100%;
    min-width: 4px;
    background: rgba(var(--dash-accent-rgb), 0.45);
    border-radius: 1px;
  }
  .card-badges {
    display: flex;
    gap: 6px;
  }
  .badge {
    font-size: 0.62rem;
    font-weight: 800;
    letter-spacing: 0.06em;
    padding: 2px 7px;
    border-radius: 4px;
  }
  .badge-running {
    color: var(--dash-ok);
    background: rgba(var(--dash-ok-rgb, 48, 209, 88), 0.12);
    border: 1px solid rgba(var(--dash-ok-rgb, 48, 209, 88), 0.4);
  }
  .badge-stale {
    color: #ffd60a;
    background: rgba(255, 214, 10, 0.1);
    border: 1px solid rgba(255, 214, 10, 0.35);
  }
</style>
