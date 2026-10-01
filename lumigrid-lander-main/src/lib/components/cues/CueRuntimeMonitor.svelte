<script lang="ts">
  /**
   * CueRuntimeMonitor — live show strip (LG-034 §56–§58).
   *
   * Answers: which list, which cue, which phase, what's next, time remaining,
   * stale content, follower control. Fading and Holding get distinct visuals;
   * display timing interpolates between authoritative snapshots.
   */
  import { formatCountdown } from '$lib/cues/timing';

  interface Props {
    listName: string | null;
    running: boolean;
    state: string;
    pausedFrom: string;
    currentCueIndex: number;
    cueCount: number;
    currentLabel: string;
    nextLabel: string | null;
    fading: boolean;
    /** Interpolated display timing. */
    elapsedMs: number;
    remainingMs: number;
    /** Total phase duration for the bar (elapsed+remaining at snapshot). */
    phaseTotalMs: number;
    contentStale: boolean;
    remoteDriven: boolean;
    isFollower: boolean;
    manualMode: boolean;
    onstart?: () => void;
    onpause?: () => void;
    onresume?: () => void;
    onstop?: () => void;
    onnext?: () => void;
    onprevious?: () => void;
    onrestart?: () => void;
  }

  let {
    listName,
    running,
    state,
    pausedFrom,
    currentCueIndex,
    cueCount,
    currentLabel,
    nextLabel,
    fading,
    elapsedMs,
    remainingMs,
    phaseTotalMs,
    contentStale,
    remoteDriven,
    isFollower,
    manualMode,
    onstart,
    onpause,
    onresume,
    onstop,
    onnext,
    onprevious,
    onrestart,
  }: Props = $props();

  let progressPct = $derived(
    phaseTotalMs > 0 ? Math.min(100, Math.max(0, (elapsedMs / phaseTotalMs) * 100)) : 0
  );

  let phaseLabel = $derived.by(() => {
    if (!running) return 'STOPPED';
    if (state === 'paused') {
      const from = pausedFrom === 'fading' ? 'FADING' : pausedFrom === 'manual' ? 'MANUAL' : 'HOLDING';
      return `PAUSED · FROM ${from}`;
    }
    if (state === 'manual') return 'MANUAL';
    if (state === 'fading' || fading) return 'FADING';
    if (state === 'holding') return 'HOLDING';
    return state.toUpperCase();
  });

  let slot = $derived(String(currentCueIndex + 1).padStart(2, '0'));
  let totalSlot = $derived(String(Math.max(cueCount, 0)).padStart(2, '0'));
</script>

