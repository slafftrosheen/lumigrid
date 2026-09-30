<script lang="ts">
  import type { TimelineStep } from '$lib/api/timeline';
  import type { ScenePreset } from '$lib/api/scenes';
  import { computeZonePreviewColor, rgbToHex } from '$lib/api/scenes';
  import { isPwmChannel, getChannelPort } from '$lib/api/timeline';

  interface Props {
    step: TimelineStep;
    stepIndex: number;
    channelId: number;
    pixelsPerBeat: number;
    scene?: ScenePreset;
    isSelected: boolean;
    isActiveDuringPlayback: boolean;
    onSelect: () => void;
    onResize: (newBeats: number) => void;
    onDragStart: (e: DragEvent) => void;
    onDragOver: (e: DragEvent) => void;
    onDrop: (e: DragEvent) => void;
  }

  const {
    step,
    stepIndex,
    channelId,
    pixelsPerBeat,
    scene,
    isSelected,
    isActiveDuringPlayback,
    onSelect,
    onResize,
    onDragStart,
    onDragOver,
    onDrop,
  }: Props = $props();

  const widthPx = $derived(Math.max(36, step.lengthBeats * pixelsPerBeat));
  const isPwm = $derived(isPwmChannel(channelId));

  // Compute preview for this specific channel
  const previewInfo = $derived.by(() => {
    if (!scene) {
      return {
        colorHex: '#334155',
        intensityPct: 50,
        hasDynamic: false,
        name: `Scene ${step.presetIndex + 1}`,
      };
    }

    const ch = scene.channels?.[channelId];
    if (!ch) {
      return {
        colorHex: '#1e293b',
        intensityPct: 0,
        hasDynamic: false,
        name: scene.name || `Scene ${step.presetIndex + 1}`,
      };
    }

    const col = computeZonePreviewColor(ch, scene.globalBrightness);
    const colorHex = rgbToHex(col);
    const maxVal = Math.max(col.r, col.g, col.b);
    const intensityPct = Math.round((maxVal / 255) * 100);
    const hasDynamic = ch.layers.some((l) => l.enabled && l.effect !== 'solid');

    return {
      colorHex,
      intensityPct,
      hasDynamic,
      name: scene.name || `Scene ${step.presetIndex + 1}`,
    };
  });

  // Resize interaction
  let isResizing = $state(false);
  let resizeStartX = $state(0);
  let resizeStartBeats = $state(0);

  function startResize(e: PointerEvent) {
    e.stopPropagation();
    isResizing = true;
    resizeStartX = e.clientX;
    resizeStartBeats = step.lengthBeats;
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  }

  function handleResizePointerMove(e: PointerEvent) {
    if (!isResizing) return;
    const deltaPx = e.clientX - resizeStartX;
    const deltaBeats = Math.round(deltaPx / pixelsPerBeat);
    const newBeats = Math.max(1, Math.min(64, resizeStartBeats + deltaBeats));
    if (newBeats !== step.lengthBeats) {
      onResize(newBeats);
    }
  }

  function handleResizePointerUp(e: PointerEvent) {
    if (!isResizing) return;
    isResizing = false;
    try {
      (e.target as HTMLElement).releasePointerCapture(e.pointerId);
    } catch {}
  }
</script>

<div
  class="timeline-block"
  class:selected={isSelected}
  class:active-playback={isActiveDuringPlayback}
  class:pwm={isPwm}
  style="width: {widthPx}px; --block-color: {previewInfo.colorHex};"
  draggable={!isResizing}
  ondragstart={onDragStart}
  ondragover={onDragOver}
  ondrop={onDrop}
  onclick={onSelect}
  role="button"
  tabindex="0"
  onkeydown={(e) => e.key === 'Enter' && onSelect()}
  aria-label={`Block ${stepIndex + 1}: ${previewInfo.name}, ${step.lengthBeats} beats`}
