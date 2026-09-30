<script lang="ts">
  import type { TimelineTrack } from '$lib/api/timeline';
  import type { ScenePreset } from '$lib/api/scenes';
  import { getChannelPort, isPwmChannel } from '$lib/api/timeline';
  import { getTrackTotalBeats, getActiveStepAtBeat } from '$lib/timeline/evaluation';
  import TimelineBlock from './TimelineBlock.svelte';

  interface Props {
    track: TimelineTrack;
    channelId: number;
    pixelsPerBeat: number;
    scenes: Map<number, ScenePreset>;
    selectedChannel: number | null;
    selectedStep: number | null;
    globalBeat: number;
    isTimelineRunning: boolean;
    onSelectBlock: (channelId: number, stepIndex: number) => void;
    onResizeBlock: (channelId: number, stepIndex: number, newBeats: number) => void;
    onReorderBlock: (channelId: number, fromIndex: number, toIndex: number) => void;
    onToggleEnabled: (channelId: number) => void;
    onOpenAddScene: (channelId: number) => void;
    onCopyTrack: (channelId: number) => void;
    onPasteTrack: (channelId: number) => void;
    onClearTrack: (channelId: number) => void;
    canPasteTrack: boolean;
  }

  const {
    track,
    channelId,
    pixelsPerBeat,
    scenes,
    selectedChannel,
    selectedStep,
    globalBeat,
    isTimelineRunning,
    onSelectBlock,
    onResizeBlock,
    onReorderBlock,
    onToggleEnabled,
    onOpenAddScene,
    onCopyTrack,
    onPasteTrack,
    onClearTrack,
    canPasteTrack,
  }: Props = $props();

  const port = $derived(getChannelPort(channelId));
  const isPwm = $derived(isPwmChannel(channelId));
  const totalBeats = $derived(getTrackTotalBeats(track));

  // Determine active step during playback
  const activeStepInfo = $derived.by(() => {
    if (!isTimelineRunning || !track.enabled || track.numSteps === 0) {
      return null;
    }
    return getActiveStepAtBeat(track, globalBeat);
  });

  // Drag-and-drop state
  let draggedStepIndex = $state<number | null>(null);

  function handleDragStart(e: DragEvent, index: number) {
    draggedStepIndex = index;
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move';
      e.dataTransfer.setData('text/plain', String(index));
    }
  }

  function handleDragOver(e: DragEvent) {
    e.preventDefault();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
  }

  function handleDrop(e: DragEvent, targetIndex: number) {
    e.preventDefault();
    if (draggedStepIndex !== null && draggedStepIndex !== targetIndex) {
      onReorderBlock(channelId, draggedStepIndex, targetIndex);
    }
    draggedStepIndex = null;
  }
</script>

