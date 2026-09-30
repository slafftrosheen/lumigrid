<script lang="ts">
  import type { TimelineStep } from '$lib/api/timeline';
  import type { ScenePreset } from '$lib/api/scenes';
  import { getChannelPort, isPwmChannel } from '$lib/api/timeline';
  import { formatDurationSeconds } from '$lib/timeline/evaluation';

  interface Props {
    channelId: number;
    stepIndex: number;
    step: TimelineStep;
    scene?: ScenePreset;
    bpm: number;
    canMoveLeft: boolean;
    canMoveRight: boolean;
    canDuplicate: boolean;
    onUpdateLength: (beats: number) => void;
    onOpenChangeScene: () => void;
    onDuplicate: () => void;
    onDelete: () => void;
    onMoveLeft: () => void;
    onMoveRight: () => void;
    onCopy: () => void;
    onClose: () => void;
  }

  const {
    channelId,
    stepIndex,
    step,
    scene,
    bpm,
    canMoveLeft,
    canMoveRight,
    canDuplicate,
    onUpdateLength,
    onOpenChangeScene,
    onDuplicate,
    onDelete,
    onMoveLeft,
    onMoveRight,
    onCopy,
    onClose,
  }: Props = $props();

  const port = $derived(getChannelPort(channelId));
  const isPwm = $derived(isPwmChannel(channelId));
  const sceneName = $derived(scene?.name || `Scene ${step.presetIndex + 1}`);
  const durationText = $derived(formatDurationSeconds(step.lengthBeats, bpm));

  function handleLengthInput(e: Event) {
    const val = parseInt((e.target as HTMLInputElement).value, 10);
    if (!isNaN(val)) {
      onUpdateLength(val);
    }
  }
</script>

<div class="block-inspector-panel">
  <div class="inspector-header">
    <div class="inspector-title-group">
      <span class="channel-chip" class:pwm-chip={isPwm}>{port}</span>
      <span class="inspector-title">Block #{stepIndex + 1}</span>
    </div>
    <button type="button" class="close-btn" onclick={onClose} title="Close inspector">
      ✕
    </button>
  </div>

  <div class="inspector-body">
    <!-- Scene Reference & Slot -->
    <div class="field-row">
      <span class="field-label">SCENE</span>
      <div class="scene-reference-box">
        <span class="scene-slot-tag">S{String(step.presetIndex + 1).padStart(2, '0')}</span>
        <span class="scene-name-text" title={sceneName}>{sceneName}</span>
        <button type="button" class="change-scene-btn" onclick={onOpenChangeScene}>
          Change
        </button>
      </div>
    </div>

    <!-- Length & Duration -->
    <div class="field-row">
      <span class="field-label">LENGTH</span>
      <div class="length-input-group">
        <input
          type="number"
          min="1"
          max="64"
          value={step.lengthBeats}
          onchange={handleLengthInput}
          class="length-input"
        />
        <span class="beats-unit">beats</span>
        <span class="duration-display">({durationText} @ {bpm} BPM)</span>
      </div>
    </div>

    <!-- Actions -->
    <div class="inspector-actions">
      <button
        type="button"
        class="action-btn"
        onclick={onMoveLeft}
        disabled={!canMoveLeft}
        title="Move Left (Earlier)"
      >
        ◀ Move Left
      </button>

      <button
        type="button"
        class="action-btn"
        onclick={onMoveRight}
        disabled={!canMoveRight}
        title="Move Right (Later)"
      >
        Move Right ▶
      </button>

      <button
        type="button"
        class="action-btn"
        onclick={onCopy}
        title="Copy Block to Clipboard"
      >
        Copy
      </button>

      <button
        type="button"
        class="action-btn"
        onclick={onDuplicate}
        disabled={!canDuplicate}
        title="Duplicate Block"
      >
        Duplicate
      </button>

      <button
        type="button"
        class="action-btn danger"
        onclick={onDelete}
        title="Delete Block"
      >
        Delete
      </button>
    </div>
  </div>
</div>

<style>
  .block-inspector-panel {
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 12px 16px;
    margin-top: 12px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .inspector-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    border-bottom: 1px solid #1e293b;
    padding-bottom: 8px;
  }

  .inspector-title-group {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .channel-chip {
    font-size: 11px;
    font-weight: 800;
    color: #38bdf8;
    background: rgba(56, 189, 248, 0.15);
    padding: 2px 6px;
    border-radius: 4px;
    border: 1px solid rgba(56, 189, 248, 0.3);
  }

  .channel-chip.pwm-chip {
    color: #fbbf24;
    background: rgba(251, 191, 36, 0.15);
    border-color: rgba(251, 191, 36, 0.3);
  }

  .inspector-title {
    font-size: 13px;
    font-weight: 700;
    color: #f1f5f9;
  }

  .close-btn {
    background: transparent;
    border: none;
    color: #64748b;
    font-size: 14px;
    cursor: pointer;
  }
  .close-btn:hover {
    color: #f1f5f9;
  }

  .inspector-body {
    display: flex;
    align-items: center;
    gap: 24px;
    flex-wrap: wrap;
  }

  .field-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .field-label {
    font-size: 10px;
    font-weight: 700;
    color: #64748b;
    letter-spacing: 0.05em;
  }

  .scene-reference-box {
    display: flex;
    align-items: center;
    gap: 6px;
    background: #1e293b;
    border: 1px solid #334155;
    padding: 3px 8px;
    border-radius: 6px;
  }

  .scene-slot-tag {
    font-size: 10px;
    font-weight: 700;
    color: #38bdf8;
  }

  .scene-name-text {
    font-size: 12px;
    font-weight: 600;
    color: #f1f5f9;
    max-width: 160px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .change-scene-btn {
    font-size: 10px;
    font-weight: 600;
    background: #334155;
    border: none;
    color: #94a3b8;
    padding: 2px 6px;
    border-radius: 4px;
    cursor: pointer;
  }
  .change-scene-btn:hover {
    background: #475569;
    color: white;
  }

  .length-input-group {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .length-input {
    width: 60px;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 4px;
    color: #f1f5f9;
    font-size: 12px;
    font-weight: 700;
    padding: 3px 6px;
    text-align: center;
  }

  .beats-unit {
    font-size: 11px;
    color: #94a3b8;
  }

  .duration-display {
    font-size: 11px;
    color: #64748b;
  }

  .inspector-actions {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-left: auto;
  }

  .action-btn {
    font-size: 11px;
    font-weight: 600;
    background: #1e293b;
    border: 1px solid #334155;
    color: #94a3b8;
    padding: 5px 10px;
    border-radius: 4px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .action-btn:hover:not(:disabled) {
    background: #334155;
    color: #f1f5f9;
  }

  .action-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .action-btn.danger {
    color: #f87171;
    border-color: rgba(239, 68, 68, 0.3);
  }
  .action-btn.danger:hover:not(:disabled) {
    background: rgba(239, 68, 68, 0.2);
    color: #ef4444;
  }
</style>
