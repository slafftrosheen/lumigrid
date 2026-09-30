<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import type { RuntimeSnapshotV3 } from '$lib/state/runtimeSnapshot';

  interface Props {
    snapshot: RuntimeSnapshotV3 | null;
    pixelsPerBeat: number;
    scrollLeft: number;
  }

  const { snapshot, pixelsPerBeat, scrollLeft }: Props = $props();

  const isRunning = $derived(
    snapshot?.transport?.mode === 'timeline' || Boolean(snapshot?.transport?.timeline?.running)
  );

  const anchorMs = $derived(snapshot?.transport?.timeline?.anchorMs ?? 0);
  const bpm = $derived(snapshot?.transport?.timeline?.bpm ?? 120);
  const snapshotClockMs = $derived(snapshot?.timeline?.clockMs ?? snapshot?.generatedAtMs ?? 0);

  let interpolatedBeat = $state(0);
  let animationFrameId: number | null = null;
  let lastSnapshotReceivePerf = $state(performance.now());
  let lastSeenSnapshotClock = $state(0);

  $effect(() => {
    if (snapshotClockMs !== lastSeenSnapshotClock) {
      lastSeenSnapshotClock = snapshotClockMs;
      lastSnapshotReceivePerf = performance.now();
    }
  });

  function updatePlayhead() {
    if (isRunning && anchorMs > 0 && bpm > 0) {
      const nowPerf = performance.now();
      const timeSinceSnap = nowPerf - lastSnapshotReceivePerf;
      // Cap interpolation at 1.5 seconds if snapshots degrade/stop
      const effectiveInterpMs = Math.min(1500, timeSinceSnap);
      const estClockMs = snapshotClockMs + effectiveInterpMs;

      const elapsedMs = estClockMs >= anchorMs ? estClockMs - anchorMs : 0;
      const msPerBeat = 60000 / bpm;
      interpolatedBeat = elapsedMs / msPerBeat;
    } else {
      interpolatedBeat = 0;
    }

    animationFrameId = requestAnimationFrame(updatePlayhead);
  }

  onMount(() => {
    animationFrameId = requestAnimationFrame(updatePlayhead);
  });

  onDestroy(() => {
    if (animationFrameId !== null) {
      cancelAnimationFrame(animationFrameId);
    }
  });

  // Calculate position: 220px is pinned left channel label width
  const playheadX = $derived(220 + interpolatedBeat * pixelsPerBeat - scrollLeft);
</script>

{#if isRunning && playheadX >= 220}
  <div class="timeline-playhead" style="left: {playheadX}px;">
    <div class="playhead-cap">
      <span class="playhead-beat-tag">{Math.floor(interpolatedBeat) + 1}</span>
    </div>
    <div class="playhead-line"></div>
  </div>
{/if}

<style>
  .timeline-playhead {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 2px;
    z-index: 30;
    pointer-events: none;
    transform: translateX(-1px);
  }

  .playhead-cap {
    position: absolute;
    top: 0;
    left: 50%;
    transform: translateX(-50%);
    background: #10b981;
    color: black;
    font-size: 9px;
    font-weight: 800;
    padding: 1px 4px;
    border-radius: 3px;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.4);
    white-space: nowrap;
  }

  .playhead-line {
    position: absolute;
    top: 14px;
    bottom: 0;
    left: 50%;
    transform: translateX(-50%);
    width: 2px;
    background: #10b981;
    box-shadow: 0 0 6px rgba(16, 185, 129, 0.8);
  }
</style>
