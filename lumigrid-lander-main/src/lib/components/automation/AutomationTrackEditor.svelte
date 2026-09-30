<script lang="ts">
  import AutomationCurve from './AutomationCurve.svelte';
  import {
    TRANSITION_CURVES,
    curveNameToId,
    type AutomationTrack,
  } from '$lib/runtime/curves';
  import type { AutomationTrackConfig, AutomationTarget } from '$lib/api/sceneMotion';
  import type { EffectMeta, SceneChannel } from '$lib/api/scenes';

  let {
    track = $bindable<AutomationTrackConfig>({
      target: 'globalBrightness',
      channel: 0,
      layer: 0,
      curve: 'easeInOut',
      loop: true,
      periodMs: 5000,
      points: [
        { position: 0, value: 0 },
        { position: 32768, value: 65535 },
        { position: 65535, value: 0 },
      ],
    }),
    trackIndex = 0,
    effects = [] as EffectMeta[],
    channels = [] as SceneChannel[],
    onDelete = () => {},
  }: {
    track?: AutomationTrackConfig;
    trackIndex?: number;
    effects?: EffectMeta[];
    channels?: SceneChannel[];
    onDelete?: () => void;
  } = $props();

  const TARGETS: { id: AutomationTarget; label: string }[] = [
    { id: 'globalBrightness', label: 'Global Brightness' },
    { id: 'layerOpacity', label: 'Layer Opacity' },
    { id: 'layerSpeed', label: 'Layer Speed' },
    { id: 'layerParameter', label: 'Layer Effect Parameter' },
  ];

  const CHANNEL_PORTS = [
    'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8',
    'P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8',
  ] as const;

  const activeEffectMeta = $derived.by(() => {
    if (track.target !== 'layerParameter') return null;
    const ch = channels[track.channel];
    if (!ch) return null;
    const layer = ch.layers[track.layer];
    if (!layer) return null;
    return effects.find(e => e.id === layer.effect) || null;
  });

  const targetScaleDescription = $derived.by(() => {
    switch (track.target) {
      case 'globalBrightness':
      case 'layerOpacity':
        return 'Value range: 0% → 100%';
      case 'layerSpeed':
        return 'Value range: 10 ms → 60,000 ms';
      case 'layerParameter':
        if (activeEffectMeta && activeEffectMeta.hasParameter) {
          const lbl = activeEffectMeta.parameterLabel || 'Parameter';
          const unit = activeEffectMeta.parameterUnit ? ` (${activeEffectMeta.parameterUnit})` : '';
          const min = activeEffectMeta.parameterMin ?? 0;
          const max = activeEffectMeta.parameterMax ?? 255;
          return `${lbl}${unit}: ${min} → ${max}`;
        }
        return 'Value range: 0 → 255';
    }
  });

  function formatPointValue(valNorm: number): string {
    const fraction = valNorm / 65535;
    switch (track.target) {
      case 'globalBrightness':
      case 'layerOpacity':
        return `${Math.round(fraction * 100)}%`;
      case 'layerSpeed': {
        const ms = Math.round(10 + fraction * (60000 - 10));
        return ms >= 1000 ? `${(ms / 1000).toFixed(1)} s` : `${ms} ms`;
      }
      case 'layerParameter': {
        if (activeEffectMeta && activeEffectMeta.hasParameter) {
          const min = activeEffectMeta.parameterMin ?? 0;
          const max = activeEffectMeta.parameterMax ?? 255;
          const val = Math.round(min + fraction * (max - min));
          const unit = activeEffectMeta.parameterUnit ? ` ${activeEffectMeta.parameterUnit}` : '';
          return `${val}${unit}`;
        }
        return `${Math.round(fraction * 255)}`;
      }
    }
  }

  function formatTimelinePosition(posNorm: number): string {
    return `${Math.round((posNorm / 65535) * 100)}%`;
  }

  function deletePoint(idx: number) {
    if (track.points.length <= 2) return;
    track.points = track.points.filter((_, i) => i !== idx);
  }

  let runtimeTrack = $derived<AutomationTrack>({
    target: 0,
    curve: curveNameToId(track.curve),
    periodMs: track.periodMs,
    loop: track.loop,
    points: track.points,
  });
</script>

