<script lang="ts">
  import {
    TRANSITION_CURVES,
    sampleCurveNormalized,
    type TransitionCurve,
  } from '$lib/runtime/curves';

  let {
    curve = $bindable<TransitionCurve>('easeInOut'),
    durationMs = $bindable<number>(500),
    canMorph = false,
  } = $props();

  const previewPath = $derived.by(() => {
    const samples = 40;
    const w = 120;
    const h = 40;
    const pad = 4;
    const pw = w - 2 * pad;
    const ph = h - 2 * pad;

    const pts: string[] = [];
    for (let i = 0; i <= samples; i++) {
      const t = i / samples;
      const v = sampleCurveNormalized(curve, t);
      const x = pad + t * pw;
      const y = pad + (1 - v) * ph;
      pts.push(`${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`);
    }
    return pts.join(' ');
  });
</script>

<div class="transition-card">
  <div class="transition-fields">
    <div class="field-col">
      <label for="trans-duration">Fade Duration</label>
      <div class="input-with-unit">
        <input
          id="trans-duration"
          type="number"
          min="0"
          max="10000"
          step="50"
          bind:value={durationMs}
          class="input-number"
        />
        <span class="unit">ms</span>
      </div>
    </div>

    <div class="field-col">
      <label for="trans-curve">Transition Curve</label>
      <select id="trans-curve" bind:value={curve} class="select-curve">
        {#each TRANSITION_CURVES as c}
          <option value={c}>{c}</option>
        {/each}
      </select>
    </div>

    <div class="field-preview">
      <svg viewBox="0 0 120 40" class="mini-curve-svg">
        <rect x="0" y="0" width="120" height="40" fill="rgba(0,0,0,0.3)" rx="4" />
        <path d={previewPath} fill="none" stroke="#ff2d55" stroke-width="2" />
      </svg>
    </div>
  </div>

  {#if canMorph}
    <div class="morph-badge">
      <span class="morph-icon">✦</span>
      <span>Continuous parameter morph active for matching effects</span>
    </div>
  {/if}
</div>

<style>
  .transition-card {
    display: flex;
    flex-direction: column;
    gap: 8px;
    background: rgba(255,255,255,.035);
    border: 1px solid rgba(255,255,255,.09);
    border-radius: var(--dash-radius-md);
    padding: 12px;
  }

  .transition-fields {
    display: flex;
    align-items: center;
    gap: 14px;
    flex-wrap: wrap;
  }

  .field-col {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .field-col label {
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.5);
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .input-with-unit {
    display: flex;
    align-items: center;
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    padding: 0 8px;
  }

  .input-number {
    background: transparent;
    border: none;
    color: #fff;
    font-size: 0.85rem;
    width: 65px;
    padding: 6px 0;
  }

  .input-number:focus-visible {
    outline: 2px solid var(--dash-accent);
    outline-offset: 2px;
  }

  .unit {
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.4);
    font-family: monospace;
  }

  .select-curve {
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    color: #fff;
    padding: 6px 8px;
    font-size: 0.85rem;
  }

  .select-curve:focus-visible {
    outline: 2px solid var(--dash-accent);
    outline-offset: 2px;
    border-color: var(--dash-accent);
  }

  .field-preview {
    display: flex;
    align-items: flex-end;
  }

  .mini-curve-svg {
    width: 100px;
    height: 34px;
    display: block;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
  }

  .morph-badge {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 0.75rem;
    color: var(--dash-ok);
    font-family: monospace;
  }

  .morph-icon {
    font-size: 0.8rem;
  }
</style>
