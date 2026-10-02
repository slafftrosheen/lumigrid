<script lang="ts">
  import type { RecoveryStatus } from '$lib/api/recovery';
  import { describeRecoveryCause } from '$lib/api/recovery';

  interface Props {
    status: RecoveryStatus;
    nodeIp: string;
    nodeId: string;
    /** Called when operator requests a backup download. */
    onDownloadBackup?: () => void;
    /** Called when operator selects a file for restore. */
    onRestoreFile?: (file: File) => void;
    /** Called when operator wants to install firmware. */
    onInstallFirmware?: () => void;
  }

  let { status, nodeIp, nodeId, onDownloadBackup, onRestoreFile, onInstallFirmware }: Props = $props();

  const cause = $derived(describeRecoveryCause(status));

  const startupStateLabel = $derived(
    status.startupState
      ? status.startupState.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase())
      : ''
  );

  const otaStateLabel: Record<string, string> = {
    idle: 'Idle',
    pending_reboot: 'Pending reboot',
    pending_verify: 'Verifying OTA candidate',
    confirmed: 'OTA confirmed',
    rolled_back: 'OTA rolled back — candidate failed verification',
    failed: 'OTA failed',
  };

  function onFileInput(e: Event) {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (file) onRestoreFile?.(file);
    // Reset input so same file can be selected again
    (e.target as HTMLInputElement).value = '';
  }
</script>

