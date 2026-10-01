<script lang="ts">
  /**
   * CueTimelineRibbon — compact show-duration ribbon (LG-034 §48).
   * Millisecond-proportional [fade][hold] segments; NOT beat-based.
   */
  interface Props {
    /** Per-cue { fadeMs, holdMs }. */
    cues?: Array<{ fadeMs: number; holdMs: number }>;
    /** Highlight the currently running cue index (optional). */
    currentCue?: number | null;
  }

  let { cues = [], currentCue = null }: Props = $props();

  let total = $derived(cues.reduce((sum, cue) => sum + cue.fadeMs + cue.holdMs, 0));
</script>

<div class="ribbon" role="img" aria-label="Show duration ribbon, {total / 1000} seconds total">
  {#if total === 0}
    <span class="ribbon-empty">No timing yet</span>
  {:else}
    {#each cues as cue, i (i)}
      <span
        class="seg seg-fade"
        class:seg-live={currentCue === i}
        style="flex-grow: {Math.max(cue.fadeMs, 1)}"
        title="Cue {i + 1} fade · {cue.fadeMs} ms"
      ></span>
      <span
        class="seg seg-hold"
        class:seg-live={currentCue === i}
        style="flex-grow: {Math.max(cue.holdMs, 1)}"
        title="Cue {i + 1} hold · {cue.holdMs} ms"
      ></span>
    {/each}
  {/if}
</div>

<style>
  .ribbon {
    display: flex;
    width: 100%;
    height: 14px;
    border-radius: 7px;
    overflow: hidden;
    border: 1px solid rgba(255, 255, 255, 0.08);
    background: rgba(0, 0, 0, 0.3);
  }
  .seg {
    height: 100%;
    min-width: 2px;
  }
  .seg-fade {
    background: rgba(191, 90, 242, 0.55);
  }
  .seg-hold {
    background: rgba(var(--dash-accent-rgb), 0.5);
  }
  .seg-live {
    outline: 1px solid rgba(255, 255, 255, 0.7);
    outline-offset: -1px;
  }
  .ribbon-empty {
    align-self: center;
    justify-content: center;
    width: 100%;
    text-align: center;
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.35);
  }
</style>
