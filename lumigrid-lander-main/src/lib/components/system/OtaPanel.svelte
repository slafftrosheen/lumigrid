<script lang="ts">
  import { uploadFirmware, getOtaStatus, isOtaSuccess, isOtaRolledBack } from '$lib/api/firmware';
  import type { OtaUploadResponse, OtaStatus } from '$lib/api/firmware';

  interface Props {
    nodeIp: string;
    /** Immutable node ID — used to reconnect after reboot. */
    nodeId: string;
  }

  let { nodeIp, nodeId }: Props = $props();

  type Phase =
    | 'select'
    | 'uploading'
    | 'pending_reboot'
    | 'waiting'
    | 'checking'
    | 'success'
    | 'rolled_back'
    | 'failed';

  let phase = $state<Phase>('select');
  let selectedFile = $state<File | null>(null);
  let progress = $state(0);
  let uploadResult = $state<OtaUploadResponse | null>(null);
  let otaStatus = $state<OtaStatus | null>(null);
  let error = $state('');

  function reset() {
    phase = 'select';
    selectedFile = null;
    progress = 0;
    uploadResult = null;
    otaStatus = null;
    error = '';
  }

  function onFileSelected(e: Event) {
    const file = (e.target as HTMLInputElement).files?.[0];
    (e.target as HTMLInputElement).value = '';
    if (!file) return;
    if (!file.name.toLowerCase().endsWith('.bin')) {
      error = 'Select a firmware .bin file.';
      return;
    }
    if (file.size <= 0) {
      error = 'The selected firmware binary is empty.';
      return;
    }
    selectedFile = file;
    error = '';
  }

  async function startUpload() {
    if (!selectedFile || phase !== 'select') return;
    phase = 'uploading';
    progress = 0;
    error = '';

    try {
      // Single canonical OTA path — POST /api/v1/ota via getNodeHttpBase()
      // targetVersion comes from firmware metadata, never from filename
      uploadResult = await uploadFirmware(nodeIp, selectedFile, (pct) => {
        progress = pct;
      });
      progress = 100;
      phase = 'pending_reboot';
      // Start polling after a short delay for reboot to begin
      setTimeout(() => waitForNode(), 3000);
    } catch (e) {
      error = `Upload failed: ${e instanceof Error ? e.message : String(e)}`;
      phase = 'failed';
    }
  }

  async function waitForNode() {
    phase = 'waiting';
    const deadline = Date.now() + 90000; // 90s timeout
    while (Date.now() < deadline) {
      await new Promise((r) => setTimeout(r, 3000));
      phase = 'checking';
      try {
        const status = await getOtaStatus(nodeIp);
        otaStatus = status;

        if (isOtaSuccess(status)) {
          phase = 'success';
          return;
        }
        if (isOtaRolledBack(status)) {
          phase = 'rolled_back';
          return;
        }
        if (status.state === 'failed') {
          phase = 'failed';
          error = status.reason ?? 'OTA failed on node.';
          return;
        }
        // pending_reboot or pending_verify — keep waiting
        phase = 'waiting';
      } catch {
        // Node is rebooting — expected
        phase = 'waiting';
      }
    }
    phase = 'failed';
    error = 'Verification timeout. Check node manually.';
  }
</script>

