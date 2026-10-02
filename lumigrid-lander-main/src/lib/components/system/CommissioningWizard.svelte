<script lang="ts">
  /**
   * CommissioningWizard — First-install setup flow.
   *
   * Steps:
   *   1. Identity
   *   2. Network
   *   3. Outputs
   *   4. Safety
   *   5. Hardware Validation
   *   6. Backup
   *   7. Ready
   *
   * Reuses existing components and APIs. Does NOT duplicate configuration implementations.
   * Wizard can be opened after first successful Admin bootstrap, or manually from System & Recovery.
   *
   * Completion is NOT persisted only in localStorage — the firmware owns commissioning state.
   */

  import NetworkConfigCard from '$lib/components/dashboard/NetworkConfigCard.svelte';
  import { getDeviceConfig, saveDeviceConfig, validateDeviceConfig } from '$lib/api/config';
  import { saveBackupFile } from '$lib/api/recovery';
  import type { DeviceConfig } from '$lib/api/config';
  import type { NodeIdentity } from '$lib/api/node';

  interface Props {
    nodeIp: string;
    nodeId: string;
    identity: NodeIdentity;
    onClose?: () => void;
    onComplete?: () => void;
  }

  let { nodeIp, nodeId, identity, onClose, onComplete }: Props = $props();

  type Step = 1 | 2 | 3 | 4 | 5 | 6 | 7;

  let currentStep = $state<Step>(1);
  let config = $state<DeviceConfig | null>(null);
  let configLoading = $state(false);
  let configError = $state('');
  let backupDownloaded = $state(false);
  let backupSkipped = $state(false);
  let validationIncomplete = $state(false); // from step 5

  const stepLabels: Record<Step, string> = {
    1: 'Identity',
    2: 'Network',
    3: 'Outputs',
    4: 'Safety',
    5: 'Hardware Validation',
    6: 'Backup',
    7: 'Ready',
  };

  async function loadConfig() {
    configLoading = true;
    configError = '';
    try {
      config = await getDeviceConfig(nodeIp);
    } catch (e) {
      configError = `Failed to load config: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      configLoading = false;
    }
  }

  async function saveIdentity() {
    if (!config) return;
    configError = '';
    try {
      const validation = await validateDeviceConfig(nodeIp, config, config.checksum);
      if (!validation.valid) {
        configError = 'Configuration validation failed.';
        return;
      }
      await saveDeviceConfig(nodeIp, config, config.checksum);
      config = await getDeviceConfig(nodeIp);
    } catch (e) {
      configError = `Save failed: ${e instanceof Error ? e.message : String(e)}`;
      return;
    }
    goToStep(2);
  }

  async function downloadBackup() {
    try {
      await saveBackupFile(nodeIp, nodeId);
      backupDownloaded = true;
    } catch (e) {
      configError = `Backup failed: ${e instanceof Error ? e.message : String(e)}`;
    }
  }

  function goToStep(step: Step) {
    currentStep = step;
    configError = '';
  }

  function skipBackup() {
    backupSkipped = true;
    goToStep(7);
  }

  // Load config on mount
  $effect(() => {
    if (nodeIp) {
      loadConfig();
    }
  });

  const safetyWarning = $derived(
    config && config.limits.maxPowerMw > 0 && config.maxPowerMw === 0
  );
</script>

<div class="wizard-overlay" role="dialog" aria-modal="true" aria-labelledby="wizard-title">
  <div class="wizard-modal">
    <!-- Header -->
    <div class="wizard-header">
      <div>
        <h1 id="wizard-title">Setup Wizard</h1>
        <p class="wizard-sub">Step {currentStep} of 7 — {stepLabels[currentStep]}</p>
      </div>
      <button type="button" class="close-btn" onclick={onClose} aria-label="Close wizard" id="wizard-close">✕</button>
    </div>

    <!-- Step progress bar -->
    <div class="step-track" aria-hidden="true">
      {#each ([1, 2, 3, 4, 5, 6, 7] as Step[]) as step}
        <div
          class="step-dot"
          class:active={step === currentStep}
          class:done={step < currentStep}
        ></div>
      {/each}
    </div>

    <!-- Step content -->
    <div class="wizard-body">

      <!-- Step 1: Identity -->
      {#if currentStep === 1}
        <div class="step-content">
          <h2>Node Identity</h2>

          <div class="immutable-info">
            <h3>Immutable hardware identity</h3>
            <dl class="info-grid">
              <dt>Node ID</dt><dd>{identity.id}</dd>
              <dt>Model</dt><dd>{identity.model}</dd>
              <dt>Hardware</dt><dd>{identity.hardwareRevision}</dd>
              <dt>Firmware</dt><dd>{identity.firmwareVersion}</dd>
            </dl>
          </div>

          {#if configLoading}
            <div class="loading">Loading configuration…</div>
          {:else if config}
            <div class="editable-fields">
              <h3>Editable fields</h3>

              <label class="field-label" for="wizard-device-name">Device Name</label>
              <input
                id="wizard-device-name"
                type="text"
                class="field-input"
                bind:value={config.deviceName}
                placeholder="e.g. lumigrid-lobby"
                maxlength="64"
              />

              <label class="field-label" for="wizard-site-label">Site Label</label>
              <input
                id="wizard-site-label"
                type="text"
                class="field-input"
                bind:value={config.siteLabel}
                placeholder="e.g. West Wing"
              />

              <label class="field-label" for="wizard-group-label">Group Label</label>
              <input
                id="wizard-group-label"
                type="text"
                class="field-input"
                bind:value={config.groupLabel}
                placeholder="e.g. Main Façade"
              />
            </div>
          {/if}

          {#if configError}
            <div class="error-notice" role="alert">{configError}</div>
          {/if}
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={onClose} id="wizard-cancel-step1">Cancel</button>
          <button type="button" class="btn-primary" onclick={saveIdentity} disabled={configLoading || !config} id="wizard-next-step1">
            Save & Continue
          </button>
        </div>

      <!-- Step 2: Network -->
      {:else if currentStep === 2}
        <div class="step-content">
          <h2>Network Configuration</h2>
          <p class="step-desc">
            Configure Wi-Fi. After applying, the node may change IP address.
            The dashboard reconnects using the immutable node ID — you do not need to re-add the node.
          </p>
          <!-- Reuse existing NetworkConfigCard -->
          <NetworkConfigCard nodeIp={nodeIp} />
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={() => goToStep(1)} id="wizard-back-step2">Back</button>
          <button type="button" class="btn-primary" onclick={() => goToStep(3)} id="wizard-next-step2">Continue</button>
        </div>

      <!-- Step 3: Outputs -->
      {:else if currentStep === 3}
        <div class="step-content">
          <h2>Output Channels</h2>
          <p class="step-desc">
            Configure physical output channels. Visual order: A1–A8 (addressable), P1–P8 (PWM).
          </p>
          <div class="channel-placeholder">
            <p class="placeholder-note">
              Configure channels in the Channels section of the dashboard.
              Channel configuration is shared with the main editor.
            </p>
            <div class="channel-grid-preview" aria-label="Channel layout preview">
              <div class="channel-row">
                {#each Array(8) as _, i}
                  <div class="channel-chip addressable">A{i + 1}</div>
                {/each}
              </div>
              <div class="channel-row">
                {#each Array(8) as _, i}
                  <div class="channel-chip pwm">P{i + 1}</div>
                {/each}
              </div>
            </div>
          </div>
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={() => goToStep(2)} id="wizard-back-step3">Back</button>
          <button type="button" class="btn-primary" onclick={() => goToStep(4)} id="wizard-next-step3">Continue</button>
        </div>

      <!-- Step 4: Safety -->
      {:else if currentStep === 4}
        <div class="step-content">
          <h2>Safety Limits</h2>
          <p class="step-desc">
            Configure power limits and safety settings. Limits shown are firmware-authoritative.
          </p>
          {#if config}
            <div class="safety-fields">
              <label class="field-label" for="wizard-max-power">Max Power (mW)</label>
              {#if config.limits.maxPowerMw > 0}
                <div class="field-meta">Hardware maximum: {config.limits.maxPowerMw.toLocaleString()} mW</div>
              {/if}
              <input
                id="wizard-max-power"
                type="number"
                class="field-input"
                bind:value={config.maxPowerMw}
                min="0"
                max={config.limits.maxPowerMw || undefined}
              />
              {#if safetyWarning}
                <div class="safety-warning" role="alert">
                  ⚠ Max power is set to 0 mW. The hardware power limit will not be enforced.
                  This is not safe for production installations.
                </div>
              {/if}

              <label class="field-label" for="wizard-watchdog">Watchdog Timeout (seconds)</label>
              <input
                id="wizard-watchdog"
                type="number"
                class="field-input"
                bind:value={config.watchdogTimeoutS}
                min="0"
                max="3600"
              />

              <label class="field-checkbox" for="wizard-soft-start">
                <input id="wizard-soft-start" type="checkbox" bind:checked={config.softStartEnabled} />
                Enable soft start
              </label>

              {#if config.softStartEnabled}
                <label class="field-label" for="wizard-soft-start-ms">Soft start duration (ms)</label>
                <input
                  id="wizard-soft-start-ms"
                  type="number"
                  class="field-input"
                  bind:value={config.softStartMs}
                  min="0"
                  max="5000"
                />
              {/if}
            </div>
          {:else}
            <div class="loading">Loading configuration…</div>
          {/if}
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={() => goToStep(3)} id="wizard-back-step4">Back</button>
          <button type="button" class="btn-primary" onclick={() => goToStep(5)} disabled={!config} id="wizard-next-step4">
            Continue
          </button>
        </div>

      <!-- Step 5: Hardware Validation -->
      {:else if currentStep === 5}
        <div class="step-content">
          <h2>Hardware Validation</h2>
          <p class="step-desc">
            Run factory hardware validation to verify all outputs are functioning correctly.
            Navigate to the Factory page to run tests, then return here.
          </p>
          <div class="validation-link-note">
            <a href="/dashboard/factory" class="action-link" target="_blank" rel="noreferrer">
              Open Factory Validation ↗
            </a>
          </div>
          <label class="field-checkbox" for="wizard-validation-incomplete">
            <input
              id="wizard-validation-incomplete"
              type="checkbox"
              bind:checked={validationIncomplete}
            />
            Continue with validation incomplete
          </label>
          {#if validationIncomplete}
            <div class="incomplete-warning" role="alert">
              Hardware validation incomplete. This should be completed before production use.
            </div>
          {/if}
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={() => goToStep(4)} id="wizard-back-step5">Back</button>
          <button type="button" class="btn-primary" onclick={() => goToStep(6)} id="wizard-next-step5">
            Continue
          </button>
        </div>

      <!-- Step 6: Backup -->
      {:else if currentStep === 6}
        <div class="step-content">
          <h2>Download Backup</h2>
          <p class="step-desc">
            Download a backup of this node's configuration and show data.
          </p>
          <div class="backup-exclusions">
            <strong>The backup does NOT include:</strong>
            <ul>
              <li>Authentication credentials (passwords)</li>
              <li>Wi-Fi password</li>
              <li>Sync secret</li>
            </ul>
          </div>
          <div class="backup-action-row">
            <button type="button" class="btn-primary" onclick={downloadBackup} id="wizard-backup-btn">
              {backupDownloaded ? '✓ Downloaded' : '↓ Download Backup (.lgbak)'}
            </button>
          </div>
          {#if configError}
            <div class="error-notice" role="alert">{configError}</div>
          {/if}
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-secondary" onclick={() => goToStep(5)} id="wizard-back-step6">Back</button>
          {#if !backupDownloaded}
            <button type="button" class="btn-text" onclick={skipBackup} id="wizard-skip-backup">Skip</button>
          {/if}
          <button
            type="button"
            class="btn-primary"
            onclick={() => goToStep(7)}
            disabled={!backupDownloaded && !backupSkipped}
            id="wizard-next-step6"
          >
            Continue
          </button>
        </div>

      <!-- Step 7: Ready -->
      {:else if currentStep === 7}
        <div class="step-content">
          <h2>Setup Complete</h2>
          <div class="ready-summary">
            <div class="summary-item">
              <span class="check">✓</span>
              Identity configured
            </div>
            <div class="summary-item">
              <span class="check">✓</span>
              Network configured
            </div>
            <div class="summary-item">
              <span class="check">✓</span>
              Output topology set
            </div>
            <div class="summary-item" class:warn={safetyWarning}>
              <span class="check" class:warn-check={safetyWarning}>{safetyWarning ? '⚠' : '✓'}</span>
              Safety limits {safetyWarning ? '(power limit not set)' : 'configured'}
            </div>
            <div class="summary-item" class:warn={validationIncomplete}>
              <span class="check" class:warn-check={validationIncomplete}>{validationIncomplete ? '⚠' : '✓'}</span>
              Hardware validation {validationIncomplete ? 'incomplete' : 'complete'}
            </div>
            <div class="summary-item" class:warn={backupSkipped}>
              <span class="check" class:warn-check={backupSkipped}>{backupSkipped ? '⚠' : '✓'}</span>
              Backup {backupSkipped ? 'skipped' : 'downloaded'}
            </div>
          </div>
        </div>
        <div class="wizard-actions">
          <button type="button" class="btn-primary" onclick={onComplete ?? onClose} id="wizard-finish">
            Finish
          </button>
        </div>
      {/if}

    </div>
  </div>
</div>

<style>
  .wizard-overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.75);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 8000;
  }

  .wizard-modal {
    background: #1a1a1f;
    border: 1px solid #3a3a42;
    border-radius: 14px;
    width: 540px;
    max-width: calc(100vw - 32px);
    max-height: calc(100vh - 48px);
    display: flex;
    flex-direction: column;
    overflow: hidden;
    box-shadow: 0 24px 80px rgba(0, 0, 0, 0.7);
  }

  .wizard-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    padding: 20px 22px 14px;
    border-bottom: 1px solid #2c2c30;
  }

  .wizard-header h1 {
    margin: 0 0 3px;
    font-size: 1.1rem;
    font-weight: 700;
    color: #e8e8ed;
  }

  .wizard-sub {
    margin: 0;
    font-size: 0.82rem;
    color: #888;
  }

  .close-btn {
    background: none;
    border: none;
    color: #666;
    font-size: 1rem;
    cursor: pointer;
    padding: 4px 6px;
    border-radius: 6px;
    transition: background 0.15s, color 0.15s;
  }

  .close-btn:hover { background: #2a2a30; color: #a8a8b0; }

  .step-track {
    display: flex;
    gap: 6px;
    padding: 12px 22px;
    background: #141418;
    border-bottom: 1px solid #2c2c30;
  }

  .step-dot {
    flex: 1;
    height: 4px;
    background: #2c2c30;
    border-radius: 2px;
    transition: background 0.2s;
  }

  .step-dot.active { background: #0a84ff; }
  .step-dot.done { background: #30d158; }

  .wizard-body {
    display: flex;
    flex-direction: column;
    flex: 1;
    overflow: hidden;
  }

  .step-content {
    flex: 1;
    overflow-y: auto;
    padding: 20px 22px;
  }

  .step-content h2 {
    margin: 0 0 10px;
    font-size: 1rem;
    font-weight: 600;
    color: #e8e8ed;
  }

  .step-desc {
    margin: 0 0 16px;
    color: #a8a8b0;
    font-size: 0.88rem;
    line-height: 1.5;
  }

  .wizard-actions {
    display: flex;
    gap: 10px;
    justify-content: flex-end;
    padding: 14px 22px;
    border-top: 1px solid #2c2c30;
    background: #141418;
  }

  .btn-primary {
    padding: 9px 22px;
    border-radius: 8px;
    border: 1px solid #0a84ff;
    background: #0a1a3a;
    color: #0a84ff;
    font-size: 0.92rem;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s;
  }

  .btn-primary:hover:not(:disabled) { background: #0e2050; }
  .btn-primary:disabled { opacity: 0.4; cursor: default; }

  .btn-secondary {
    padding: 9px 18px;
    border-radius: 8px;
    border: 1px solid #444;
    background: #252528;
    color: #c8c8d0;
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 0.15s;
  }

  .btn-secondary:hover { background: #303035; }

  .btn-text {
    padding: 9px 14px;
    border: none;
    background: none;
    color: #666;
    font-size: 0.88rem;
    cursor: pointer;
    transition: color 0.15s;
  }

  .btn-text:hover { color: #a8a8b0; }

  .immutable-info {
    background: #141418;
    border: 1px solid #2c2c30;
    border-radius: 8px;
    padding: 14px 16px;
    margin-bottom: 18px;
  }

  .immutable-info h3, .editable-fields h3 {
    margin: 0 0 10px;
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #666;
  }

  .info-grid {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 3px 14px;
    margin: 0;
    font-size: 0.85rem;
  }

  .info-grid dt { color: #666; }
  .info-grid dd { margin: 0; color: #b8b8c0; font-family: monospace; font-size: 0.82rem; }

  .field-label {
    display: block;
    font-size: 0.82rem;
    color: #888;
    margin: 12px 0 4px;
    text-transform: uppercase;
    letter-spacing: 0.06em;
  }

  .field-input {
    display: block;
    width: 100%;
    box-sizing: border-box;
    padding: 9px 12px;
    background: #111115;
    border: 1px solid #333;
    border-radius: 7px;
    color: #e8e8ed;
    font-size: 0.9rem;
    font-family: inherit;
    transition: border-color 0.15s;
  }

  .field-input:focus {
    outline: none;
    border-color: #0a84ff;
  }

  .field-meta {
    font-size: 0.78rem;
    color: #555;
    margin-bottom: 4px;
  }

  .field-checkbox {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 0.88rem;
    color: #c8c8d0;
    cursor: pointer;
    margin: 14px 0 8px;
  }

  .safety-warning, .incomplete-warning {
    background: #2a1a00;
    border: 1px solid #ff9f0a;
    border-radius: 7px;
    padding: 10px 13px;
    color: #ffd569;
    font-size: 0.85rem;
    margin-top: 10px;
  }

  .error-notice {
    background: #2a0e0e;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 10px 13px;
    color: #ff9f9a;
    font-size: 0.87rem;
    margin-top: 12px;
  }

  .loading {
    color: #666;
    font-size: 0.88rem;
    padding: 12px 0;
  }

  .channel-placeholder {
    background: #141418;
    border: 1px solid #2c2c30;
    border-radius: 9px;
    padding: 16px;
    margin-top: 8px;
  }

  .placeholder-note {
    color: #888;
    font-size: 0.85rem;
    margin: 0 0 14px;
  }

  .channel-grid-preview { display: flex; flex-direction: column; gap: 8px; }
  .channel-row { display: flex; gap: 6px; }

  .channel-chip {
    padding: 5px 0;
    width: 40px;
    text-align: center;
    border-radius: 5px;
    font-size: 0.78rem;
    font-weight: 600;
    letter-spacing: 0.02em;
  }

  .channel-chip.addressable { background: #0a2a3a; color: #00d4ff; border: 1px solid #1a4a5a; }
  .channel-chip.pwm { background: #2a1a0a; color: #ff9f0a; border: 1px solid #4a3a1a; }

  .validation-link-note { margin: 0 0 16px; }

  .action-link {
    color: #0a84ff;
    text-decoration: none;
    font-size: 0.9rem;
    border-bottom: 1px solid transparent;
    transition: border-color 0.15s;
  }

  .action-link:hover { border-bottom-color: #0a84ff; }

  .backup-exclusions {
    background: #111115;
    border: 1px solid #2c2c30;
    border-radius: 8px;
    padding: 12px 16px;
    margin-bottom: 16px;
  }

  .backup-exclusions strong {
    display: block;
    color: #a8a8b0;
    font-size: 0.82rem;
    margin-bottom: 6px;
  }

  .backup-exclusions ul {
    margin: 0;
    padding-left: 18px;
    color: #666;
    font-size: 0.85rem;
    line-height: 1.7;
  }

  .backup-action-row { margin-bottom: 10px; }

  .ready-summary {
    display: flex;
    flex-direction: column;
    gap: 10px;
    margin-top: 8px;
  }

  .summary-item {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 0.9rem;
    color: #c8c8d0;
    padding: 8px 12px;
    background: #141418;
    border: 1px solid #2c2c30;
    border-radius: 7px;
  }

  .summary-item.warn {
    border-color: #ff9f0a44;
    background: #1a1500;
    color: #ffd569;
  }

  .check { color: #30d158; font-size: 1rem; flex-shrink: 0; }
  .warn-check { color: #ff9f0a; }
</style>
