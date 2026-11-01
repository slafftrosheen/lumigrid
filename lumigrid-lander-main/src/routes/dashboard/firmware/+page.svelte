<script lang="ts">
  import { onMount } from 'svelte';
  import { getIdentity, type NodeIdentity } from '$lib/api/node';
  import { getRecoveryStatus, type RecoveryStatus } from '$lib/api/recovery';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';
  import { nodeStore } from '$lib/state/nodeStore';

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

  let liveState = $state({ connection: 'idle' });
  let targetGeneration = $state(0);
  let nodeSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let disposed = false;
  let readRequest = 0;
  let pageError = $state('');

  function capturePageOperation() {
    const generation = targetGeneration, ip = nodeIp, session = nodeAuthStore.getSession(ip);
    return { ip, isCurrent: () => !disposed && generation === targetGeneration && nodeIp === ip && nodeAuthStore.getSession(ip) === session };
  }

  async function loadNodeState(ip: string) {
    const owner = capturePageOperation();
    if (!ip || owner.ip !== ip || !owner.isCurrent()) return;
    const request = ++readRequest;
    loadingRecovery = true;
    try {
      const [idRes, recRes] = await Promise.allSettled([getIdentity(ip), getRecoveryStatus(ip)]);
      if (!owner.isCurrent() || request !== readRequest) return;
      const issues: string[] = [];
      if (idRes.status === 'fulfilled' && idRes.value?.id) {
        if (nodeId && nodeId !== idRes.value.id) {
          targetGeneration++; recoveryStatus = null; showWizard = false;
          identity = idRes.value; nodeId = idRes.value.id; loadingRecovery = false;
          pageError = 'A different node now uses this address. Its system view has been reloaded.';
          return;
        }
        identity = idRes.value; nodeId = idRes.value.id;
      } else issues.push('Node identity could not be loaded.');
      if (recRes.status === 'fulfilled') recoveryStatus = recRes.value;
      else issues.push('Recovery status could not be refreshed. Any displayed status is the last received value.');
      pageError = issues.join(' ');
    } finally {
      if (owner.isCurrent() && request === readRequest) loadingRecovery = false;
    }
  }

  function syncFocusedNode(ip: string) {
    const session = nodeAuthStore.getSession(ip);
    if (ip === nodeIp && session === nodeSession) return;
    targetGeneration++; readRequest++;
    nodeIp = ip; nodeSession = session; nodeId = '';
    identity = recoveryStatus = null; showWizard = false; pageError = ''; loadingRecovery = false;
    if (ip) void loadNodeState(ip);
  }

  onMount(() => {
    disposed = false;
    const unsubscribe = nodeStore.subscribe(state => {
      liveState = { connection: state.connection };
      syncFocusedNode(state.route || '');
    });
    const unsubscribeAuth = nodeAuthStore.subscribe(() => syncFocusedNode(nodeIp));
    let timer: ReturnType<typeof setTimeout>;
    const refresh = async () => {
      if (nodeIp) await loadNodeState(nodeIp);
      if (!disposed) timer = setTimeout(refresh, 10000);
    };
    timer = setTimeout(refresh, 10000);
    return () => { disposed = true; targetGeneration++; readRequest++; clearTimeout(timer); unsubscribe(); unsubscribeAuth(); };
  });

  const resolvedIdentity = $derived<NodeIdentity>(identity ?? {
    id: 'Unavailable', name: 'LumiGrid Node', family: 'Unavailable', model: 'Unavailable',
    hardwareRevision: 'Unavailable', firmwareVersion: 'Unavailable',
  });
</script>

<svelte:head>
  <title>System & Recovery — LumiGrid</title>
</svelte:head>

<div class="system-page">
  <header class="page-header">
    <div class="header-left">
      <h1>System & Recovery</h1>
      <p class="header-sub">
        Configure this node, protect saved shows, and verify firmware updates.
      </p>
    </div>
    {#if nodeIp}
      <div class="node-badge">
        <span class="node-dot" class:online={liveState.connection === 'online' || liveState.connection === 'degraded'}></span>
        <span class="node-id-text">{resolvedIdentity.id} ({nodeIp})</span>
      </div>
    {/if}
  </header>

  <!-- Navigation Tabs -->
  <nav class="system-tabs" aria-label="System sections">
    <button type="button" class="tab-btn" onclick={() => window.dispatchEvent(new Event('lumigrid:open-access'))}>Access</button>
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

  {#if pageError}
    <div class="loading-box" role="alert">
      <p>{pageError}</p>
      <button type="button" class="btn-wizard" onclick={() => loadNodeState(nodeIp)} disabled={!nodeIp || loadingRecovery}>Retry</button>
    </div>
  {/if}

  <!-- Tab Content -->
  <div class="tab-content">
    {#key activeTab === 'fleet' ? 'fleet' : targetGeneration}
    {@const owner = capturePageOperation()}
    {#if !nodeIp && activeTab !== 'fleet'}
      <div class="no-node-card">
        <h3>No Node Selected</h3>
        <p>Please select an active node from the dashboard navigation to manage its system settings.</p>
      </div>
    {:else if !identity && activeTab !== 'fleet'}
      <div class="loading-box" role="status">{loadingRecovery ? 'Loading this node’s identity and recovery status…' : 'Node identity is unavailable. Reload before managing this node.'}</div>
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
          onRestoreComplete={() => { if (owner.isCurrent()) void loadNodeState(owner.ip); }}
        />
      </div>
    {:else if activeTab === 'ota'}
      <OtaPanel {nodeIp} nodeId={resolvedIdentity.id} />
    {:else if activeTab === 'fleet'}
      <FleetOtaPanel />
    {/if}
    {/key}
  </div>
</div>

{#if showWizard && nodeIp && identity}
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
    flex: 0 0 auto;
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
