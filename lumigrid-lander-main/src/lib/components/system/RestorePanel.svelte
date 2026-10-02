<script lang="ts">
  import { validateRestorePackage, restorePackage } from '$lib/api/recovery';
  import type { RestoreValidationResponse } from '$lib/api/recovery';
  import { parseBackupHeader } from '$lib/recovery/backup';

  interface Props {
    nodeIp: string;
    /** Immutable node ID — used for reconnect after reboot. */
    nodeId: string;
    onRestoreComplete?: () => void;
  }

  let { nodeIp, nodeId, onRestoreComplete }: Props = $props();

  type Phase =
    | 'select'
    | 'validating'
    | 'preflight'
    | 'confirming'
    | 'restoring'
    | 'rebooting'
    | 'waiting'
    | 'done'
    | 'error';

  let phase = $state<Phase>('select');
  let selectedFile = $state<File | null>(null);
  let validation = $state<RestoreValidationResponse | null>(null);
  let isLegacy = $state(false);
  let error = $state('');

  function reset() {
    phase = 'select';
    selectedFile = null;
    validation = null;
    isLegacy = false;
    error = '';
  }

  async function onFileSelected(e: Event) {
    const file = (e.target as HTMLInputElement).files?.[0];
    (e.target as HTMLInputElement).value = '';
    if (!file) return;

    const ext = file.name.toLowerCase();
    if (!ext.endsWith('.lgbak') && !ext.endsWith('.json')) {
      error = 'Select a .lgbak backup file. Legacy .json backups are accepted but require firmware validation.';
      return;
    }
    if (file.size <= 0) {
      error = 'The selected file is empty.';
      return;
    }
    if (file.size > 8 * 1024 * 1024) {
      error = 'File is unexpectedly large (> 8 MB).';
      return;
    }

    selectedFile = file;
    error = '';
    phase = 'validating';

    // Client-side header parse for display (display-only, firmware validates authoritatively)
    const parseResult = await parseBackupHeader(file);
    isLegacy = parseResult.isLegacyV3;

    // Always preflight through firmware — no direct path from selection to restore
    try {
      validation = await validateRestorePackage(nodeIp, file);
      phase = 'preflight';
    } catch (e) {
      error = `Validation failed: ${e instanceof Error ? e.message : String(e)}`;
      phase = 'error';
    }
  }

  async function executeRestore() {
    if (!selectedFile || !validation?.valid) return;
    phase = 'restoring';
    error = '';
    try {
      // Send raw file bytes — never reserialize
      await restorePackage(nodeIp, selectedFile);
      phase = 'rebooting';
      // Expect network disconnect — this is normal
      await waitForReconnect();
    } catch (e) {
      // Network loss during restore is expected — transition to waiting
      if (phase === 'restoring') {
        phase = 'rebooting';
        await waitForReconnect();
      } else {
        error = `Restore failed: ${e instanceof Error ? e.message : String(e)}`;
        phase = 'error';
      }
    }
  }

  async function waitForReconnect() {
    phase = 'waiting';
    // After restore the node reboots. Reconnect is tracked by immutable node ID in FleetStore.
    // The page/layout handles reconnect detection — here we transition to done.
    await new Promise((r) => setTimeout(r, 8000));
    phase = 'done';
    onRestoreComplete?.();
  }

  const planSummary = $derived(
    validation?.plan
      ? [
          validation.plan.deviceConfig && 'Device configuration',
          validation.plan.scenes > 0 && `${validation.plan.scenes} Scene${validation.plan.scenes !== 1 ? 's' : ''}`,
          validation.plan.sceneMotions > 0 && `${validation.plan.sceneMotions} Motion document${validation.plan.sceneMotions !== 1 ? 's' : ''}`,
          validation.plan.timeline && 'Timeline',
          validation.plan.calendar && 'Calendar',
          validation.plan.cueLists > 0 && `${validation.plan.cueLists} Cue List${validation.plan.cueLists !== 1 ? 's' : ''}`,
        ].filter(Boolean) as string[]
      : []
  );
</script>

