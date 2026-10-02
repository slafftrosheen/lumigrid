<script lang="ts">
  import {
    getDeviceConfig,
    validateDeviceConfig,
    saveDeviceConfig,
    ConfigConflictError,
    type DeviceConfig,
    type ConfigValidationResponse,
  } from '$lib/api/config';
  import ConfigImpactDialog from './ConfigImpactDialog.svelte';

  interface Props {
    nodeIp: string;
    onSaved?: () => void;
  }

  let { nodeIp, onSaved }: Props = $props();

  let loading = $state(false);
  let persisted = $state<DeviceConfig | null>(null);
  let working = $state<DeviceConfig | null>(null);

  let validating = $state(false);
  let saving = $state(false);
  let validation = $state<ConfigValidationResponse | null>(null);
  let showImpactDialog = $state(false);

  let conflict = $state(false);
  let conflictChecksum = $state(0);
  let error = $state('');
  let successMsg = $state('');

  // Write-only sync token helper state
  let newSyncTokenInput = $state('');
  let clearTokenRequested = $state(false);

  export async function reload() {
    if (!nodeIp) return;
    loading = true;
    error = '';
    conflict = false;
    try {
      const cfg = await getDeviceConfig(nodeIp);
      persisted = cfg;
      // Deep clone working copy
      working = JSON.parse(JSON.stringify(cfg));
      newSyncTokenInput = '';
      clearTokenRequested = false;
    } catch (e) {
      error = `Failed to load device configuration: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    if (nodeIp) {
      reload();
    }
  });

  const isDirty = $derived(() => {
    if (!persisted || !working) return false;
    if (newSyncTokenInput.trim() !== '' || clearTokenRequested) return true;
    return (
      working.deviceName !== persisted.deviceName ||
      working.siteLabel !== persisted.siteLabel ||
      working.groupLabel !== persisted.groupLabel ||
      working.serialNumber !== persisted.serialNumber ||
      working.maxPowerMw !== persisted.maxPowerMw ||
      working.watchdogTimeoutS !== persisted.watchdogTimeoutS ||
      working.gammaX10 !== persisted.gammaX10 ||
      working.softStartEnabled !== persisted.softStartEnabled ||
      working.softStartMs !== persisted.softStartMs ||
      working.timezone !== persisted.timezone ||
      working.mqttEnabled !== persisted.mqttEnabled ||
      working.mqttBroker !== persisted.mqttBroker ||
      working.mqttPort !== persisted.mqttPort ||
      working.syncEnabled !== persisted.syncEnabled ||
      working.syncRole !== persisted.syncRole ||
      working.activePresetIndex !== persisted.activePresetIndex ||
      working.fallbackPresetIndex !== persisted.fallbackPresetIndex
    );
  });

  async function handleReviewChanges() {
    if (!working || !persisted) return;
    validating = true;
    error = '';
    conflict = false;

    // Apply token updates to working before validate
    if (clearTokenRequested) {
      working.clearSyncToken = true;
      working.pendingSyncToken = undefined;
    } else if (newSyncTokenInput.trim() !== '') {
      const num = parseInt(newSyncTokenInput.trim(), 10);
      if (!isNaN(num) && num > 0) {
        working.pendingSyncToken = num;
        working.clearSyncToken = false;
      }
    } else {
      working.clearSyncToken = false;
      working.pendingSyncToken = undefined;
    }

    try {
      const res = await validateDeviceConfig(nodeIp, working, persisted.checksum);
      validation = res;
      if (!res.valid) {
        error = `Validation rejected by node: ${res.warnings.join('; ')}`;
        return;
      }
      showImpactDialog = true;
    } catch (e) {
      error = `Preflight failed: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      validating = false;
    }
  }

  async function handleApply() {
    if (!working || !persisted) return;
    saving = true;
    error = '';
    try {
      const saveRes = await saveDeviceConfig(nodeIp, working, persisted.checksum);
      showImpactDialog = false;
      successMsg = `Configuration saved (checksum: 0x${saveRes.checksum.toString(16).toUpperCase()})`;
      setTimeout(() => { if (successMsg) successMsg = ''; }, 4000);
      await reload();
      onSaved?.();
    } catch (e) {
      showImpactDialog = false;
      if (e instanceof ConfigConflictError) {
        conflict = true;
        conflictChecksum = e.currentChecksum;
      } else {
        error = e instanceof Error ? e.message : String(e);
      }
    } finally {
      saving = false;
    }
  }

  function handleCancelImpact() {
    showImpactDialog = false;
  }

  function handleResetChanges() {
    if (persisted) {
      working = JSON.parse(JSON.stringify(persisted));
      newSyncTokenInput = '';
      clearTokenRequested = false;
      conflict = false;
      error = '';
    }
  }
