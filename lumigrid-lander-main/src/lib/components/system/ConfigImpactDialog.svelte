<script lang="ts">
  import type { ConfigApplyImpact } from '$lib/api/config';

  interface Props {
    impact: ConfigApplyImpact;
    warnings: string[];
    onCancel: () => void;
    onApply: () => void;
    applying?: boolean;
  }

  let { impact, warnings, onCancel, onApply, applying = false }: Props = $props();

  // Build human-readable consequence list from firmware impact — not inferred in UI.
  const consequences = $derived(() => {
    const list: string[] = [];
    if (impact.hotApply) list.push('Changes apply immediately without restart.');
    if (impact.outputRestart) {
      // Distinguish output blackout from full reboot
      list.push('Outputs will briefly blackout while hardware drivers are reconfigured.');
    }
    if (impact.dmxRestart) list.push('Art-Net / sACN DMX input will restart.');
    if (impact.syncRestart) list.push('Node synchronization will reconnect.');
    if (impact.schedulerRestart) list.push('Schedule engine will restart.');
    if (impact.mqttRestart) list.push('MQTT connection will reconnect.');
    // rebootRequired is shown as a distinct message — never confused with outputRestart
    if (impact.rebootRequired) list.push('The node will reboot to apply these changes.');
    return list;
  });
</script>

<div class="impact-dialog-overlay" role="dialog" aria-modal="true" aria-labelledby="impact-dialog-title">
  <div class="impact-dialog">
    <h2 id="impact-dialog-title">Apply Configuration</h2>

    {#if warnings.length > 0}
      <div class="warnings-section" role="alert">
        <h3>Warnings</h3>
        <ul class="warning-list">
          {#each warnings as warning}
            <li>{warning}</li>
          {/each}
        </ul>
      </div>
    {/if}

    <div class="consequences-section">
      <h3>These changes will:</h3>
      {#if consequences().length === 0}
        <p class="no-consequences">No service restarts required.</p>
      {:else}
        <ul class="consequence-list">
          {#each consequences() as consequence}
            <li
              class:reboot={consequence.includes('reboot')}
              class:blackout={consequence.includes('blackout')}
            >
              {consequence}
            </li>
          {/each}
        </ul>
      {/if}

      {#if impact.rebootRequired}
        <div class="reboot-notice" role="alert">
          ⚠ Node will reboot. Outputs will be offline during restart.
        </div>
      {:else if impact.outputRestart}
        <div class="blackout-notice">
          ⚡ Brief output blackout during driver reconfiguration.
        </div>
      {:else}
        <div class="clean-notice">
          ✓ No reboot required.
        </div>
      {/if}
    </div>

    <div class="dialog-actions">
      <button
        type="button"
        class="btn-cancel"
        onclick={onCancel}
        disabled={applying}
        id="config-impact-cancel"
      >
        Cancel
      </button>
      <button
        type="button"
        class="btn-apply"
        onclick={onApply}
        disabled={applying}
        id="config-impact-apply"
      >
        {applying ? 'Applying…' : 'Apply'}
      </button>
    </div>
  </div>
</div>

<style>
  .impact-dialog-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.7);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9000;
  }

  .impact-dialog {
    background: #1e1e22;
    border: 1px solid #3a3a42;
    border-radius: 12px;
    padding: 24px;
    width: 460px;
    max-width: calc(100vw - 32px);
    box-shadow: 0 20px 60px rgba(0, 0, 0, 0.6);
  }

  h2 {
    margin: 0 0 18px;
    font-size: 1.15rem;
    font-weight: 600;
    color: #e8e8ed;
  }

  h3 {
    margin: 0 0 8px;
    font-size: 0.82rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #888;
  }

  .warnings-section {
    background: #2a1f0a;
    border: 1px solid #ff9f0a;
    border-radius: 8px;
    padding: 12px 14px;
    margin-bottom: 16px;
  }

  .warnings-section h3 {
    color: #ff9f0a;
    margin-bottom: 6px;
  }

  .warning-list {
    margin: 0;
    padding-left: 18px;
    color: #ffd569;
    font-size: 0.88rem;
    line-height: 1.5;
  }

  .consequences-section {
    margin-bottom: 20px;
  }

  .consequence-list {
    margin: 0 0 12px;
    padding-left: 18px;
    color: #c8c8d0;
    font-size: 0.9rem;
    line-height: 1.6;
  }

  .consequence-list .reboot {
    color: #ff9f0a;
  }

  .consequence-list .blackout {
    color: #ffd569;
  }

  .no-consequences {
    margin: 0 0 12px;
    color: #30d158;
    font-size: 0.9rem;
  }

  .reboot-notice {
    background: #2a1f0a;
    border: 1px solid #ff9f0a;
    border-radius: 7px;
    padding: 9px 13px;
    color: #ffd569;
    font-size: 0.87rem;
  }

  .blackout-notice {
    background: #1f1f0a;
    border: 1px solid #665500;
    border-radius: 7px;
    padding: 9px 13px;
    color: #b8a848;
    font-size: 0.87rem;
  }

  .clean-notice {
    background: #0a1f10;
    border: 1px solid #1a6030;
    border-radius: 7px;
    padding: 9px 13px;
    color: #30d158;
    font-size: 0.87rem;
  }

  .dialog-actions {
    display: flex;
    gap: 10px;
    justify-content: flex-end;
  }

  .btn-cancel, .btn-apply {
    padding: 9px 22px;
    border-radius: 8px;
    border: 1px solid transparent;
    font-size: 0.92rem;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s, border-color 0.15s;
  }

  .btn-cancel {
    background: #2c2c30;
    border-color: #444;
    color: #c8c8d0;
  }

  .btn-cancel:hover:not(:disabled) {
    background: #353538;
  }

  .btn-apply {
    background: #0a3a1a;
    border-color: #30d158;
    color: #30d158;
  }

  .btn-apply:hover:not(:disabled) {
    background: #0e4a22;
  }

  .btn-cancel:disabled, .btn-apply:disabled {
    opacity: 0.5;
    cursor: default;
  }
</style>