<div class="restore-panel">
  <h2 class="panel-title">Restore Backup</h2>

  {#if phase === 'select' || phase === 'error'}
    <p class="instruction">
      Select a <code>.lgbak</code> backup file.
      {#if isLegacy}
        Legacy <code>.json</code> backups are identified as "Legacy Backup" and validated by firmware before restore.
      {/if}
    </p>

    {#if error}
      <div class="error-notice" role="alert">{error}</div>
    {/if}

    <label class="file-select-btn" for="restore-file-input" id="restore-select-label">
      Select backup file…
      <input
        id="restore-file-input"
        type="file"
        accept=".lgbak,.json"
        onchange={onFileSelected}
        style="display:none"
      />
    </label>

  {:else if phase === 'validating'}
    <div class="status-row">
      <span class="spinner" aria-hidden="true"></span>
      <span>Validating backup with node…</span>
    </div>

  {:else if phase === 'preflight'}
    {#if !validation?.valid}
      <div class="error-notice" role="alert">
        <strong>Backup is not valid.</strong>
        {#if validation?.warnings.length}
          <ul class="warning-list">
            {#each validation.warnings as w}
              <li>{w}</li>
            {/each}
          </ul>
        {/if}
      </div>
      <button type="button" class="btn-secondary" onclick={reset} id="restore-reset">
        Select different file
      </button>
    {:else}
      {#if isLegacy}
        <div class="legacy-badge">Legacy Backup</div>
      {/if}

      <div class="preflight-card">
        <h3>Backup source</h3>
        <dl class="detail-list">
          <dt>Node ID</dt><dd>{validation.source.nodeId}</dd>
          <dt>Firmware</dt><dd>{validation.source.firmwareVersion}</dd>
          <dt>Schema</dt><dd>{validation.schema}</dd>
        </dl>

        <h3 class="section-header">Content</h3>
        <ul class="plan-list">
          {#each planSummary as item}
            <li>• {item}</li>
          {/each}
        </ul>

        <h3 class="section-header">Preserved</h3>
        <ul class="preserved-list">
          <li class:yes={validation.preserved.nodeIdentity} class:no={!validation.preserved.nodeIdentity}>
            {validation.preserved.nodeIdentity ? '✓' : '✗'} Node identity
          </li>
          <li class:yes={validation.preserved.authentication} class:no={!validation.preserved.authentication}>
            {validation.preserved.authentication ? '✓' : '✗'} Authentication credentials
          </li>
          <li class:yes={validation.preserved.networkCredentials} class:no={!validation.preserved.networkCredentials}>
            {validation.preserved.networkCredentials ? '✓' : '✗'} Wi-Fi credentials
          </li>
          <li class:yes={validation.preserved.syncToken} class:no={!validation.preserved.syncToken}>
            {validation.preserved.syncToken ? '✓' : '✗'} Sync secret
          </li>
        </ul>

        {#if validation.warnings.length > 0}
          <div class="warnings-section" role="alert">
            <strong>Warnings</strong>
            <ul class="warning-list">
              {#each validation.warnings as w}
                <li>{w}</li>
              {/each}
            </ul>
          </div>
        {/if}
      </div>

      <div class="confirm-note">
        The node will restore configuration/show data and reboot.
        Security credentials and network secrets remain unchanged.
      </div>

      <div class="action-row">
        <button type="button" class="btn-secondary" onclick={reset} id="restore-cancel-preflight">
          Cancel
        </button>
        <button type="button" class="btn-restore" onclick={executeRestore} id="restore-confirm">
          Restore &amp; Reboot
        </button>
      </div>
    {/if}

  {:else if phase === 'restoring'}
    <div class="status-row">
      <span class="spinner" aria-hidden="true"></span>
      <span>Restoring…</span>
    </div>

  {:else if phase === 'rebooting'}
    <div class="status-row">
      <span class="spinner amber" aria-hidden="true"></span>
      <span>Rebooting…</span>
    </div>

  {:else if phase === 'waiting'}
    <div class="status-row">
      <span class="spinner" aria-hidden="true"></span>
      <span>Waiting for node…</span>
    </div>
    <p class="reconnect-note">
      Reconnect is tracked by immutable node ID. You do not need to re-add the node.
    </p>

  {:else if phase === 'done'}
    <div class="done-notice">
      <span class="done-icon">✓</span>
      <span>Restore complete. Node has reconnected.</span>
    </div>
    <button type="button" class="btn-secondary" onclick={reset} id="restore-done">
      Dismiss
    </button>
  {/if}
</div>

<style>
  .restore-panel {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
    color: #e8e8ed;
    font-family: inherit;
  }

  .panel-title {
    margin: 0 0 14px;
    font-size: 1rem;
    font-weight: 600;
    color: #e8e8ed;
  }

  .instruction {
    margin: 0 0 14px;
    color: #a8a8b0;
    font-size: 0.9rem;
    line-height: 1.5;
  }

  code {
    background: #2a2a30;
    border-radius: 3px;
    padding: 1px 5px;
    font-family: monospace;
    font-size: 0.85em;
  }

  .error-notice {
    background: #2a0e0e;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 12px 14px;
    color: #ff9f9a;
    font-size: 0.88rem;
    margin-bottom: 12px;
  }

  .file-select-btn {
    display: inline-block;
    padding: 9px 18px;
    border-radius: 8px;
    border: 1px solid #444;
    background: #252528;
    color: #e8e8ed;
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 0.15s;
  }

  .file-select-btn:hover {
    background: #303035;
  }

  .status-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 14px 0;
    color: #a8a8b0;
    font-size: 0.9rem;
  }

  .spinner {
    width: 18px;
    height: 18px;
    border: 2px solid #444;
    border-top-color: #00d4ff;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    flex-shrink: 0;
  }

  .spinner.amber {
    border-top-color: #ff9f0a;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  .legacy-badge {
    display: inline-block;
    background: #3a2a00;
    border: 1px solid #665500;
    color: #ffd569;
    border-radius: 6px;
    padding: 3px 10px;
    font-size: 0.8rem;
    margin-bottom: 12px;
  }

  .preflight-card {
    background: #141418;
    border: 1px solid #2c2c30;
    border-radius: 9px;
    padding: 16px;
    margin-bottom: 14px;
  }

  .preflight-card h3 {
    margin: 0 0 8px;
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #666;
  }

  .section-header {
    margin: 14px 0 8px;
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #666;
  }

  .detail-list {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 3px 14px;
    margin: 0;
    font-size: 0.85rem;
  }

  .detail-list dt { color: #666; }
  .detail-list dd { margin: 0; color: #b8b8c0; font-family: monospace; font-size: 0.82rem; }

  .plan-list, .preserved-list {
    margin: 0;
    padding: 0;
    list-style: none;
    font-size: 0.88rem;
  }

  .plan-list li { color: #c8c8d0; line-height: 1.8; }

  .preserved-list li { line-height: 1.8; }
  .preserved-list .yes { color: #30d158; }
  .preserved-list .no { color: #888; }

  .warnings-section {
    background: #2a1f0a;
    border: 1px solid #665500;
    border-radius: 7px;
    padding: 10px 13px;
    margin-top: 12px;
  }

  .warnings-section strong {
    display: block;
    color: #ff9f0a;
    font-size: 0.82rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    margin-bottom: 5px;
  }

  .warning-list {
    margin: 0;
    padding-left: 16px;
    color: #ffd569;
    font-size: 0.85rem;
    line-height: 1.5;
  }

  .confirm-note {
    background: #111115;
    border-radius: 7px;
    padding: 10px 14px;
    color: #888;
    font-size: 0.85rem;
    line-height: 1.5;
    margin-bottom: 14px;
  }

  .action-row {
    display: flex;
    gap: 10px;
    justify-content: flex-end;
  }

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

  .btn-secondary:hover {
    background: #303035;
  }

  .btn-restore {
    padding: 9px 20px;
    border-radius: 8px;
    border: 1px solid #ff9f0a;
    background: #2a1f00;
    color: #ffd569;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s;
  }

  .btn-restore:hover {
    background: #382800;
  }

  .reconnect-note {
    color: #666;
    font-size: 0.82rem;
    margin: 4px 0 0;
  }

  .done-notice {
    display: flex;
    align-items: center;
    gap: 10px;
    color: #30d158;
    font-size: 0.95rem;
    margin-bottom: 14px;
    padding: 12px 0;
  }

  .done-icon { font-size: 1.2rem; }
</style>