</script>

<div class="config-panel" id="device-config-panel">
  <div class="panel-header">
    <div class="title-group">
      <h2>Device Configuration</h2>
      {#if persisted}
        <span class="checksum-badge" title="CRC32 Checksum">
          CRC: 0x{persisted.checksum.toString(16).toUpperCase()}
        </span>
      {/if}
    </div>
    <div class="header-actions">
      <button
        type="button"
        class="btn-secondary"
        onclick={reload}
        disabled={loading || saving || validating}
        id="config-reload-btn"
      >
        ⟳ Reload
      </button>
    </div>
  </div>

  {#if loading}
    <div class="loading-state">Loading configuration from node…</div>
  {:else if conflict}
    <!-- Dedicated Conflict State (Requirement 11) -->
    <div class="conflict-banner" role="alert" id="config-conflict-alert">
      <div class="conflict-header">
        <span class="conflict-icon">⚠</span>
        <strong>Configuration Conflict Detected</strong>
      </div>
      <p class="conflict-desc">
        Configuration changed on the node since you opened it (Node Checksum: 0x{conflictChecksum.toString(16).toUpperCase()}).
      </p>
      <div class="conflict-actions">
        <button
          type="button"
          class="btn-primary"
          onclick={reload}
          id="config-conflict-reload"
        >
          Reload Node Version
        </button>
        <button
          type="button"
          class="btn-secondary"
          onclick={() => { conflict = false; }}
          id="config-conflict-review"
        >
          Review My Changes
        </button>
      </div>
    </div>
  {:else if working}
    {#if error}
      <div class="error-banner" role="alert">{error}</div>
    {/if}
    {#if successMsg}
      <div class="success-banner" role="status">{successMsg}</div>
    {/if}

    <!-- Authoritative Hardware Limits Section -->
    <div class="limits-card">
      <span class="limits-title">Hardware Limits (Firmware Authoritative):</span>
      <div class="limits-grid">
        <div class="limit-item">
          <span class="limit-label">Max Power:</span>
          <span class="limit-val">{working.limits.maxPowerMw} mW</span>
        </div>
        <div class="limit-item">
          <span class="limit-label">Max Pixels/Channel:</span>
          <span class="limit-val">{working.limits.maxPixelsPerChannel}</span>
        </div>
        <div class="limit-item">
          <span class="limit-label">Max Groups:</span>
          <span class="limit-val">{working.limits.maxGroups}</span>
        </div>
        <div class="limit-item">
          <span class="limit-label">Max Channels:</span>
          <span class="limit-val">{working.limits.maxChannels}</span>
        </div>
        <div class="limit-item">
          <span class="limit-label">Max DMX Universe:</span>
          <span class="limit-val">{working.limits.maxDmxUniverse}</span>
        </div>
      </div>
    </div>

    <form class="config-form" onsubmit={(e) => { e.preventDefault(); handleReviewChanges(); }}>
      <!-- Identity & Location -->
      <fieldset class="form-section">
        <legend>Identity & Labels</legend>
        <div class="field-row">
          <label>
            <span>Device Name</span>
            <input type="text" maxlength="31" bind:value={working.deviceName} required id="cfg-device-name" />
          </label>
          <label>
            <span>Site Label</span>
            <input type="text" maxlength="31" bind:value={working.siteLabel} id="cfg-site-label" />
          </label>
          <label>
            <span>Group Label</span>
            <input type="text" maxlength="31" bind:value={working.groupLabel} id="cfg-group-label" />
          </label>
        </div>
      </fieldset>

      <!-- Safety & Power -->
      <fieldset class="form-section">
        <legend>Power & Safety Limits</legend>
        <div class="field-row">
          <label>
            <span>Max Power Budget (mW)</span>
            <input
              type="number"
              min="0"
              max={working.limits.maxPowerMw || 200000}
              bind:value={working.maxPowerMw}
              id="cfg-max-power"
            />
            {#if working.limits.maxPowerMw > 0 && working.maxPowerMw === 0}
              <span class="field-warn">⚠ Power limit is 0 mW (safety limits unconfigured)</span>
            {/if}
          </label>
          <label>
            <span>Watchdog Timeout (seconds)</span>
            <input type="number" min="1" max="3600" bind:value={working.watchdogTimeoutS} id="cfg-watchdog" />
          </label>
          <label>
            <span>Gamma Correction (×10)</span>
            <input type="number" min="10" max="40" bind:value={working.gammaX10} id="cfg-gamma" />
          </label>
        </div>

        <div class="field-row checkbox-row">
          <label class="checkbox-label">
            <input type="checkbox" bind:checked={working.softStartEnabled} id="cfg-soft-start-enabled" />
            <span>Enable Output Soft Start</span>
          </label>
          {#if working.softStartEnabled}
            <label>
              <span>Duration (ms)</span>
              <input type="number" min="50" max="5000" bind:value={working.softStartMs} id="cfg-soft-start-ms" />
            </label>
          {/if}
        </div>
      </fieldset>

      <!-- Sync & Security -->
      <fieldset class="form-section">
        <legend>Cluster Synchronization & Secrets</legend>
        <div class="field-row">
          <label class="checkbox-label">
            <input type="checkbox" bind:checked={working.syncEnabled} id="cfg-sync-enabled" />
            <span>Enable NodeSync Protocol</span>
          </label>
          <label>
            <span>Sync Role</span>
            <select bind:value={working.syncRole} id="cfg-sync-role">
              <option value={0}>Standalone (Follower / Listener)</option>
              <option value={1}>Leader (Clock Master)</option>
            </select>
          </label>
        </div>

        <!-- Write-Only Sync Token Handling (Requirement 5) -->
        <div class="token-management">
          <span class="token-status-label">
            Current Status:
            {#if working.syncTokenSet && !clearTokenRequested}
              <span class="token-set-badge">✓ Sync Secret Configured</span>
            {:else}
              <span class="token-none-badge">No Sync Secret Set</span>
            {/if}
          </span>

          <div class="token-inputs">
            <label>
              <span>{working.syncTokenSet ? 'Replace Sync Secret (Write-Only)' : 'Set Sync Secret (Write-Only)'}</span>
              <input
                type="password"
                placeholder="Enter new 32-bit token..."
                bind:value={newSyncTokenInput}
                disabled={clearTokenRequested}
                id="cfg-new-sync-token"
              />
            </label>
            {#if working.syncTokenSet}
              <button
                type="button"
                class="btn-clear-token"
                class:active={clearTokenRequested}
                onclick={() => { clearTokenRequested = !clearTokenRequested; if (clearTokenRequested) newSyncTokenInput = ''; }}
                id="cfg-clear-token-btn"
              >
                {clearTokenRequested ? 'Cancel Clear' : 'Clear Secret'}
              </button>
            {/if}
          </div>
          <span class="token-hint">
            The stored secret is never echoed back by the node. Leaving this blank preserves the existing secret.
          </span>
        </div>
      </fieldset>

      <!-- Form Actions -->
      <div class="form-actions">
        <button
          type="button"
          class="btn-secondary"
          onclick={handleResetChanges}
          disabled={!isDirty() || saving || validating}
          id="config-discard-btn"
        >
          Discard Changes
        </button>
        <button
          type="submit"
          class="btn-primary"
          disabled={!isDirty() || saving || validating}
          id="config-save-btn"
        >
          {#if validating}
            Preflighting…
          {:else}
            Review Changes & Apply
          {/if}
        </button>
      </div>
    </form>
  {/if}
</div>

{#if showImpactDialog && validation}
  <ConfigImpactDialog
    impact={validation.impact}
    warnings={validation.warnings}
    applying={saving}
    onApply={handleApply}
    onCancel={handleCancelImpact}
  />
{/if}

<style>
  .config-panel {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
    color: #e8e8ed;
    font-family: inherit;
  }

  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;
    border-bottom: 1px solid #282830;
    padding-bottom: 12px;
  }

  .title-group {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .title-group h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 600;
  }

  .checksum-badge {
    background: #252530;
    color: #a8a8b8;
    border: 1px solid #3c3c4c;
    border-radius: 4px;
    padding: 2px 8px;
    font-family: monospace;
    font-size: 0.8rem;
  }

  .limits-card {
    background: #141419;
    border: 1px solid #2a2a35;
    border-radius: 6px;
    padding: 10px 14px;
    margin-bottom: 18px;
  }

  .limits-title {
    font-size: 0.75rem;
    text-transform: uppercase;
    color: #888898;
    letter-spacing: 0.05em;
    font-weight: 600;
    display: block;
    margin-bottom: 6px;
  }

  .limits-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
  }

  .limit-item {
    font-size: 0.82rem;
  }

  .limit-label {
    color: #a0a0b0;
    margin-right: 4px;
  }

  .limit-val {
    font-family: monospace;
    color: #4cd964;
    font-weight: 600;
  }

  .form-section {
    border: 1px solid #2a2a35;
    border-radius: 8px;
    padding: 14px 16px;
    margin-bottom: 18px;
  }

  .form-section legend {
    font-size: 0.88rem;
    font-weight: 600;
    color: #c0c0d0;
    padding: 0 6px;
  }

  .field-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 14px;
    margin-bottom: 12px;
  }

  .checkbox-row {
    display: flex;
    align-items: center;
    gap: 20px;
  }

  label {
    display: flex;
    flex-direction: column;
    gap: 4px;
    font-size: 0.84rem;
    color: #b0b0be;
  }

  .checkbox-label {
    flex-direction: row;
    align-items: center;
    gap: 8px;
    cursor: pointer;
  }

  input[type="text"],
  input[type="number"],
  input[type="password"],
  select {
    background: #111116;
    border: 1px solid #3c3c4a;
    border-radius: 5px;
    color: #eee;
    padding: 7px 10px;
    font-size: 0.88rem;
    font-family: inherit;
  }

  input:focus, select:focus {
    outline: none;
    border-color: #5e81f4;
  }

  .field-warn {
    font-size: 0.75rem;
    color: #ff9f0a;
    margin-top: 2px;
  }

  .token-management {
    background: #121217;
    border: 1px solid #262632;
    border-radius: 6px;
    padding: 12px;
    margin-top: 10px;
  }

  .token-status-label {
    font-size: 0.84rem;
    display: block;
    margin-bottom: 8px;
  }

  .token-set-badge {
    color: #4cd964;
    font-weight: 600;
    margin-left: 6px;
  }

  .token-none-badge {
    color: #888898;
    margin-left: 6px;
  }

  .token-inputs {
    display: flex;
    gap: 10px;
    align-items: flex-end;
  }

  .token-inputs label {
    flex: 1;
  }

  .btn-clear-token {
    background: #2a2020;
    border: 1px solid #ff453a;
    color: #ff453a;
    border-radius: 5px;
    padding: 7px 12px;
    font-size: 0.82rem;
    cursor: pointer;
  }

  .btn-clear-token.active {
    background: #ff453a;
    color: #fff;
  }

  .token-hint {
    font-size: 0.75rem;
    color: #707080;
    display: block;
    margin-top: 6px;
  }

  .form-actions {
    display: flex;
    justify-content: flex-end;
    gap: 12px;
    margin-top: 20px;
  }

  .btn-primary {
    background: #5e81f4;
    color: #fff;
    border: none;
    border-radius: 6px;
    padding: 8px 18px;
    font-weight: 600;
    cursor: pointer;
  }

  .btn-primary:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  .btn-secondary {
    background: #282835;
    color: #d0d0e0;
    border: 1px solid #3c3c4c;
    border-radius: 6px;
    padding: 8px 16px;
    cursor: pointer;
  }

  .btn-secondary:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .conflict-banner {
    background: #351a1a;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 16px;
    margin-bottom: 16px;
  }

  .conflict-header {
    display: flex;
    align-items: center;
    gap: 10px;
    color: #ff453a;
    font-size: 1rem;
    margin-bottom: 6px;
  }

  .conflict-desc {
    color: #ffb0b0;
    font-size: 0.88rem;
    margin: 0 0 12px;
  }

  .conflict-actions {
    display: flex;
    gap: 10px;
  }

  .error-banner {
    background: #301515;
    border: 1px solid #ff453a;
    color: #ff9f9a;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.88rem;
    margin-bottom: 14px;
  }

  .success-banner {
    background: #152b18;
    border: 1px solid #4cd964;
    color: #a8eec0;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.88rem;
    margin-bottom: 14px;
  }

  .loading-state {
    padding: 30px;
    text-align: center;
    color: #888898;
  }
</style>
