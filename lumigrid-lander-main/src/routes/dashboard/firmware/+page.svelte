<script lang="ts">
  import { onMount } from 'svelte';
  import { getIdentity, type NodeIdentity } from '$lib/api/node';
  import { getRecoveryStatus, type RecoveryStatus } from '$lib/api/recovery';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { getNodeLiveState } from '$lib/state/nodeStore';

  import RecoveryStatusCard from '$lib/components/system/RecoveryStatusCard.svelte';
  import DeviceConfigPanel from '$lib/components/system/DeviceConfigPanel.svelte';
  import BackupPanel from '$lib/components/system/BackupPanel.svelte';
  import RestorePanel from '$lib/components/system/RestorePanel.svelte';
  import OtaPanel from '$lib/components/system/OtaPanel.svelte';
  import FleetOtaPanel from '$lib/components/system/FleetOtaPanel.svelte';
  import CommissioningWizard from '$lib/components/system/CommissioningWizard.svelte';

  type Tab = 'overview' | 'config' | 'backup' | 'ota' | 'fleet';

  let activeTab = $state<Tab>('overview');
  let nodeIp = $state('');
  let nodeId = $state('');
  let identity = $state<NodeIdentity | null>(null);
  let recoveryStatus = $state<RecoveryStatus | null>(null);
  let loadingRecovery = $state(false);
  let showWizard = $state(false);

  let liveState = $state({ connection: 'disconnected', nodeId: '' });

  async function loadNodeState(ip: string) {
    if (!ip) {
      identity = null;
      recoveryStatus = null;
      return;
    }
    loadingRecovery = true;
    try {
      const [idRes, recRes] = await Promise.allSettled([
        getIdentity(ip),
        getRecoveryStatus(ip),
      ]);
      if (idRes.status === 'fulfilled') {
        identity = idRes.value;
        nodeId = idRes.value.id;
      }
      if (recRes.status === 'fulfilled') {
        recoveryStatus = recRes.value;
      }
    } finally {
      loadingRecovery = false;
    }
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    if (nodeIp) {
      loadNodeState(nodeIp);
    }

    const unsubLive = getNodeLiveState().subscribe((val) => {
      liveState = { connection: val.connection, nodeId: val.nodeId };
      if (val.nodeId && !nodeId) {
        nodeId = val.nodeId;
      }
    });

    const interval = setInterval(() => {
      if (nodeIp) {
        getRecoveryStatus(nodeIp).then((s) => {
          recoveryStatus = s;
        }).catch(() => {});
      }
    }, 10000);

    return () => {
      unsubLive();
      clearInterval(interval);
    };
  });

  const resolvedIdentity = $derived<NodeIdentity>(
    identity ?? {
      id: nodeId || liveState.nodeId || 'unknown-node',
      name: 'LumiGrid Node',
      family: 'led-node',
      model: 'LumiGrid LED Node',
      hardwareRevision: 'v1.0',
      firmwareVersion: '1.0.0',
    }
  );
</script>

<svelte:head>
  <title>System & Recovery — LumiGrid</title>
</svelte:head>

