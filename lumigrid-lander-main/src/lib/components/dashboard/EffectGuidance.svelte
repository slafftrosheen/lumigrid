<script lang="ts">
  import type { EffectMeta } from '$lib/api/scenes';
  import { effectGuidance } from '$lib/api/effectCatalog';

  export let effect: EffectMeta | undefined = undefined;
  export let channel: number;
  export let pixelCount: number | undefined = undefined;

  $: guidance = effectGuidance(effect, channel, pixelCount);
  $: validChannel = Number.isInteger(channel) && channel >= 0 && channel < 16;
  $: outputLabel = !validChannel ? 'Output not selected' : channel < 8
    ? `Animated strip A${channel + 1}` : `Dimmer P${channel - 7}`;
</script>

<aside class="effect-guidance" data-state={guidance.state} role="note" aria-label="Effect suitability">
  <strong>{outputLabel}</strong>
  <span>{guidance.message}</span>
</aside>

<style>
  .effect-guidance { display:grid; gap:.25rem; padding:.75rem .9rem; margin:.6rem 0; border:1px solid var(--dash-border); border-left:3px solid var(--dash-accent); border-radius:8px; color:var(--dash-text,var(--ink)); background:var(--dash-surface); font-size:.86rem; line-height:1.5; overflow-wrap:anywhere; }
  .effect-guidance strong { font-size:.82rem; font-weight:650; }
  .effect-guidance span { color:var(--dash-muted,var(--muted)); }
  .effect-guidance[data-state="limited"], .effect-guidance[data-state="unknown"] { border-left-color:var(--dash-warm); }
  .effect-guidance[data-state="unavailable"] { border-left-color:var(--dash-danger); }
</style>