<div class="track-editor-card">
  <div class="track-header">
    <div class="track-title">
      <span class="track-badge">TRACK #{trackIndex + 1}</span>
      <select bind:value={track.target} class="select-input">
        {#each TARGETS as tgt}
          <option value={tgt.id}>{tgt.label}</option>
        {/each}
      </select>
    </div>

    <div class="track-actions">
      <label class="loop-toggle">
        <input type="checkbox" bind:checked={track.loop} />
        <span>Loop</span>
      </label>
      <button class="btn-delete" onclick={onDelete} title="Delete Track">✕</button>
    </div>
  </div>

  <div class="track-params-grid">
    <div class="param-field">
      <label for="track-curve-{trackIndex}">Curve Interpolation</label>
      <select id="track-curve-{trackIndex}" bind:value={track.curve} class="select-input">
        {#each TRANSITION_CURVES as c}
          <option value={c}>{c}</option>
        {/each}
      </select>
    </div>

    <div class="param-field">
      <label for="track-period-{trackIndex}">Period (ms)</label>
      <input
        id="track-period-{trackIndex}"
        type="number"
        min="100"
        max="3600000"
        step="100"
        bind:value={track.periodMs}
        class="number-input"
      />
    </div>

    {#if track.target !== 'globalBrightness'}
      <div class="param-field">
        <label for="track-channel-{trackIndex}">Channel</label>
        <select id="track-channel-{trackIndex}" bind:value={track.channel} class="select-input">
          {#each CHANNEL_PORTS as port, channel}
            <option value={channel}>{port} · {channel < 8 ? 'Addressable' : 'PWM'}</option>
          {/each}
        </select>
      </div>

      <div class="param-field">
        <label for="track-layer-{trackIndex}">Layer</label>
        <select id="track-layer-{trackIndex}" bind:value={track.layer} class="select-input">
          <option value={0}>Layer 1</option>
          <option value={1}>Layer 2</option>
          <option value={2}>Layer 3</option>
        </select>
      </div>
    {/if}
  </div>

  {#if track.target === 'layerParameter' && activeEffectMeta}
    <div class="target-context-hint">
      Target: <strong>{activeEffectMeta.label}</strong> — {activeEffectMeta.parameterLabel || 'Parameter'}
      {#if !activeEffectMeta.hasParameter}
        <span class="warning-tag">Note: Current effect does not support parameters</span>
      {/if}
    </div>
  {/if}

  <div class="scale-summary-bar">
    <span>Timeline: 0% → 100%</span>
    <span class="scale-desc">{targetScaleDescription}</span>
  </div>

  <div class="curve-wrapper">
    <AutomationCurve
      track={runtimeTrack}
      onChange={(points) => (track.points = points)}
      color={trackIndex === 0 ? '#6366f1' : trackIndex === 1 ? '#ec4899' : trackIndex === 2 ? '#f59e0b' : '#10b981'}
    />
  </div>

  <!-- Points list for accessible keyboard/touch management -->
  <div class="points-shelf">
    <span class="points-label">Points ({track.points.length}/6):</span>
    <div class="points-chips">
      {#each track.points as pt, pIdx}
        <div class="point-chip" title={`Position: ${formatTimelinePosition(pt.position)}, Value: ${formatPointValue(pt.value)}`}>
          <span class="pt-pos">{formatTimelinePosition(pt.position)}</span>
          <span class="pt-arrow">→</span>
          <span class="pt-val">{formatPointValue(pt.value)}</span>
          {#if track.points.length > 2}
            <button
              type="button"
              class="pt-del-btn"
              onclick={() => deletePoint(pIdx)}
              title="Delete this point"
            >✕</button>
          {/if}
        </div>
      {/each}
    </div>
  </div>
</div>

<style>
  .track-editor-card {
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.09);
    border-radius: var(--dash-radius-md, 10px);
    padding: 14px;
    margin-bottom: 12px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .track-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .track-title {
    display: flex;
    align-items: center;
    gap: 10px;
    flex: 1;
  }

  .track-badge {
    font-size: 0.72rem;
    font-weight: 700;
    color: #6366f1;
    background: rgba(99, 102, 241, 0.15);
    padding: 3px 8px;
    border-radius: 4px;
    letter-spacing: 0.05em;
  }

  .track-actions {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .loop-toggle {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 0.8rem;
    color: rgba(255, 255, 255, 0.7);
    cursor: pointer;
  }

  .btn-delete {
    background: none;
    border: none;
    color: rgba(255, 255, 255, 0.4);
    cursor: pointer;
    font-size: 1rem;
    padding: 4px 6px;
    border-radius: 4px;
    transition: color 0.15s ease, background 0.15s ease;
  }

  .btn-delete:hover {
    color: #ef4444;
    background: rgba(239, 68, 68, 0.1);
  }

  .track-params-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
    gap: 10px;
  }

  .param-field {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .param-field label {
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.5);
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .select-input, .number-input {
    min-height: 34px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.13);
    border-radius: 6px;
    color: #fff;
    padding: 5px 8px;
    font-size: 0.82rem;
    font-family: inherit;
    box-sizing: border-box;
  }

  .target-context-hint {
    font-size: 0.78rem;
    color: rgba(255, 255, 255, 0.8);
    background: rgba(99, 102, 241, 0.08);
    border: 1px solid rgba(99, 102, 241, 0.2);
    border-radius: 6px;
    padding: 6px 10px;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .warning-tag {
    color: #f59e0b;
    font-size: 0.72rem;
  }

  .scale-summary-bar {
    display: flex;
    justify-content: space-between;
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.5);
    padding: 0 4px;
  }

  .scale-desc {
    color: #a5b4fc;
    font-weight: 500;
  }

  .curve-wrapper {
    margin-top: 2px;
  }

  .points-shelf {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
    padding-top: 4px;
  }

  .points-label {
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.5);
  }

  .points-chips {
    display: flex;
    gap: 6px;
    flex-wrap: wrap;
  }

  .point-chip {
    display: flex;
    align-items: center;
    gap: 4px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 0.72rem;
    color: #e2e8f0;
  }

  .pt-pos {
    color: #94a3b8;
  }

  .pt-arrow {
    color: rgba(255, 255, 255, 0.3);
  }

  .pt-val {
    color: #38bdf8;
    font-weight: 600;
  }

  .pt-del-btn {
    background: none;
    border: none;
    color: rgba(255, 255, 255, 0.4);
    cursor: pointer;
    font-size: 0.7rem;
    padding: 0 2px;
    line-height: 1;
    margin-left: 2px;
  }

  .pt-del-btn:hover {
    color: #ef4444;
  }
</style>
