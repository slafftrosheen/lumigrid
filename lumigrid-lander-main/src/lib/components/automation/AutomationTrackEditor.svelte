<script lang="ts">
  import AutomationCurve from './AutomationCurve.svelte';
  import {
    TRANSITION_CURVES,
    curveIdToName,
    curveNameToId,
    type TransitionCurve,
    type AutomationTrack,
  } from '$lib/runtime/curves';
  import type { AutomationTrackConfig, AutomationTarget } from '$lib/api/sceneMotion';

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
    onDelete = () => {},
  }: {
    track?: AutomationTrackConfig;
    trackIndex?: number;
    onDelete?: () => void;
  } = $props();

  const TARGETS: { id: AutomationTarget; label: string }[] = [
    { id: 'globalBrightness', label: 'Scene Brightness' },
    { id: 'layerOpacity', label: 'Layer Opacity' },
    { id: 'layerSpeed', label: 'Layer Speed' },
    { id: 'layerParameter', label: 'Layer FX Parameter' },
  ];

  const CHANNEL_PORTS = [
    'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8',
    'P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8',
  ] as const;

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
        max="60000"
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

  <div class="curve-wrapper">
    <AutomationCurve
      track={runtimeTrack}
      onChange={(points) => (track.points = points)}
      color={trackIndex === 0 ? '#ff2d55' : trackIndex === 1 ? '#ff3b30' : trackIndex === 2 ? '#ff9500' : '#ffd60a'}
    />
  </div>
</div>

<style>
  .track-editor-card {
    background: rgba(255,255,255,.035);
    border: 1px solid rgba(255,255,255,.09);
    border-radius: var(--dash-radius-md);
    padding: 14px;
    margin-bottom: 12px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .track-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .track-title {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .track-badge {
    font-family: monospace;
    font-size: 0.75rem;
    font-weight: 700;
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.1);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    padding: 2px 8px;
    border-radius: 7px;
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
    color: rgba(255, 255, 255, 0.8);
    cursor: pointer;
  }

  .btn-delete {
    background: transparent;
    border: 1px solid rgba(255, 69, 58, 0.4);
    color: var(--dash-danger);
    border-radius: 8px;
    padding: 5px 9px;
    cursor: pointer;
    font-size: 0.8rem;
    line-height: 1;
    transition: background .15s ease, border-color .15s ease, color .15s ease;
  }

  .btn-delete:hover {
    background: rgba(255, 69, 58, 0.2);
    border-color: var(--dash-danger);
  }

  .track-params-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 10px;
  }

  .param-field {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .param-field label {
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.5);
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .select-input, .number-input {
    min-height: 38px;
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255,255,255,.13);
    border-radius: 8px;
    color: #fff;
    padding: 6px 8px;
    font-size: 0.85rem;
    font-family: inherit;
    transition: border-color 0.15s ease, box-shadow 0.15s ease;
  }

  .select-input:focus-visible, .number-input:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }

  .curve-wrapper {
    margin-top: 4px;
  }
</style>