<div class="system-page">
  <header class="page-header">
    <div class="header-left">
      <h1>System & Recovery</h1>
      <p class="header-sub">
        Hardware configuration, authoritative limits, streaming backup/restore, and canonical OTA operations.
      </p>
    </div>
    {#if nodeIp}
      <div class="node-badge">
        <span class="node-dot" class:online={liveState.connection === 'connected'}></span>
        <span class="node-id-text">{resolvedIdentity.id} ({nodeIp})</span>
      </div>
    {/if}
  </header>

  <!-- Navigation Tabs -->
  <nav class="system-tabs" aria-label="System sections">
    <button
      type="button"
      class="tab-btn"
      class:active={activeTab === 'overview'}
      onclick={() => (activeTab = 'overview')}
      id="tab-overview"
    >
      Status & Overview
      {#if recoveryStatus?.safeMode}
        <span class="warn-dot" title="Safe Mode active">!</span>
      {/if}
    </button>
    <button
      type="button"
      class="tab-btn"
      class:active={activeTab === 'config'}
      onclick={() => (activeTab = 'config')}
      id="tab-config"
    >
      Configuration
    </button>
    <button
      type="button"
      class="tab-btn"
      class:active={activeTab === 'backup'}
      onclick={() => (activeTab = 'backup')}
      id="tab-backup"
    >
      Backup & Restore
    </button>
    <button
      type="button"
      class="tab-btn"
      class:active={activeTab === 'ota'}
      onclick={() => (activeTab = 'ota')}
      id="tab-ota"
    >
      Firmware OTA
    </button>
    <button
      type="button"
      class="tab-btn"
      class:active={activeTab === 'fleet'}
      onclick={() => (activeTab = 'fleet')}
      id="tab-fleet"
    >
      Fleet Rollout
    </button>
  </nav>

  <!-- Tab Content -->
  <div class="tab-content">
    {#if !nodeIp && activeTab !== 'fleet'}
      <div class="no-node-card">
        <h3>No Node Selected</h3>
        <p>Please select an active node from the dashboard navigation to manage its system settings.</p>
      </div>
    {:else if activeTab === 'overview'}
      <div class="overview-grid">
        <!-- Recovery Status Card -->
        {#if recoveryStatus}
          <RecoveryStatusCard
            status={recoveryStatus}
            {nodeIp}
            nodeId={resolvedIdentity.id}
            onDownloadBackup={() => { activeTab = 'backup'; }}
            onInstallFirmware={() => { activeTab = 'ota'; }}
          />
        {:else if loadingRecovery}
          <div class="loading-box">Checking recovery status…</div>
        {/if}

        <!-- System Identity & Hardware Info -->
        <div class="identity-card">
          <div class="card-header">
            <h2>Hardware & Identity</h2>
            <button
              type="button"
              class="btn-wizard"
              onclick={() => (showWizard = true)}
              id="btn-run-wizard"
            >
              ★ Run Setup Wizard
            </button>
          </div>

          <dl class="id-list">
            <dt>Immutable Node ID</dt>
            <dd><code>{resolvedIdentity.id}</code></dd>
            <dt>Device Family</dt>
            <dd>{resolvedIdentity.family}</dd>
            <dt>Hardware Model</dt>
            <dd>{resolvedIdentity.model}</dd>
            <dt>Hardware Revision</dt>
            <dd>{resolvedIdentity.hardwareRevision}</dd>
            <dt>Firmware Version</dt>
            <dd>{resolvedIdentity.firmwareVersion}</dd>
          </dl>
        </div>
      </div>
    {:else if activeTab === 'config'}
      <DeviceConfigPanel {nodeIp} />
    {:else if activeTab === 'backup'}
      <div class="backup-restore-split">
        <BackupPanel {nodeIp} nodeId={resolvedIdentity.id} />
        <RestorePanel
          {nodeIp}
          nodeId={resolvedIdentity.id}
          onRestoreComplete={() => loadNodeState(nodeIp)}
        />
      </div>
    {:else if activeTab === 'ota'}
      <OtaPanel {nodeIp} nodeId={resolvedIdentity.id} />
    {:else if activeTab === 'fleet'}
      <FleetOtaPanel />
    {/if}
  </div>
</div>

{#if showWizard && nodeIp}
  <CommissioningWizard
    {nodeIp}
    nodeId={resolvedIdentity.id}
    identity={resolvedIdentity}
    onClose={() => (showWizard = false)}
    onComplete={() => {
      showWizard = false;
      loadNodeState(nodeIp);
    }}
  />
{/if}

<style>
  .system-page {
    max-width: 1100px;
    margin: 0 auto;
    padding: 10px 0 40px;
    font-family: inherit;
    color: #e8e8ed;
  }

  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    margin-bottom: 24px;
    flex-wrap: wrap;
    gap: 16px;
  }

  .header-left h1 {
    font-size: 1.6rem;
    font-weight: 700;
    margin: 0 0 6px;
    letter-spacing: -0.02em;
  }

  .header-sub {
    margin: 0;
    color: #888898;
    font-size: 0.92rem;
  }

  .node-badge {
    background: #141419;
    border: 1px solid #2e2e3e;
    border-radius: 8px;
    padding: 6px 14px;
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 0.85rem;
    font-family: monospace;
    color: #c0c0d0;
  }

  .node-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #888;
  }

  .node-dot.online {
    background: #4cd964;
    box-shadow: 0 0 6px #4cd96488;
  }

  .system-tabs {
    display: flex;
    gap: 6px;
    border-bottom: 1px solid #282834;
    margin-bottom: 24px;
    overflow-x: auto;
    padding-bottom: 2px;
  }

  .tab-btn {
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    color: #888898;
    padding: 8px 16px;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
    white-space: nowrap;
    display: flex;
    align-items: center;
    gap: 6px;
    transition: color 0.15s, border-color 0.15s;
  }

  .tab-btn:hover {
    color: #eee;
  }

  .tab-btn.active {
    color: #5e81f4;
    border-bottom-color: #5e81f4;
  }

  .warn-dot {
    background: #ff453a;
    color: #fff;
    width: 16px;
    height: 16px;
    border-radius: 50%;
    font-size: 0.68rem;
    font-weight: 800;
    display: grid;
    place-items: center;
  }

  .overview-grid {
    display: flex;
    flex-direction: column;
    gap: 20px;
  }

  .identity-card {
    background: #1a1a1f;
    border: 1px solid #333;
    border-radius: 10px;
    padding: 20px;
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;
  }

  .card-header h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 600;
  }

  .btn-wizard {
    background: #25283a;
    border: 1px solid #5e81f4;
    color: #8fa6ff;
    border-radius: 6px;
    padding: 6px 14px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
  }

  .btn-wizard:hover {
    background: #5e81f4;
    color: #fff;
  }

  .id-list {
    display: grid;
    grid-template-columns: 180px 1fr;
    gap: 10px 16px;
    margin: 0;
    font-size: 0.88rem;
  }

  .id-list dt {
    color: #888898;
  }

  .id-list dd {
    margin: 0;
    color: #eee;
  }

  .id-list code {
    background: #111116;
    padding: 2px 6px;
    border-radius: 4px;
    color: #5e81f4;
    font-family: monospace;
  }

  .backup-restore-split {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
    gap: 20px;
  }

  .no-node-card {
    background: #1a1a1f;
    border: 1px dashed #444;
    border-radius: 10px;
    padding: 40px;
    text-align: center;
  }

  .no-node-card h3 {
    margin: 0 0 8px;
    font-size: 1.1rem;
  }

  .no-node-card p {
    color: #888898;
    margin: 0;
    font-size: 0.9rem;
  }

  .loading-box {
    padding: 20px;
    text-align: center;
    color: #888898;
  }
</style>