<div class="recovery-status-card" class:safe-mode={status.safeMode}>
  <!-- Recovery mode banner — shown prominently when in safe mode -->
  {#if status.safeMode}
    <div class="recovery-banner" role="alert">
      <span class="banner-icon">⚠</span>
      <div class="banner-content">
        <strong>RECOVERY MODE</strong>
        <span>Outputs are disabled.</span>
        {#if status.outputsForcedOff}
          <span class="outputs-off-badge">All outputs forced off</span>
        {/if}
      </div>
    </div>
  {/if}

  <!-- Recovery cause section -->
  {#if cause}
    <div class="cause-panel">
      <h3>Recovery cause</h3>
      <p class="cause-text">{cause}</p>

      <dl class="detail-list">
        {#if startupStateLabel}
          <dt>Startup state</dt>
          <dd>{startupStateLabel}</dd>
        {/if}
        {#if status.failingStage}
          <dt>Failing subsystem</dt>
          <dd class="error-value">{status.failingStage}</dd>
        {/if}
        {#if status.failingError}
          <dt>Error code</dt>
          <dd class="error-value">{status.failingError}</dd>
        {/if}
        {#if status.restoreState && status.restoreState !== 'none' && status.restoreState !== ''}
          <dt>Restore state</dt>
          <dd class:interrupted={status.restoreState === 'interrupted'}>
            {status.restoreState.replace(/_/g, ' ')}
          </dd>
        {/if}
        {#if status.otaState && status.otaState !== 'idle'}
          <dt>OTA state</dt>
          <dd class:rolled-back={status.otaState === 'rolled_back' || status.otaState === 'failed'}>
            {otaStateLabel[status.otaState] ?? status.otaState}
          </dd>
        {/if}
        {#if status.recoveryNetworkActive}
          <dt>Network</dt>
          <dd>Recovery AP active</dd>
        {/if}
      </dl>
    </div>
  {:else}
    <div class="healthy-panel">
      <span class="healthy-icon">✓</span>
      <span>Node is operating normally.</span>
    </div>
  {/if}

  <!-- Recovery actions — only shown when in safe mode -->
  {#if status.safeMode}
    <div class="recovery-actions">
      <h3>Recovery actions</h3>

      {#if status.restoreState === 'interrupted'}
        <div class="interrupted-restore-notice" role="alert">
          <strong>RESTORE INTERRUPTED</strong>
          <p>Normal output remains disabled. Select a backup file to restore again.</p>
        </div>
      {/if}

      <div class="action-list">
        <button
          class="action-btn primary"
          type="button"
          onclick={onDownloadBackup}
          disabled={!onDownloadBackup}
          id="recovery-download-backup"
        >
          ↓ Download Backup
        </button>

        <label class="action-btn" for="recovery-restore-input" id="recovery-restore-label">
          ↑ Restore Backup
          <input
            id="recovery-restore-input"
            type="file"
            accept=".lgbak,.json"
            onchange={onFileInput}
            style="display:none"
          />
        </label>

        <button
          class="action-btn"
          type="button"
          onclick={onInstallFirmware}
          disabled={!onInstallFirmware}
          id="recovery-install-firmware"
        >
          Install Firmware
        </button>
      </div>

      <div class="physical-reset-note">
        <strong>Physical factory reset:</strong> Hold the recovery button for 5 seconds.
        No software factory-reset is available.
      </div>
    </div>
  {/if}
</div>

<style>
  .recovery-status-card {
    border: 1px solid #333;
    border-radius: 10px;
    overflow: hidden;
    background: #1a1a1f;
    color: #e8e8ed;
    font-family: inherit;
  }

  .recovery-status-card.safe-mode {
    border-color: #ff453a;
  }

  .recovery-banner {
    background: #3a1010;
    border-bottom: 1px solid #ff453a;
    padding: 14px 18px;
    display: flex;
    align-items: center;
    gap: 14px;
  }

  .banner-icon {
    font-size: 1.5rem;
    color: #ff453a;
    flex-shrink: 0;
  }

  .banner-content {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
  }

  .banner-content strong {
    color: #ff453a;
    font-size: 1rem;
    letter-spacing: 0.05em;
  }

  .banner-content span {
    color: #ff9f9a;
    font-size: 0.9rem;
  }

  .outputs-off-badge {
    background: #5c1a1a;
    border: 1px solid #ff453a;
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 0.8rem;
    color: #ff9f9a;
  }

  .cause-panel, .recovery-actions {
    padding: 16px 18px;
  }

  .cause-panel {
    border-bottom: 1px solid #2c2c30;
  }

  .cause-panel h3, .recovery-actions h3 {
    margin: 0 0 10px;
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #888;
  }

  .cause-text {
    margin: 0 0 12px;
    color: #ffd569;
    font-size: 0.95rem;
    line-height: 1.5;
  }

  .detail-list {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 4px 16px;
    margin: 0;
    font-size: 0.85rem;
  }

  .detail-list dt {
    color: #888;
    white-space: nowrap;
  }

  .detail-list dd {
    margin: 0;
    color: #c8c8d0;
  }

  .error-value {
    color: #ff9f0a;
    font-family: monospace;
  }

  .interrupted {
    color: #ff453a;
  }

  .rolled-back {
    color: #ff453a;
  }

  .healthy-panel {
    padding: 16px 18px;
    display: flex;
    align-items: center;
    gap: 10px;
    color: #30d158;
    font-size: 0.95rem;
  }

  .healthy-icon {
    font-size: 1.2rem;
  }

  .interrupted-restore-notice {
    background: #3a1010;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 12px 14px;
    margin-bottom: 14px;
  }

  .interrupted-restore-notice strong {
    display: block;
    color: #ff453a;
    font-size: 0.9rem;
    letter-spacing: 0.05em;
    margin-bottom: 4px;
  }

  .interrupted-restore-notice p {
    margin: 0;
    color: #ff9f9a;
    font-size: 0.85rem;
  }

  .action-list {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 14px;
  }

  .action-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 8px 16px;
    border-radius: 7px;
    border: 1px solid #444;
    background: #252528;
    color: #e8e8ed;
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 0.15s, border-color 0.15s;
    text-decoration: none;
  }

  .action-btn:hover:not(:disabled) {
    background: #303035;
    border-color: #666;
  }

  .action-btn:disabled {
    opacity: 0.4;
    cursor: default;
  }

  .action-btn.primary {
    background: #1a3a2a;
    border-color: #30d158;
    color: #30d158;
  }

  .action-btn.primary:hover:not(:disabled) {
    background: #214630;
  }

  .physical-reset-note {
    font-size: 0.8rem;
    color: #666;
    line-height: 1.4;
    padding: 10px;
    background: #111113;
    border-radius: 6px;
  }

  .physical-reset-note strong {
    color: #888;
  }
</style>
