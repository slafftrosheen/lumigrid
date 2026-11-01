<script lang="ts">
  import { onMount } from 'svelte';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';
  import {
    uploadFirmware, getVerifiedOtaStatus, bindOtaTarget, isOtaTargetStatus,
    isOtaSuccess, isOtaRolledBack, isOtaSlotReady, firmwareFitsSlot, waitForOtaDelay,
    OtaUnconfirmedError, OtaIdentityError,
    type OtaUploadResponse, type OtaStatus, type OtaVerificationTarget,
  } from '$lib/api/firmware';

  let { nodeIp, nodeId }: { nodeIp: string; nodeId: string } = $props();
  type Phase = 'select' | 'uploading' | 'pending_reboot' | 'waiting' | 'checking' | 'success' | 'rolled_back' | 'failed' | 'unconfirmed';
  let phase = $state<Phase>('select');
  let selectedFile = $state<File | null>(null);
  let fileInput = $state<HTMLInputElement>();
  let progress = $state(0);
  let uploadResult = $state<OtaUploadResponse | null>(null);
  let otaStatus = $state<OtaStatus | null>(null);
  let error = $state('');
  let capacity = $state<OtaStatus | null>(null);
  let capacityLoading = $state(false);
  let generation = 0;
  let disposed = false;
  let controller: AbortController | null = null;
  const fits = $derived(selectedFile && capacity?.maxFirmwareBytes
    ? firmwareFitsSlot(selectedFile.size, capacity.maxFirmwareBytes) : null);
  const capacityReady = $derived(Boolean(capacity && isOtaSlotReady(capacity)));
  const earlierUpdatePending = $derived(capacity?.state === 'pending_reboot' || capacity?.state === 'pending_verify');
  const mb = (bytes: number) => (bytes / 1048576).toFixed(2) + ' MB';

  function captureOperation() {
    const current = generation, ip = nodeIp, id = nodeId, session = nodeAuthStore.getSession(ip);
    return { ip, id, isCurrent: () => !disposed && generation === current && nodeIp === ip && nodeId === id && nodeAuthStore.getSession(ip) === session };
  }

  async function loadCapacity() {
    const owner = captureOperation();
    capacityLoading = true; capacity = null; error = '';
    try {
      const status = await getVerifiedOtaStatus(owner.ip, owner.id);
      if (!owner.isCurrent()) return;
      capacity = status;
      if (!isOtaSlotReady(status)) error = 'Firmware slot information is unavailable. Reload it before uploading.';
    } catch (cause) {
      if (owner.isCurrent()) error = `Could not read this node's update status: ${cause instanceof Error ? cause.message : String(cause)}`;
    } finally { if (owner.isCurrent()) capacityLoading = false; }
  }

  onMount(() => {
    disposed = false;
    void loadCapacity();
    return () => { disposed = true; generation++; controller?.abort(); };
  });

  function reset() {
    generation++; controller?.abort(); controller = null;
    phase = 'select'; selectedFile = null; progress = 0; uploadResult = otaStatus = null; error = '';
    void loadCapacity();
  }

  function onFileSelected(event: Event) {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0]; input.value = '';
    if (!file) return;
    if (!file.name.toLowerCase().endsWith('.bin') || file.size <= 0) {
      selectedFile = null; error = 'Select a non-empty firmware .bin file.'; return;
    }
    selectedFile = file; error = '';
  }

  async function startUpload() {
    if (!selectedFile || phase !== 'select' || !capacityReady || capacityLoading || earlierUpdatePending || fits !== true) return;
    const owner = captureOperation(), file = selectedFile;
    if (!owner.isCurrent()) return;
    const operation = new AbortController(); controller = operation;
    let uploadStarted = false;
    phase = 'uploading'; progress = 0; error = '';
    try {
      const before = await getVerifiedOtaStatus(owner.ip, owner.id, operation.signal);
      if (!owner.isCurrent()) return;
      capacity = before;
      if (!isOtaSlotReady(before) || !firmwareFitsSlot(file.size, before.maxFirmwareBytes ?? 0) || before.state === 'pending_reboot' || before.state === 'pending_verify') {
        throw new Error('The node is not ready for this image. Reload its update status before retrying.');
      }
      uploadStarted = true;
      const receipt = await uploadFirmware(owner.ip, file, (pct) => {
        if (owner.isCurrent()) progress = pct;
      }, { signal: operation.signal, expectedNodeId: before.targetBinding ? owner.id : undefined });
      if (!owner.isCurrent()) return;
      const target = bindOtaTarget(receipt, before, owner.id);
      uploadResult = receipt; progress = 100; phase = 'pending_reboot';
      await waitForOtaDelay(3000, operation.signal);
      if (owner.isCurrent()) await waitForNode(owner, target, operation);
    } catch (cause) {
      if (!owner.isCurrent()) return;
      error = cause instanceof Error ? cause.message : String(cause);
      if (!uploadStarted) {
        // No image was sent: keep the selected file for a fresh preflight.
        capacity = null; phase = 'select';
      } else {
        phase = cause instanceof OtaUnconfirmedError || cause instanceof OtaIdentityError ? 'unconfirmed' : 'failed';
      }
    } finally {
      if (owner.isCurrent() && controller === operation) controller = null;
    }
  }

  async function waitForNode(owner: ReturnType<typeof captureOperation>, target: OtaVerificationTarget, operation: AbortController) {
    phase = 'waiting';
    const deadline = setTimeout(() => {
      if (!owner.isCurrent()) return;
      phase = 'unconfirmed'; error = 'Startup verification was not confirmed within 90 seconds. Check this node before retrying.';
      operation.abort();
    }, 90000);
    try {
      while (owner.isCurrent() && !operation.signal.aborted) {
        await waitForOtaDelay(3000, operation.signal);
        if (!owner.isCurrent()) return;
        phase = 'checking';
        try {
          const status = await getVerifiedOtaStatus(owner.ip, owner.id, operation.signal);
          if (!owner.isCurrent() || operation.signal.aborted) return;
          otaStatus = status;
          if (isOtaTargetStatus(status, target)) {
            if (isOtaRolledBack(status)) { phase = 'rolled_back'; return; }
            if (isOtaSuccess(status, target)) { phase = 'success'; return; }
            if (status.state === 'failed') { phase = 'failed'; error = status.reason ?? 'Startup verification failed on this node.'; return; }
          }
          phase = 'waiting';
        } catch (cause) {
          if (!owner.isCurrent() || operation.signal.aborted) return;
          if (cause instanceof OtaIdentityError) { phase = 'unconfirmed'; error = cause.message; return; }
          phase = 'waiting';
        }
      }
    } catch (cause) {
      if (!operation.signal.aborted) throw cause;
    } finally { clearTimeout(deadline); }
  }