<div class="timeline-track-row" class:muted={!track.enabled}>
  <!-- Pinned Channel Header -->
  <div class="track-pinned-header">
    <div class="channel-info">
      <span class="channel-badge" class:pwm-badge={isPwm}>{port}</span>
      <button
        type="button"
        class="mute-toggle-btn"
        class:is-active={track.enabled}
        onclick={() => onToggleEnabled(channelId)}
        title={track.enabled ? 'Click to Mute track' : 'Click to Activate track'}
      >
        {track.enabled ? 'ACTIVE' : 'MUTED'}
      </button>
    </div>

    <div class="track-metrics">
      <span class="total-beats-label">
        {#if track.numSteps > 0}
          {totalBeats}b loop
        {:else}
          empty
        {/if}
      </span>
    </div>

    <!-- Quick Action Buttons -->
    <div class="track-action-bar">
      <button
        type="button"
        class="track-icon-btn add-btn"
        onclick={() => onOpenAddScene(channelId)}
        title="Add Scene Block"
        disabled={track.steps.length >= 16}
      >
        + Add
      </button>
      <button
        type="button"
        class="track-icon-btn"
        onclick={() => onCopyTrack(channelId)}
        title="Copy Track Pattern"
        disabled={track.steps.length === 0}
      >
        Copy
      </button>
      {#if canPasteTrack}
        <button
          type="button"
          class="track-icon-btn"
          onclick={() => onPasteTrack(channelId)}
          title="Paste Pattern"
        >
          Paste
        </button>
      {/if}
      {#if track.steps.length > 0}
        <button
          type="button"
          class="track-icon-btn danger"
          onclick={() => onClearTrack(channelId)}
          title="Clear Track"
        >
          Clear
        </button>
      {/if}
    </div>
  </div>

  <!-- Blocks Sequencer Lane -->
  <div class="track-sequencer-lane">
    {#if track.steps.length === 0}
      <div class="empty-track-lane">
        <button
          type="button"
          class="empty-add-btn"
          onclick={() => onOpenAddScene(channelId)}
        >
          + Add Scene to {port}
        </button>
      </div>
    {:else}
      <div class="blocks-container">
        {#each track.steps as step, idx}
          {@const isSelected = selectedChannel === channelId && selectedStep === idx}
          {@const isActive = activeStepInfo?.stepIndex === idx}
          {@const scene = scenes.get(step.presetIndex)}
          <TimelineBlock
            {step}
            stepIndex={idx}
            {channelId}
            {pixelsPerBeat}
            {scene}
            {isSelected}
            isActiveDuringPlayback={isActive}
            onSelect={() => onSelectBlock(channelId, idx)}
            onResize={(newBeats) => onResizeBlock(channelId, idx, newBeats)}
            onDragStart={(e) => handleDragStart(e, idx)}
            onDragOver={handleDragOver}
            onDrop={(e) => handleDrop(e, idx)}
          />
        {/each}
      </div>
    {/if}
  </div>
</div>

<style>
  .timeline-track-row {
    display: flex;
    align-items: stretch;
    height: 56px;
    background: #0f172a;
    border-bottom: 1px solid #1e293b;
    transition: opacity 0.15s ease;
  }

  .timeline-track-row.muted {
    opacity: 0.55;
  }

  .track-pinned-header {
    width: 220px;
    min-width: 220px;
    max-width: 220px;
    background: #0b0f19;
    border-right: 2px solid #334155;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    padding: 6px 10px;
    position: sticky;
    left: 0;
    z-index: 15;
    box-sizing: border-box;
  }

  .channel-info {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 6px;
  }

  .channel-badge {
    font-size: 11px;
    font-weight: 800;
    color: #38bdf8;
    background: rgba(56, 189, 248, 0.15);
    padding: 1px 6px;
    border-radius: 4px;
    border: 1px solid rgba(56, 189, 248, 0.3);
  }

  .channel-badge.pwm-badge {
    color: #fbbf24;
    background: rgba(251, 191, 36, 0.15);
    border-color: rgba(251, 191, 36, 0.3);
  }

  .mute-toggle-btn {
    font-size: 9px;
    font-weight: 700;
    padding: 1px 6px;
    border-radius: 3px;
    border: 1px solid #334155;
    background: #1e293b;
    color: #64748b;
    cursor: pointer;
    text-transform: uppercase;
    transition: all 0.15s ease;
  }

  .mute-toggle-btn.is-active {
    background: rgba(16, 185, 129, 0.15);
    color: #10b981;
    border-color: rgba(16, 185, 129, 0.4);
  }

  .track-metrics {
    display: flex;
    align-items: center;
  }

  .total-beats-label {
    font-size: 10px;
    font-weight: 600;
    color: #94a3b8;
  }

  .track-action-bar {
    display: flex;
    align-items: center;
    gap: 4px;
  }

  .track-icon-btn {
    font-size: 9px;
    font-weight: 600;
    background: #1e293b;
    border: 1px solid #334155;
    color: #94a3b8;
    padding: 1px 5px;
    border-radius: 3px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .track-icon-btn:hover:not(:disabled) {
    background: #334155;
    color: #f1f5f9;
  }

  .track-icon-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .track-icon-btn.add-btn {
    color: #38bdf8;
    border-color: rgba(56, 189, 248, 0.3);
  }

  .track-icon-btn.danger:hover {
    background: rgba(239, 68, 68, 0.2);
    color: #ef4444;
    border-color: #ef4444;
  }

  .track-sequencer-lane {
    flex: 1;
    display: flex;
    align-items: center;
    padding: 0 4px;
    position: relative;
    overflow: visible;
  }

  .empty-track-lane {
    display: flex;
    align-items: center;
    height: 100%;
    padding-left: 8px;
  }

  .empty-add-btn {
    font-size: 11px;
    font-weight: 600;
    background: rgba(30, 41, 59, 0.5);
    border: 1px dashed #334155;
    color: #64748b;
    padding: 6px 14px;
    border-radius: 4px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .empty-add-btn:hover {
    border-color: #38bdf8;
    color: #38bdf8;
    background: rgba(56, 189, 248, 0.05);
  }

  .blocks-container {
    display: flex;
    align-items: center;
    gap: 2px;
    height: 100%;
  }
</style>
