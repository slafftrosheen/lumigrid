<script lang="ts">
  import type { ScenePreset, SceneChannel } from '$lib/api/scenes';
  import { computeZonePreviewColor, rgbToHex } from '$lib/api/scenes';

  interface Props {
    preset: ScenePreset;
    active?: boolean;
    motionTracksCount?: number;
  }

  const { preset, active = false, motionTracksCount = 0 }: Props = $props();

  const channels = $derived<SceneChannel[]>(preset.channels ?? preset.zones ?? []);

  const aChannels = $derived(
    Array.from({ length: 8 }, (_, i) => {
      const ch = channels[i];
      if (!ch) return { color: '#111827', hasDynamic: false, enabled: false };
      const col = computeZonePreviewColor(ch, preset.globalBrightness);
      const enabled = ch.layers.some(l => l.enabled);
      const hasDynamic = ch.layers.some(l => l.enabled && l.effect !== 'solid');
      return {
        color: enabled ? rgbToHex(col) : '#1a1f2c',
        hasDynamic,
        enabled,
      };
    })
  );

  const pChannels = $derived(
    Array.from({ length: 8 }, (_, i) => {
      const ch = channels[i + 8];
      if (!ch) return { intensityPct: 0, enabled: false };
      const enabled = ch.layers.some(l => l.enabled);
      const col = computeZonePreviewColor(ch, preset.globalBrightness);
      // PWM intensity is derived from max RGB component scaled by global brightness
      const maxVal = Math.max(col.r, col.g, col.b);
      const pct = enabled ? Math.round((maxVal / 255) * 100) : 0;
      return {
        intensityPct: pct,
        enabled,
      };
    })
  );

  const hasMotion = $derived(motionTracksCount > 0);
</script>

<div class="scene-thumbnail" class:active aria-label={`Preview for ${preset.name}`}>
  <!-- A1..A8 Addressable Color Ribbon -->
  <div class="a-ribbon" title="Addressable outputs A1–A8">
    {#each aChannels as ch, i}
      <div
        class="a-cell"
        class:dynamic={ch.hasDynamic}
        class:disabled={!ch.enabled}
        style="--ch-color: {ch.color};"
        title={`A${i + 1}: ${ch.enabled ? ch.color : 'Off'}`}
      >
        {#if ch.hasDynamic}
          <div class="dynamic-wave"></div>
        {/if}
      </div>
    {/each}
  </div>

  <!-- P1..P8 PWM Intensity Bars -->
  <div class="p-ribbon" title="PWM dimmer outputs P1–P8">
    {#each pChannels as ch, i}
      <div class="p-bar-track" title={`P${i + 1}: ${ch.intensityPct}%`}>
        <div
          class="p-bar-fill"
          style="height: {ch.intensityPct}%;"
        ></div>
      </div>
    {/each}
  </div>

  {#if hasMotion}
    <div class="motion-glyph" title={`${motionTracksCount} automation track${motionTracksCount > 1 ? 's' : ''}`}>
      <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
        <path d="M2 12h2a4 4 0 0 1 4 4 4 4 0 0 0 4 4h0a4 4 0 0 0 4-4 4 4 0 0 1 4-4h2" />
      </svg>
    </div>
  {/if}
</div>

<style>
  .scene-thumbnail {
    position: relative;
    width: 100%;
    height: 38px;
    background: #0f1422;
    border-radius: 6px;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    gap: 3px;
    padding: 3px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    box-sizing: border-box;
  }

  .scene-thumbnail.active {
    border-color: rgba(99, 102, 241, 0.5);
    box-shadow: 0 0 8px rgba(99, 102, 241, 0.25);
  }

  .a-ribbon {
    display: flex;
    height: 18px;
    width: 100%;
    border-radius: 3px;
    overflow: hidden;
    background: #07090e;
    gap: 1px;
  }

  .a-cell {
    flex: 1;
    height: 100%;
    background-color: var(--ch-color);
    position: relative;
    transition: background-color 0.15s ease;
  }

  .a-cell.disabled {
    background-color: #151926;
    opacity: 0.6;
  }

  .dynamic-wave {
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, transparent, rgba(255, 255, 255, 0.35), transparent);
    animation: waveShimmer 2s infinite linear;
  }

  @keyframes waveShimmer {
    0% { transform: translateX(-100%); }
    100% { transform: translateX(100%); }
  }

  .p-ribbon {
    display: flex;
    height: 11px;
    width: 100%;
    align-items: flex-end;
    gap: 2px;
    padding: 0 2px;
  }

  .p-bar-track {
    flex: 1;
    height: 100%;
    background: rgba(255, 255, 255, 0.05);
    border-radius: 1px;
    display: flex;
    align-items: flex-end;
    overflow: hidden;
  }

  .p-bar-fill {
    width: 100%;
    background: #e2e8f0;
    border-radius: 1px;
    min-height: 1px;
    transition: height 0.15s ease;
  }

  .motion-glyph {
    position: absolute;
    top: 2px;
    right: 2px;
    background: rgba(99, 102, 241, 0.85);
    color: #fff;
    border-radius: 3px;
    padding: 1px 2px;
    display: flex;
    align-items: center;
    justify-content: center;
    pointer-events: none;
  }
</style>
