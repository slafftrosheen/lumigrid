<script lang="ts">
  /**
   * ManualCueTBar — manual-mode crossfade T-Bar (LG-034 §60/§20/§21).
   *
   * Canonical truth is node telemetry (transport.cue.manualFade Q15). While the
   * pointer is down, local drag state overrides rendering; release reconciles
   * back to authoritative telemetry. Requests are throttled to 10 Hz with an
   * immediate trailing flush on release; failures surface (never swallowed).
   */
  import { q15ToUnit, ThrottledSender } from '$lib/cues/timing';

  interface Props {
    /** Authoritative node telemetry Q15 0..32768. */
    manualFadeQ15: number;
    currentLabel: string;
    nextLabel: string;
    currentIndex: number;
    nextIndex: number;
    disabled?: boolean;
    oncrossfade: (unit: number) => Promise<void>;
    onerror?: (message: string) => void;
  }

  let {
    manualFadeQ15,
    currentLabel,
    nextLabel,
    currentIndex,
    nextIndex,
    disabled = false,
    oncrossfade,
    onerror,
  }: Props = $props();

  let dragging = $state(false);
  let localValue = $state(0);
  let lastError = $state('');

  // Reconcile to node telemetry whenever not actively dragging (§21).
  let display = $derived(dragging ? localValue : q15ToUnit(manualFadeQ15));
  let displayPct = $derived(Math.round(display * 100));

  const sender = new ThrottledSender(100, async (value) => {
    try {
      await oncrossfade(value);
      lastError = '';
    } catch (err) {
      // §20: never silently swallow crossfade failures.
      lastError = err instanceof Error ? err.message : String(err);
      onerror?.(lastError);
    }
  });

  function onInput(event: Event) {
    if (disabled) return;
    const target = event.target as HTMLInputElement;
    localValue = parseFloat(target.value);
    sender.push(localValue);
  }

  function onPointerDown() {
    if (disabled) return;
    dragging = true;
    localValue = q15ToUnit(manualFadeQ15);
  }

  function onPointerUp() {
    if (!dragging) return;
    dragging = false;
    // Immediate final value on release (§20).
    sender.flush();
  }

  function formatSlot(index: number): string {
    return String(index + 1).padStart(2, '0');
  }
</script>

<div class="tbar" role="group" aria-label="Manual crossfade">
  <div class="tbar-scenes">
    <div class="tbar-scene current">
      <span class="scene-slot">{formatSlot(currentIndex)}</span>
      <span class="scene-label">{currentLabel}</span>
      <span class="scene-role">CURRENT</span>
    </div>
    <div class="tbar-scene next">
      <span class="scene-slot">{formatSlot(nextIndex)}</span>
      <span class="scene-label">{nextLabel}</span>
      <span class="scene-role">NEXT</span>
    </div>
  </div>

  <div class="tbar-value">{displayPct}%</div>

  <input
    class="tbar-slider"
    type="range"
    min="0"
    max="1"
    step="0.01"
    value={display}
    disabled={disabled}
    aria-label="Manual crossfade position"
    oninput={onInput}
    onpointerdown={onPointerDown}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
  />

  <div class="tbar-scale">
    <span>A</span>
    <span>B</span>
  </div>

  {#if lastError}
    <div class="tbar-error" role="alert">Crossfade failed: {lastError}</div>
  {/if}
</div>

<style>
  .tbar {
    display: flex;
    flex-direction: column;
    gap: 8px;
    min-width: 260px;
  }
  .tbar-scenes {
    display: flex;
    justify-content: space-between;
    gap: 12px;
  }
  .tbar-scene {
    display: flex;
    flex-direction: column;
    gap: 1px;
    max-width: 45%;
  }
  .tbar-scene.next {
    text-align: right;
    margin-left: auto;
  }
  .scene-slot {
    font-family: monospace;
    font-size: 0.68rem;
    color: rgba(255, 255, 255, 0.45);
  }
  .scene-label {
    font-size: 0.86rem;
    font-weight: 600;
    color: #fff;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .tbar-scene.current .scene-label {
    color: var(--dash-accent);
  }
  .scene-role {
    font-size: 0.6rem;
    font-weight: 800;
    letter-spacing: 0.08em;
    color: rgba(255, 255, 255, 0.35);
  }
  .tbar-value {
    font-family: monospace;
    font-size: 1rem;
    font-weight: 700;
    text-align: center;
    color: #fff;
  }
  .tbar-slider {
    accent-color: var(--dash-accent);
    cursor: pointer;
    width: 100%;
  }
  .tbar-scale {
    display: flex;
    justify-content: space-between;
    font-family: monospace;
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.4);
  }
  .tbar-error {
    font-size: 0.75rem;
    color: var(--dash-danger, #ff453a);
    background: rgba(255, 69, 58, 0.1);
    border: 1px solid rgba(255, 69, 58, 0.35);
    border-radius: 6px;
    padding: 6px 10px;
  }
</style>