</script>

<div class="ota-panel">
  <h2 class="panel-title">Install Firmware</h2>
  {#if capacity?.maxFirmwareBytes}
    <p class="instruction">Firmware storage — Running: {capacity.runningPartition ?? 'Unknown'} ·
      Slot: {mb(capacity.maxFirmwareBytes)} · Image: {capacity.imageBytes ? mb(capacity.imageBytes) : 'Not measured'} ·
      Available: {capacity.imageBytes ? mb(Math.max(0, capacity.maxFirmwareBytes - capacity.imageBytes)) : 'Not measured'}</p>
  {/if}

  {#if phase === 'select'}
    <p class="instruction">
      Select a LumiGrid application image (.bin). Use the application binary, not a merged installation image. Keep this page open until startup verification finishes.
    </p>

    {#if error}
      <div class="error-notice" role="alert">{error}</div>
    {/if}

    {#if capacityLoading}<p class="instruction" role="status">Reading this node's firmware storage…</p>{/if}
    {#if !capacityReady || earlierUpdatePending}
      <p class="instruction">{earlierUpdatePending ? 'An earlier update is still awaiting startup verification.' : 'Load this node’s identity and firmware slot before uploading.'}</p>
      <button type="button" class="btn-secondary" onclick={loadCapacity} disabled={capacityLoading}>Reload update status</button>
    {/if}
    <div class="file-drop-row">
      <button type="button" class="file-select-btn" onclick={() => fileInput?.click()} id="ota-select-label">Select firmware.bin…</button>
      <input bind:this={fileInput} id="ota-file-input" type="file" accept=".bin" aria-label="Firmware application image" onchange={onFileSelected} hidden />
      {#if selectedFile}
        <span class="file-name">{selectedFile.name} ({(selectedFile.size / 1024).toFixed(0)} KB)</span>
        {#if fits !== null}<span>{fits ? 'Fits OTA slot ✓' : 'Image exceeds OTA slot capacity'}</span>{/if}
      {/if}
    </div>

    {#if selectedFile}
      <div class="upload-row">
        <button type="button" class="btn-upload" onclick={startUpload} id="ota-upload-btn" disabled={!capacityReady || capacityLoading || earlierUpdatePending || fits !== true}>
          Upload Firmware
        </button>
      </div>
    {/if}

  {:else if phase === 'uploading'}
    <div class="progress-section">
      <div class="progress-label">Uploading… {progress}%</div>
      <div class="progress-track" role="progressbar" aria-label="Firmware upload" aria-valuemin="0" aria-valuemax="100" aria-valuenow={progress}>
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
      Confirming {nodeId} at {nodeIp}. If its address changes, select the node at its new address and check its update status.
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

  {:else if phase === 'unconfirmed'}
    <div class="error-notice" role="alert">
      <strong>Update outcome unconfirmed</strong>
      <div>{error}</div>
      <div>An already accepted update may still finish on the node.</div>
    </div>
    <button type="button" class="btn-secondary" onclick={reset}>Return to update status</button>
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
    color: var(--dash-text);
    font-family: inherit;
  }

  .panel-title {
    margin: 0 0 12px;
    font-size: 1rem;
    font-weight: 600;
    color: var(--dash-text);
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
    color: var(--dash-text);
    font-size: 0.9rem;
    cursor: pointer;
    transition: background 0.15s;
  }

  .file-select-btn:hover { background: #303035; }

  .file-name {
    min-width: 0;
    overflow-wrap: anywhere;
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
    border: 1px solid var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), .16);
    color: var(--dash-accent);
    font-size: 0.92rem;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s;
  }

  .btn-upload:hover { background: rgba(var(--dash-accent-rgb), .25); }

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
    background: linear-gradient(90deg, var(--dash-accent), var(--dash-accent-hover));
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
    color: var(--dash-text);
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
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    flex-shrink: 0;
  }

  @keyframes spin { to { transform: rotate(360deg); } }

  .reconnect-note {
    color: var(--dash-muted);
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
  button:disabled { opacity: .5; cursor: not-allowed; }
  button:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 3px; }
</style>
