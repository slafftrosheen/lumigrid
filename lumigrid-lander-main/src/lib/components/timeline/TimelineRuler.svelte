<script lang="ts">
  interface Props {
    maxBeats: number;
    pixelsPerBeat: number;
    scrollLeft?: number;
  }

  const { maxBeats, pixelsPerBeat, scrollLeft = 0 }: Props = $props();

  const totalWidth = $derived(Math.max(800, maxBeats * pixelsPerBeat));
  const beats = $derived(Array.from({ length: Math.max(32, maxBeats + 16) }, (_, i) => i));
</script>

<div class="timeline-ruler-wrapper">
  <!-- Pinned channel label spacer -->
  <div class="ruler-pinned-spacer">
    <span class="ruler-channel-title">CHANNEL</span>
  </div>

  <!-- Scrollable beat markings -->
  <div class="ruler-scroll-track" style="width: {totalWidth}px;">
    {#each beats as b}
      {@const isBar = b % 4 === 0}
      <div
        class="beat-mark"
        class:is-bar={isBar}
        style="left: {b * pixelsPerBeat}px; width: {pixelsPerBeat}px;"
      >
        <span class="beat-label">{b + 1}</span>
        {#if isBar}
          <span class="bar-tag">B{Math.floor(b / 4) + 1}</span>
        {/if}
      </div>
    {/each}
  </div>
</div>

<style>
  .timeline-ruler-wrapper {
    display: flex;
    align-items: stretch;
    height: 32px;
    background: #0f172a;
    border-top: 1px solid #1e293b;
    border-bottom: 1px solid #334155;
    position: sticky;
    top: 0;
    z-index: 20;
    user-select: none;
  }

  .ruler-pinned-spacer {
    width: 220px;
    min-width: 220px;
    max-width: 220px;
    background: #0b0f19;
    border-right: 2px solid #334155;
    display: flex;
    align-items: center;
    padding-left: 12px;
    position: sticky;
    left: 0;
    z-index: 25;
  }

  .ruler-channel-title {
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 0.08em;
    color: #64748b;
  }

  .ruler-scroll-track {
    position: relative;
    height: 100%;
    background: repeating-linear-gradient(
      90deg,
      transparent,
      transparent 31px,
      rgba(51, 65, 85, 0.4) 32px
    );
  }

  .beat-mark {
    position: absolute;
    top: 0;
    bottom: 0;
    border-left: 1px solid #1e293b;
    display: flex;
    flex-direction: column;
    justify-content: flex-end;
    padding-left: 3px;
    padding-bottom: 2px;
  }

  .beat-mark.is-bar {
    border-left: 1px solid #64748b;
  }

  .beat-label {
    font-size: 9px;
    font-weight: 500;
    color: #475569;
    line-height: 1;
  }

  .is-bar .beat-label {
    color: #94a3b8;
    font-weight: 700;
  }

  .bar-tag {
    position: absolute;
    top: 2px;
    left: 3px;
    font-size: 8px;
    font-weight: 700;
    color: #38bdf8;
    line-height: 1;
    letter-spacing: 0.02em;
  }
</style>