<div class="monitor" class:monitor-running={running}>
  <div class="monitor-top">
    <div class="monitor-title">
      <span class="status-dot" class:dot-running={running} class:dot-paused={state === 'paused'}></span>
      {#if running}
        <span class="monitor-heading">RUNNING · {listName ?? 'Cue List'}</span>
      {:else}
        <span class="monitor-heading">STOPPED</span>
      {/if}
      {#if contentStale}
        <span class="pill pill-stale">RUNNING PREVIOUS VERSION</span>
      {/if}
      {#if remoteDriven || isFollower}
        <span class="pill pill-remote">FOLLOWER</span>
      {/if}
    </div>

    <div class="monitor-indices">
      <span class="idx-current">{slot} / {totalSlot}</span>
      {#if !manualMode}
        <span class="idx-arrow">→</span>
        <span class="idx-next">{nextLabel ?? '—'}</span>
      {/if}
    </div>
  </div>

  <div class="monitor-cue">
    <span class="cue-label">{currentLabel}</span>
    <span class="cue-phase" class:phase-fading={fading && state !== 'paused'} class:phase-holding={!fading && state === 'holding'}>
      {phaseLabel}
    </span>
    {#if running && !manualMode && (state === 'holding' || state === 'fading' || state === 'paused')}
      <span class="cue-time">{formatCountdown(elapsedMs)} / {formatCountdown(elapsedMs + remainingMs)}</span>
    {/if}
  </div>

  {#if running && !manualMode}
    <div class="monitor-bar">
      <div
        class="monitor-fill"
        class:fill-fading={fading && state !== 'paused'}
        style="width: {progressPct}%"
      ></div>
    </div>
  {/if}

  <div class="monitor-actions">
    {#if !running}
      <button class="mbtn" disabled={isFollower} onclick={onstart} title={isFollower ? 'Playback controlled by Sync Leader' : 'Start show'}>▶ RUN</button>
    {:else}
      {#if state === 'paused'}
        <button class="mbtn" disabled={isFollower} onclick={onresume}>⏯ RESUME</button>
      {:else}
        <button class="mbtn" disabled={isFollower || manualMode} onclick={onpause}>⏸ PAUSE</button>
      {/if}
      <button class="mbtn" disabled={isFollower} onclick={onnext}>⏭ NEXT</button>
    {/if}
    <button class="mbtn" disabled={isFollower} onclick={onprevious}>⏮ PREV</button>
    <div class="mbtn-divider"></div>
    {#if contentStale}
      <button class="mbtn mbtn-restart" disabled={isFollower} onclick={onrestart} title="Load the newest saved version">
        ↻ RESTART WITH SAVED VERSION
      </button>
    {/if}
    <button class="mbtn mbtn-stop" disabled={isFollower} onclick={onstop}>⏹ STOP</button>
  </div>
</div>

<style>
  .monitor {
    display: flex;
    flex-direction: column;
    gap: 12px;
    background: linear-gradient(180deg, rgba(29, 29, 31, 0.92), rgba(15, 15, 17, 0.95));
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.1rem 1.25rem;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
  }
  .monitor-top {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
  }
  .monitor-title {
    display: flex;
    align-items: center;
    gap: 10px;
    min-width: 0;
  }
  .monitor-heading {
    font-family: monospace;
    font-size: 0.95rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    color: #fff;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .status-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #636366;
    flex: 0 0 auto;
  }
  .dot-running {
    background: var(--dash-ok);
    box-shadow: 0 0 10px var(--dash-ok);
  }
  .dot-paused {
    background: #ffd60a;
    box-shadow: 0 0 10px #ffd60a;
  }
  .pill {
    font-size: 0.62rem;
    font-weight: 800;
    letter-spacing: 0.06em;
    padding: 2px 8px;
    border-radius: 4px;
    white-space: nowrap;
  }
  .pill-stale {
    color: #ffd60a;
    background: rgba(255, 214, 10, 0.1);
    border: 1px solid rgba(255, 214, 10, 0.35);
  }
  .pill-remote {
    color: #64d2ff;
    background: rgba(100, 210, 255, 0.1);
    border: 1px solid rgba(100, 210, 255, 0.35);
  }
  .monitor-indices {
    display: flex;
    align-items: baseline;
    gap: 10px;
    font-family: monospace;
  }
  .idx-current {
    color: var(--dash-accent);
    font-weight: 700;
    font-size: 1.05rem;
  }
  .idx-arrow {
    color: rgba(255, 255, 255, 0.3);
  }
  .idx-next {
    color: rgba(255, 255, 255, 0.6);
    font-size: 0.9rem;
  }
  .monitor-cue {
    display: flex;
    align-items: baseline;
    gap: 14px;
    flex-wrap: wrap;
  }
  .cue-label {
    font-size: 1.25rem;
    font-weight: 700;
    color: #fff;
  }
  .cue-phase {
    font-family: monospace;
    font-size: 0.78rem;
    font-weight: 700;
    letter-spacing: 0.05em;
    padding: 3px 10px;
    border-radius: 5px;
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.12);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
  }
  .cue-phase.phase-fading {
    color: #bf5af2;
    background: rgba(191, 90, 242, 0.12);
    border-color: rgba(191, 90, 242, 0.35);
  }
  .cue-time {
    font-family: monospace;
    font-size: 0.85rem;
    color: rgba(255, 255, 255, 0.65);
    margin-left: auto;
  }
  .monitor-bar {
    height: 10px;
    background: rgba(0, 0, 0, 0.45);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 5px;
    overflow: hidden;
  }
  .monitor-fill {
    height: 100%;
    background: linear-gradient(90deg, var(--dash-accent), var(--dash-accent-hover));
    transition: width 0.2s linear;
  }
  .monitor-fill.fill-fading {
    background: linear-gradient(90deg, #bf5af2, #da8fff);
  }
  .monitor-actions {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }
  .mbtn {
    min-height: 34px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.14);
    color: #fff;
    padding: 6px 13px;
    border-radius: 6px;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .mbtn:hover:not(:disabled) {
    background: rgba(var(--dash-accent-rgb), 0.15);
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }
  .mbtn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .mbtn-restart {
    border-color: rgba(255, 214, 10, 0.4);
    color: #ffd60a;
  }
  .mbtn-stop:hover:not(:disabled) {
    border-color: var(--dash-danger);
    color: var(--dash-danger);
    background: rgba(255, 69, 58, 0.1);
  }
  .mbtn-divider {
    width: 1px;
    height: 18px;
    background: rgba(255, 255, 255, 0.14);
    margin: 0 4px;
  }
</style>