>
  <!-- Active Playback Glow Highlight -->
  {#if isActiveDuringPlayback}
    <div class="playback-halo"></div>
  {/if}

  <!-- Header: Scene Name + Slot Tag -->
  <div class="block-header">
    <span class="scene-name" title={previewInfo.name}>{previewInfo.name}</span>
    <span class="slot-tag">S{String(step.presetIndex + 1).padStart(2, '0')}</span>
  </div>

  <!-- Channel Preview Visualizer -->
  <div class="block-visualizer">
    {#if isPwm}
      <!-- PWM Dimmer Treatment -->
      <div class="pwm-bar-track">
        <div class="pwm-bar-fill" style="width: {previewInfo.intensityPct}%;"></div>
      </div>
      <span class="pwm-pct-label">{previewInfo.intensityPct}%</span>
    {:else}
      <!-- Addressable Color Strip -->
      <div
        class="color-chip"
        class:dynamic={previewInfo.hasDynamic}
        style="background: {previewInfo.colorHex};"
      ></div>
    {/if}
  </div>

  <!-- Footer: Beat Duration -->
  <div class="block-footer">
    <span class="beats-tag">{step.lengthBeats}b</span>
  </div>

  <!-- Right Resize Handle -->
  <div
    class="resize-handle"
    onpointerdown={startResize}
    onpointermove={handleResizePointerMove}
    onpointerup={handleResizePointerUp}
    onpointercancel={handleResizePointerUp}
    title="Drag to resize (1–64 beats)"
    role="slider"
    aria-label="Resize step beats"
    aria-valuenow={step.lengthBeats}
    aria-valuemin="1"
    aria-valuemax="64"
    tabindex="0"
  ></div>
</div>

<style>
  .timeline-block {
    position: relative;
    height: 48px;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 6px;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    padding: 3px 6px;
    cursor: pointer;
    user-select: none;
    box-sizing: border-box;
    overflow: hidden;
    transition: border-color 0.15s ease, box-shadow 0.15s ease;
  }

  .timeline-block:hover {
    border-color: #64748b;
  }

  .timeline-block.selected {
    border-color: #38bdf8;
    box-shadow: 0 0 0 1.5px rgba(56, 189, 248, 0.6);
    background: #1e2d42;
  }

  .timeline-block.active-playback {
    border-color: #10b981;
    box-shadow: 0 0 10px rgba(16, 185, 129, 0.5);
  }

  .playback-halo {
    position: absolute;
    inset: 0;
    border: 2px solid #10b981;
    border-radius: 5px;
    pointer-events: none;
    animation: pulse 1.2s infinite alternate ease-in-out;
  }

  @keyframes pulse {
    0% { opacity: 0.6; }
    100% { opacity: 1; }
  }

  .block-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 4px;
    font-size: 10px;
    line-height: 1.1;
  }

  .scene-name {
    font-weight: 600;
    color: #f1f5f9;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 80%;
  }

  .slot-tag {
    font-size: 9px;
    font-weight: 700;
    color: #94a3b8;
    background: rgba(0, 0, 0, 0.3);
    padding: 1px 3px;
    border-radius: 3px;
  }

  .block-visualizer {
    display: flex;
    align-items: center;
    gap: 4px;
    height: 10px;
  }

  .color-chip {
    width: 100%;
    height: 6px;
    border-radius: 2px;
    box-shadow: 0 1px 2px rgba(0, 0, 0, 0.4);
  }

  .color-chip.dynamic {
    background: linear-gradient(90deg, var(--block-color), #f59e0b) !important;
  }

  .pwm-bar-track {
    flex: 1;
    height: 6px;
    background: #0f172a;
    border-radius: 2px;
    overflow: hidden;
  }

  .pwm-bar-fill {
    height: 100%;
    background: #fbbf24;
    border-radius: 2px;
  }

  .pwm-pct-label {
    font-size: 9px;
    font-weight: 600;
    color: #fbbf24;
  }

  .block-footer {
    display: flex;
    align-items: center;
    justify-content: flex-end;
  }

  .beats-tag {
    font-size: 9px;
    font-weight: 700;
    color: #64748b;
    letter-spacing: 0.02em;
  }

  .resize-handle {
    position: absolute;
    top: 0;
    right: 0;
    bottom: 0;
    width: 8px;
    cursor: ew-resize;
    background: transparent;
    z-index: 10;
    transition: background 0.15s ease;
  }

  .resize-handle:hover {
    background: rgba(56, 189, 248, 0.4);
  }
</style>
