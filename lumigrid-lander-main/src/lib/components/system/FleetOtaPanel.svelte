<script lang="ts">
  import { onDestroy } from 'svelte';
  import { onlineNodes } from '$lib/fleet/fleetStore';
  import { getNodeDisplayName } from '$lib/fleet/types';
  import {
    executeStagedDeployment,
    type DeploymentPlan,
    type DeploymentProgress,
  } from '$lib/api/deployment';

  let selectedFile = $state<File | null>(null);
  let selectedNodeIds = $state<string[]>([]);
  let targetVersion = $state('');
  let isDeploying = $state(false);
  let progress = $state<DeploymentProgress | null>(null);
  let errorMsg = $state('');
  let successMsg = $state('');
  let disposed = false;
  let controller: AbortController | null = null;
  onDestroy(() => { disposed = true; controller?.abort(); });

  function onFileChange(e: Event) {
    const file = (e.target as HTMLInputElement).files?.[0];
    (e.target as HTMLInputElement).value = '';
    if (!file) return;
    if (!file.name.toLowerCase().endsWith('.bin') || file.size <= 0) {
      selectedFile = null;
      errorMsg = 'Select a non-empty firmware .bin file.';
      return;
    }
    selectedFile = file;
    errorMsg = '';
  }

  function toggleNode(id: string) {
    if (selectedNodeIds.includes(id)) {
      selectedNodeIds = selectedNodeIds.filter((x) => x !== id);
    } else {
      selectedNodeIds = [...selectedNodeIds, id];
    }
  }

  function selectAll() {
    selectedNodeIds = $onlineNodes.map((n) => n.id);
  }

  function deselectAll() {
    selectedNodeIds = [];
  }

  async function startDeployment() {
    if (!selectedFile || selectedNodeIds.length === 0 || isDeploying) return;

    errorMsg = '';
    successMsg = '';
    isDeploying = true;
    progress = null;
    const operation = new AbortController(); controller = operation;

    // Build nodeAddressMap mapping immutable ID -> IP from fleet store
    const nodeAddressMap = new Map<string, string>();
    for (const id of selectedNodeIds) {
      const node = $onlineNodes.find((n) => n.id === id);
      if (node?.address) {
        nodeAddressMap.set(id, node.address);
      }
    }

    const plan: DeploymentPlan = {
      id: `fleet-ota-${Date.now()}`,
      targetVersion: targetVersion.trim(),
      nodeIds: [...selectedNodeIds],
      canaryCount: 1,
      concurrency: 2,
      state: 'preflight',
    };

    try {
      const result = await executeStagedDeployment(
        plan,
        nodeAddressMap,
        selectedFile,
        (p) => {
          if (!disposed && controller === operation) progress = p;
        },
        { signal: operation.signal }
      );

      if (disposed || controller !== operation) return;
      if (result.success) {
        successMsg = `Deployment completed successfully on ${result.completed.length} nodes.`;
      } else {
        errorMsg = progress?.error || `Deployment halted. Failed nodes: ${result.failed.join(', ')}`;
      }
    } catch (e) {
      if (disposed || controller !== operation) return;
      errorMsg = `Deployment failed: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      if (!disposed && controller === operation) { isDeploying = false; controller = null; }
    }
  }
</script>

<div class="fleet-ota-panel" id="fleet-ota-panel">
  <div class="panel-header">
    <h2>Staged Fleet OTA Deployment</h2>
    <p class="description">
      Deploys firmware with automatic canary staging and cross-reboot verification.
      A failed or unconfirmed canary stops the rollout. Later batches stop after any failed verification. Keep this page open; leaving stops further uploads, while already accepted updates may still finish.
    </p>
  </div>

  <!-- Firmware Binary Selection -->
  <div class="section">
    <label class="file-picker-label">
      <span>Firmware Binary (.bin)</span>
      <input type="file" accept=".bin" onchange={onFileChange} disabled={isDeploying} id="fleet-file-input" />
      {#if selectedFile}
        <span class="file-name">Selected: {selectedFile.name} ({Math.round(selectedFile.size / 1024)} KB)</span>
      {/if}
    </label>

    <label class="version-label">
      <span>Expected Target Version (Optional verification check)</span>
      <input
        type="text"
        placeholder="e.g. 1.1.0"
        bind:value={targetVersion}
        disabled={isDeploying}
        id="fleet-target-version"
      />
    </label>
  </div>

  <!-- Target Node Selection (Keyed by Immutable Node ID) -->
  <div class="section">
    <div class="section-header">
      <span class="section-title">Select Target Nodes ({selectedNodeIds.length} / {$onlineNodes.length} online selected)</span>
      <div class="select-actions">
        <button type="button" class="btn-text" onclick={selectAll} disabled={isDeploying}>Select All</button>
        <button type="button" class="btn-text" onclick={deselectAll} disabled={isDeploying}>Deselect All</button>
      </div>
    </div>

    {#if $onlineNodes.length === 0}
      <div class="no-nodes">No online nodes discovered in fleet.</div>
    {:else}
      <div class="node-grid">
        {#each $onlineNodes as node (node.id)}
          <label class="node-chip" class:selected={selectedNodeIds.includes(node.id)}>
            <input
              type="checkbox"
              checked={selectedNodeIds.includes(node.id)}
              onchange={() => toggleNode(node.id)}
              disabled={isDeploying}
            />
            <div class="node-chip-info">
              <span class="node-name">{getNodeDisplayName(node)}</span>
              <span class="node-sub">ID: {node.id} • {node.address || ''} • v{node.firmware || '?'}</span>
            </div>
          </label>
        {/each}
      </div>
    {/if}
  </div>

  {#if errorMsg}
    <div class="error-notice" role="alert">{errorMsg}</div>
  {/if}
  {#if successMsg}
    <div class="success-notice" role="status">{successMsg}</div>
  {/if}

  <!-- Progress State -->
  {#if progress}
    <div class="progress-box">
      <div class="progress-header">
        <span class="phase-badge phase-{progress.state}">Phase: {progress.state.toUpperCase()}</span>
        {#if progress.canaryNodeId}
          <span class="canary-tag">Canary Node: {progress.canaryNodeId}</span>
        {/if}
      </div>

      <div class="progress-details">
        <div class="progress-stat">
          <span class="stat-label">Completed:</span>
          <span class="stat-val">{progress.completedNodeIds.length}</span>
        </div>
        <div class="progress-stat">
          <span class="stat-label">Active:</span>
          <span class="stat-val">{progress.activeNodeIds.join(', ') || 'none'}</span>
        </div>
        <div class="progress-stat">
          <span class="stat-label">Failed:</span>
          <span class="stat-val failed">{progress.failedNodeIds.length}</span>
        </div>
      </div>
    </div>
  {/if}

  <div class="action-bar">
    <button
      type="button"
      class="btn-deploy"
      disabled={!selectedFile || selectedNodeIds.length === 0 || isDeploying}
      onclick={startDeployment}
      id="fleet-deploy-btn"
    >
      {#if isDeploying}
        Deploying Fleet ({progress?.state || 'in progress'})…
      {:else}
        Start Staged Rollout ({selectedNodeIds.length} nodes)
      {/if}
    </button>
  </div>
</div>

<style>
  .fleet-ota-panel {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
    color: #e8e8ed;
    font-family: inherit;
  }

  .panel-header h2 {
    margin: 0 0 6px;
    font-size: 1.15rem;
    font-weight: 600;
  }

  .description {
    margin: 0 0 16px;
    color: #a0a0b0;
    font-size: 0.88rem;
  }

  .section {
    background: #141418;
    border: 1px solid #282832;
    border-radius: 8px;
    padding: 14px;
    margin-bottom: 16px;
  }

  .section-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
  }

  .section-title {
    font-size: 0.85rem;
    font-weight: 600;
    color: #c0c0d0;
  }

  .select-actions {
    display: flex;
    gap: 12px;
  }

  .btn-text {
    background: none;
    border: none;
    color: var(--dash-accent);
    font-size: 0.8rem;
    cursor: pointer;
    padding: 0;
  }

  .btn-text:hover {
    text-decoration: underline;
  }

  .file-picker-label, .version-label {
    display: flex;
    flex-direction: column;
    gap: 6px;
    font-size: 0.84rem;
    color: #b0b0be;
    margin-bottom: 12px;
  }

  .file-name {
    font-size: 0.8rem;
    color: #4cd964;
    font-family: monospace;
  }

  input[type="text"] {
    background: #111116;
    border: 1px solid #3c3c4a;
    border-radius: 5px;
    color: #eee;
    padding: 7px 10px;
    font-size: 0.88rem;
    max-width: 320px;
  }

  .node-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
    gap: 10px;
    max-height: 240px;
    overflow-y: auto;
  }

  .node-chip {
    background: #1b1b22;
    border: 1px solid #30303c;
    border-radius: 6px;
    padding: 8px 10px;
    display: flex;
    align-items: center;
    gap: 10px;
    cursor: pointer;
    user-select: none;
  }

  .node-chip.selected {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), .12);
  }

  .node-chip-info {
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .node-name {
    font-size: 0.84rem;
    font-weight: 600;
    color: #eee;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .node-sub {
    font-size: 0.72rem;
    color: #888898;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .no-nodes {
    color: #888898;
    font-size: 0.85rem;
    padding: 12px;
    text-align: center;
  }

  .progress-box {
    background: #121217;
    border: 1px solid #2e2e3e;
    border-radius: 8px;
    padding: 14px;
    margin-bottom: 16px;
  }

  .progress-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
  }

  .phase-badge {
    padding: 3px 8px;
    border-radius: 4px;
    font-size: 0.75rem;
    font-weight: 700;
    letter-spacing: 0.05em;
  }

  .phase-preflight { background: #28283a; color: #a0a0c0; }
  .phase-canary { background: #3a2e10; color: #ffd60a; }
  .phase-verifying { background: #1a2a3a; color: #64d2ff; }
  .phase-deploying { background: rgba(var(--dash-accent-rgb), .12); color: var(--dash-accent); }
  .phase-completed { background: #123018; color: #4cd964; }
  .phase-failed { background: #3a1515; color: #ff453a; }

  .canary-tag {
    font-size: 0.78rem;
    color: #ffd60a;
  }

  .progress-details {
    display: flex;
    gap: 20px;
    font-size: 0.82rem;
  }

  .stat-label { color: #888898; margin-right: 4px; }
  .stat-val { font-weight: 600; color: #eee; }
  .stat-val.failed { color: #ff453a; }

  .action-bar {
    display: flex;
    justify-content: flex-end;
  }

  .btn-deploy {
    background: var(--dash-accent);
    /* The opaque accent needs a dark label in both page color schemes. */
    color: #000;
    border: none;
    border-radius: 6px;
    padding: 9px 20px;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
  }

  .btn-deploy:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  .error-notice {
    background: #301515;
    border: 1px solid #ff453a;
    color: #ff9f9a;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.88rem;
    margin-bottom: 14px;
  }

  .success-notice {
    background: #152b18;
    border: 1px solid #4cd964;
    color: #a8eec0;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.88rem;
    margin-bottom: 14px;
  }
  button:focus-visible, input:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 3px; }
  .file-name, .stat-val { overflow-wrap: anywhere; }
  .progress-details, .section-header, .progress-header { flex-wrap: wrap; gap: 12px; }
</style>