<div class="ota-panel">
  <h2 class="panel-title">Install Firmware</h2>

  {#if phase === 'select'}
    <p class="instruction">
      Select an ESP-IDF application image (.bin). The firmware image is NOT signed.
    </p>

    {#if error}
      <div class="error-notice" role="alert">{error}</div>
    {/if}

    <div class="file-drop-row">
      <label class="file-select-btn" for="ota-file-input" id="ota-select-label">
        Select firmware.bin…
        <input
          id="ota-file-input"
          type="file"
          accept=".bin"
          onchange={onFileSelected}
          style="display:none"
        />
      </label>
      {#if selectedFile}
        <span class="file-name">{selectedFile.name} ({(selectedFile.size / 1024).toFixed(0)} KB)</span>
      {/if}
    </div>

    {#if selectedFile}
      <div class="upload-row">
        <button type="button" class="btn-upload" onclick={startUpload} id="ota-upload-btn">
          Upload Firmware
        </button>
      </div>
    {/if}

  {:else if phase === 'uploading'}
    <div class="progress-section">
      <div class="progress-label">Uploading… {progress}%</div>
      <div class="progress-track">
        <div class="progress-bar" style="width:{progress}%"></div>
      </div>
    </div>

  {:else if phase === 'pending_reboot'}
    <div class="status-row">
      <span class="spinner" aria-hidden="true"></span>
      <div>
        <div class="status-primary">Validated candidate</div>
        {#if uploadResult?.targetVersion}
          <div class="status-sub">Target: {uploadResult.targetVersion}</div>
        {/if}
      </div>
    </div>
    <div class="phase-note">Node rebooting…</div>

  {:else if phase === 'waiting' || phase === 'checking'}
    <div class="status-row">
      <span class="spinner" aria-hidden="true"></span>
      <span>{phase === 'checking' ? 'Checking OTA status…' : 'Waiting for node…'}</span>
    </div>
    <p class="reconnect-note">
      Reconnect tracked by immutable node ID.
    </p>

  {:else if phase === 'success'}
    <div class="success-notice" role="status">
      <span class="status-icon success">✓</span>
      <div>
        <div class="status-primary">Firmware update confirmed</div>
        {#if otaStatus?.runningVersion}
          <div class="status-sub">Running: {otaStatus.runningVersion}</div>
        {/if}
      </div>
    </div>
    <button type="button" class="btn-secondary" onclick={reset} id="ota-done">Dismiss</button>

  {:else if phase === 'rolled_back'}
    <div class="rollback-notice" role="alert">
      <span class="status-icon warn">⚠</span>
      <div>
        <strong>UPDATE ROLLED BACK</strong>
        {#if otaStatus?.targetVersion && otaStatus?.previousVersion}
          <div class="rollback-detail">
            Candidate {otaStatus.targetVersion} failed startup verification.
            Node recovered on {otaStatus.previousVersion}.
          </div>
        {:else}
          <div class="rollback-detail">Candidate failed startup verification. Node recovered on previous version.</div>
        {/if}
        {#if otaStatus?.reason}
          <div class="rollback-reason">{otaStatus.reason}</div>
        {/if}
      </div>
    </div>
    <div class="action-row">
      <button type="button" class="btn-secondary" onclick={reset} id="ota-retry">
        Install Different Firmware
      </button>
    </div>

  {:else if phase === 'failed'}
    <div class="error-notice" role="alert">
      <strong>OTA failed</strong>
      {#if error}<div>{error}</div>{/if}
    </div>
    <button type="button" class="btn-secondary" onclick={reset} id="ota-retry-fail">Retry</button>
  {/if}
</div>

<style>
  .ota-panel {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
    color: #e8e8ed;
    font-family: inherit;
  }

  .panel-title {
    margin: 0 0 12px;
    font-size: 1rem;
    font-weight: 600;
    color: #e8e8ed;
  }

  .instruction {
    margin: 0 0 14px;
    color: #a8a8b0;
    font-size: 0.88rem;
    line-height: 1.5;
  }

  .file-drop-row {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 12px;
    flex-wrap: wrap;
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

  .file-select-btn:hover { background: #303035; }

  .file-name {
    color: #888;
    font-size: 0.85rem;
    font-family: monospace;
  }

  .upload-row {
    margin-top: 8px;
  }

  .btn-upload {
    padding: 9px 20px;
    border-radius: 8px;
    border: 1px solid #0a84ff;
    background: #0a1a3a;
    color: #0a84ff;
    font-size: 0.92rem;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s;
  }

  .btn-upload:hover { background: #0e2050; }

  .progress-section {
    margin: 8px 0;
  }

  .progress-label {
    font-size: 0.88rem;
    color: #a8a8b0;
    margin-bottom: 8px;
  }

  .progress-track {
    height: 6px;
    background: #2a2a30;
    border-radius: 3px;
    overflow: hidden;
  }

  .progress-bar {
    height: 100%;
    background: linear-gradient(90deg, #0a84ff, #00d4ff);
    border-radius: 3px;
    transition: width 0.3s;
  }

  .status-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 0;
  }

  .status-primary {
    font-size: 0.95rem;
    color: #e8e8ed;
  }

  .status-sub {
    font-size: 0.82rem;
    color: #888;
    margin-top: 2px;
    font-family: monospace;
  }

  .phase-note {
    color: #a8a8b0;
    font-size: 0.85rem;
    margin-left: 30px;
  }

  .spinner {
    width: 18px;
    height: 18px;
    border: 2px solid #333;
    border-top-color: #0a84ff;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    flex-shrink: 0;
  }

  @keyframes spin { to { transform: rotate(360deg); } }

  .reconnect-note {
    color: #555;
    font-size: 0.82rem;
    margin: 4px 0 0;
  }

  .success-notice {
    display: flex;
    align-items: center;
    gap: 14px;
    background: #0a2015;
    border: 1px solid #1a6030;
    border-radius: 9px;
    padding: 14px 16px;
    margin-bottom: 14px;
  }

  .rollback-notice {
    display: flex;
    align-items: flex-start;
    gap: 14px;
    background: #2a1010;
    border: 1px solid #ff453a;
    border-radius: 9px;
    padding: 14px 16px;
    margin-bottom: 14px;
  }

  .rollback-notice strong {
    display: block;
    color: #ff453a;
    font-size: 0.92rem;
    letter-spacing: 0.04em;
    margin-bottom: 6px;
  }

  .rollback-detail {
    color: #ff9f9a;
    font-size: 0.87rem;
    line-height: 1.5;
  }

  .rollback-reason {
    color: #888;
    font-size: 0.82rem;
    margin-top: 4px;
    font-family: monospace;
  }

  .status-icon {
    font-size: 1.4rem;
    flex-shrink: 0;
  }

  .status-icon.success { color: #30d158; }
  .status-icon.warn { color: #ff453a; }

  .error-notice {
    background: #2a0e0e;
    border: 1px solid #ff453a;
    border-radius: 8px;
    padding: 12px 14px;
    color: #ff9f9a;
    font-size: 0.88rem;
    margin-bottom: 12px;
  }

  .error-notice strong {
    display: block;
    margin-bottom: 4px;
    color: #ff453a;
  }

  .action-row {
    display: flex;
    gap: 10px;
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

  .btn-secondary:hover { background: #303035; }
</style>
