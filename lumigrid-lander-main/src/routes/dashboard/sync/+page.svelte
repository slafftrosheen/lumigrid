<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    getSavedNodes,
    getSelectedNodeIp,
    getAuthRole,
    type SavedNodeInfo,
  } from '$lib/auth/storage';
  import {
    getSyncStatus,
    getShowManifest,
    compareShowManifest,
    buildClusterPlan,
    type SyncStatus,
    type ShowManifest,
    type ShowParityResult,
  } from '$lib/api/sync';
  import {
    deployShowToNodes,
    type ShowDeploymentOptions,
    type ShowDeploymentNodeResult,
    DEFAULT_SHOW_DEPLOYMENT_OPTIONS,
  } from '$lib/api/showDeployment';
  import { getDeviceConfig, saveDeviceConfig } from '$lib/api/config';
  import { activateScene, getPresets, type ScenePreset } from '$lib/api/scenes';
  import { getSequence, saveSequence, type Sequence } from '$lib/api/timeline';

  interface FleetNodeSyncData {
    node: SavedNodeInfo;
    online: boolean;
    syncStatus?: SyncStatus;
    manifest?: ShowManifest;
    parity?: ShowParityResult;
    error?: string;
  }

  let nodes = $state<SavedNodeInfo[]>([]);
  let fleetData = $state<Record<string, FleetNodeSyncData>>({});
  let selectedLeaderIp = $state<string>('');
  let leaderNodeData = $state<FleetNodeSyncData | null>(null);
  let availablePresets = $state<ScenePreset[]>([]);
  let leaderTimeline = $state<Sequence | null>(null);

  let loading = $state(false);
  let pollInterval: ReturnType<typeof setInterval> | null = null;
  let isAdmin = $state(false);

  // Modals & Panels
  let showConfigureModal = $state(false);
  let showDeployModal = $state(false);
  let diffTargetNode = $state<FleetNodeSyncData | null>(null);

  // Configure Group form state
  let configGroupName = $state('Main Sync Group');
  let configMode = $state<'auto' | 'explicit'>('auto');
  let configLeaderIp = $state('');
  let configSelectedIps = $state<string[]>([]);
  let configSaving = $state(false);
  let configError = $state('');
  let configSuccess = $state('');

  // Deploy Show form state
  let deploySourceIp = $state('');
  let deployTargetIps = $state<string[]>([]);
  let deployOptions = $state<ShowDeploymentOptions>({ ...DEFAULT_SHOW_DEPLOYMENT_OPTIONS });
  let deployRunning = $state(false);
  let deployResults = $state<Record<string, ShowDeploymentNodeResult>>({});
  let deployError = $state('');

  // Synchronized Scene / Timeline control state
  let selectedSceneIndex = $state<number>(0);
  let sceneTriggerPending = $state(false);
  let timelineTriggerPending = $state(false);
  let controlFeedback = $state('');

  // Derived cluster aggregates
  let onlineNodes = $derived(Object.values(fleetData).filter((d) => d.online));
  let totalCount = $derived(nodes.length);
  let onlineCount = $derived(onlineNodes.length);
  let clockLockedCount = $derived(onlineNodes.filter((d) => d.syncStatus?.clockLocked).length);

  // Parity evaluated against current leader
  let showParityCount = $derived(
    onlineNodes.filter((d) => d.parity?.status === 'MATCHED').length
  );

  let clusterTransport = $derived.by(() => {
    if (!leaderNodeData?.syncStatus) return 'Standalone';
    const t = leaderNodeData.syncStatus.transport;
    if (t === 'timeline') return 'Timeline';
    if (t === 'scene') return 'Scene';
    return 'Standalone';
  });

  let worstOffsetMs = $derived.by(() => {
    let maxAbs = 0;
    let worst = 0;
    for (const d of onlineNodes) {
      if (d.syncStatus?.clockOffsetMs !== undefined) {
        if (Math.abs(d.syncStatus.clockOffsetMs) >= maxAbs) {
          maxAbs = Math.abs(d.syncStatus.clockOffsetMs);
          worst = d.syncStatus.clockOffsetMs;
        }
      }
    }
    return worst;
  });

  let worstJitterUs = $derived.by(() => {
    let maxJitter = 0;
    for (const d of onlineNodes) {
      if (d.syncStatus?.clockJitterUs && d.syncStatus.clockJitterUs > maxJitter) {
        maxJitter = d.syncStatus.clockJitterUs;
      }
    }
    return maxJitter;
  });

  let worstRttMs = $derived.by(() => {
    let maxRtt = 0;
    for (const d of onlineNodes) {
      if (d.syncStatus?.peers) {
        for (const p of d.syncStatus.peers) {
          if (p.latencyMs > maxRtt) maxRtt = p.latencyMs;
        }
      }
    }
    return maxRtt;
  });

  // Cluster Ready Gate: all online, sync running, clock locked, matched
  let isClusterReady = $derived.by(() => {
    if (onlineCount === 0 || onlineCount < totalCount) return false;
    if (!leaderNodeData?.online) return false;
    for (const d of onlineNodes) {
      if (!d.syncStatus?.running || !d.syncStatus.clockLocked) return false;
      if (d.parity?.status !== 'MATCHED') return false;
    }
    return true;
  });

  let followers = $derived(
    onlineNodes.filter((d) => d.node.ip !== leaderNodeData?.node.ip)
  );

  onMount(async () => {
    isAdmin = getAuthRole() === 'admin';
    nodes = getSavedNodes();
    if (nodes.length > 0) {
      configSelectedIps = nodes.map((n) => n.ip);
      configLeaderIp = nodes[0].ip;
      deploySourceIp = nodes[0].ip;
      deployTargetIps = nodes.slice(1).map((n) => n.ip);
    }
    await refreshCluster();
    pollInterval = setInterval(refreshCluster, 3000);
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });

  async function refreshCluster() {
    nodes = getSavedNodes();
    if (nodes.length === 0) {
      fleetData = {};
      return;
    }

    // Query nodes concurrently with max 4 concurrency
    const nextData: Record<string, FleetNodeSyncData> = {};
    const queue = [...nodes];
    const concurrency = 4;

    async function fetchNode(item: SavedNodeInfo) {
      try {
        const [syncStatus, manifest] = await Promise.all([
          getSyncStatus(item.ip).catch(() => null),
          getShowManifest(item.ip).catch(() => null),
        ]);

        if (syncStatus && manifest) {
          nextData[item.ip] = {
            node: item,
            online: true,
            syncStatus,
            manifest,
          };
        } else {
          nextData[item.ip] = {
            node: item,
            online: false,
            error: 'Node unreachable or sync endpoint offline',
          };
        }
      } catch (err: any) {
        nextData[item.ip] = {
          node: item,
          online: false,
          error: err?.message || 'Connection failed',
        };
      }
    }

    async function worker() {
      while (queue.length > 0) {
        const item = queue.shift();
        if (item) await fetchNode(item);
      }
    }

    await Promise.all(Array.from({ length: Math.min(concurrency, nodes.length) }, () => worker()));

    // Identify current leader (activeRole === 'master' or first online)
    let leaderIp = '';
    for (const [ip, data] of Object.entries(nextData)) {
      if (data.online && data.syncStatus?.activeRole === 'master') {
        leaderIp = ip;
        break;
      }
    }

    if (!leaderIp) {
      // Fallback: node marked as master by address in worker nodes
      for (const data of Object.values(nextData)) {
        if (data.online && data.syncStatus?.masterAddress) {
          leaderIp = data.syncStatus.masterAddress;
          break;
        }
      }
    }

    if (!leaderIp) {
      const activeIp = getSelectedNodeIp();
      leaderIp = nextData[activeIp]?.online ? activeIp : (nodes[0]?.ip ?? '');
    }

    selectedLeaderIp = leaderIp;
    const leaderData = nextData[leaderIp] ?? null;
    leaderNodeData = leaderData;

    // Evaluate parity of all nodes against leader
    if (leaderData?.manifest) {
      for (const [ip, data] of Object.entries(nextData)) {
        if (data.online && data.manifest) {
          data.parity = compareShowManifest(leaderData.manifest, data.manifest);
        } else {
          data.parity = { status: 'UNAVAILABLE', diffs: [] };
        }
      }
    }

    fleetData = nextData;

    // Load presets and timeline from leader for synchronized operator actions
    if (leaderData?.online && availablePresets.length === 0) {
      try {
        availablePresets = await getPresets(leaderIp);
        leaderTimeline = await getSequence(leaderIp).catch(() => null);
      } catch {
        // Ignored in background refresh
      }
    }
  }

  async function handleSaveClusterConfig() {
    configError = '';
    configSuccess = '';
    configSaving = true;

    try {
      const { plan, error } = buildClusterPlan(
        configSelectedIps,
        configMode,
        configLeaderIp,
        configGroupName
      );

      if (error) {
        configError = error;
        configSaving = false;
        return;
      }

      // Apply sequentially to each node
      for (const [ip, nodePlan] of Object.entries(plan)) {
        const currentCfg = await getDeviceConfig(ip);
        await saveDeviceConfig(ip, {
          ...currentCfg,
          syncEnabled: nodePlan.syncEnabled,
          syncRole: nodePlan.syncRole,
          syncToken: nodePlan.syncToken,
          groupLabel: nodePlan.groupLabel,
        });
      }

      configSuccess = `Sync group applied successfully to ${Object.keys(plan).length} nodes!`;
      setTimeout(() => {
        showConfigureModal = false;
        configSuccess = '';
      }, 1500);
      await refreshCluster();
    } catch (err: any) {
      configError = `Failed to apply cluster configuration: ${err?.message || err}`;
    } finally {
      configSaving = false;
    }
  }

  async function handleDeployShow() {
    deployError = '';
    if (!deploySourceIp) {
      deployError = 'Please select a source node.';
      return;
    }
    if (deployTargetIps.length === 0) {
      deployError = 'Please select at least one target node.';
      return;
    }

    deployRunning = true;
    try {
      await deployShowToNodes(
        deploySourceIp,
        deployTargetIps,
        deployOptions,
        (progress) => {
          deployResults = { ...progress };
        }
      );
      await refreshCluster();
    } catch (err: any) {
      deployError = `Deployment error: ${err?.message || err}`;
    } finally {
      deployRunning = false;
    }
  }

  async function handleTriggerSceneOnLeader() {
    if (!leaderNodeData?.online) return;
    sceneTriggerPending = true;
    controlFeedback = '';
    try {
      await activateScene(leaderNodeData.node.ip, selectedSceneIndex);
      controlFeedback = `Scene ${selectedSceneIndex} triggered on Leader (${leaderNodeData.node.name || leaderNodeData.node.ip}). Follower nodes will synchronize automatically via NodeSync.`;
    } catch (err: any) {
      controlFeedback = `Error triggering scene: ${err?.message || err}`;
    } finally {
      sceneTriggerPending = false;
    }
  }

  async function handleToggleTimelineOnLeader(enable: boolean) {
    if (!leaderNodeData?.online) return;
    timelineTriggerPending = true;
    controlFeedback = '';
    try {
      const currentSeq = await getSequence(leaderNodeData.node.ip);
      await saveSequence(leaderNodeData.node.ip, {
        ...currentSeq,
        enabled: enable,
      });
      leaderTimeline = { ...currentSeq, enabled: enable };
      controlFeedback = `Timeline ${enable ? 'Started' : 'Stopped'} on Leader. Followers are synchronized against shared clock & BPM.`;
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error updating timeline: ${err?.message || err}`;
    } finally {
      timelineTriggerPending = false;
    }
  }
</script>

<div class="sync-root">
  <!-- Disclaimer Notice -->
  <div class="sync-cue-banner" role="note">
    <span class="sync-banner-icon">ℹ</span>
    <span class="sync-banner-text">
      Cue Lists can be deployed across the cluster, but synchronized live Cue transport is not supported in Sync v1. Use synchronized Scene or Timeline transport for frame-locked multi-node playback.
    </span>
  </div>

  <!-- Header & Actions -->
  <header class="sync-header">
    <div class="sync-header-info">
      <div class="sync-badge-row">
        <span class="sync-tag">FLEET SYNC STUDIO</span>
        {#if isClusterReady}
          <span class="sync-status-pill sync-ready">● READY FOR SYNC</span>
        {:else if onlineCount < totalCount}
          <span class="sync-status-pill sync-warn">● NODES OFFLINE</span>
        {:else if showParityCount < totalCount}
          <span class="sync-status-pill sync-warn">● SHOW CONTENT DRIFT</span>
        {:else}
          <span class="sync-status-pill sync-neutral">● INITIALIZING</span>
        {/if}
      </div>
      <h1 class="sync-title">Multi-Node Cluster Management</h1>
      <p class="sync-desc">
        Real-time telemetry, bounded clock discipline, and show content parity across distributed LumiGrid nodes.
      </p>
    </div>

    <div class="sync-actions">
      {#if isAdmin}
        <button class="sync-action-btn sync-btn-secondary" onclick={() => (showConfigureModal = true)}>
          ⚙ Configure Group
        </button>
        <button class="sync-action-btn sync-btn-primary" onclick={() => (showDeployModal = true)}>
          ⇪ Deploy Show Content
        </button>
      {/if}
      <button class="sync-action-btn sync-btn-icon" onclick={refreshCluster} title="Refresh cluster status">
        ↻
      </button>
    </div>
  </header>

  <!-- Section 1: Cluster Summary -->
  <section class="sync-summary-grid" aria-label="Cluster Metrics Summary">
    <div class="sync-metric-tile">
      <div class="sync-metric-label">LEADER NODE</div>
      <div class="sync-metric-val highlight">
        {leaderNodeData?.node.name || leaderNodeData?.node.ip || 'No Leader'}
      </div>
      <div class="sync-metric-sub">
        {leaderNodeData?.node.ip ?? '—'}
      </div>
    </div>

    <div class="sync-metric-tile">
      <div class="sync-metric-label">CLUSTER ONLINE</div>
      <div class="sync-metric-val">
        {onlineCount} <span class="sync-metric-denom">/ {totalCount}</span>
      </div>
      <div class="sync-metric-sub">
        {onlineCount === totalCount ? 'All nodes reachable' : `${totalCount - onlineCount} node(s) offline`}
      </div>
    </div>

    <div class="sync-metric-tile">
      <div class="sync-metric-label">CLOCK LOCKED</div>
      <div class="sync-metric-val" class:success={clockLockedCount === onlineCount && onlineCount > 0}>
        {clockLockedCount} <span class="sync-metric-denom">/ {onlineCount}</span>
      </div>
      <div class="sync-metric-sub">
        Worst offset: {worstOffsetMs >= 0 ? `+${worstOffsetMs}` : worstOffsetMs} ms · Jitter: {worstJitterUs} µs
      </div>
    </div>

    <div class="sync-metric-tile">
      <div class="sync-metric-label">SHOW PARITY</div>
      <div class="sync-metric-val" class:success={showParityCount === onlineCount && onlineCount > 0} class:warn={showParityCount < onlineCount}>
        {showParityCount} <span class="sync-metric-denom">/ {onlineCount}</span>
      </div>
      <div class="sync-metric-sub">
        {showParityCount === onlineCount ? 'All show hashes match' : 'Content drift detected'}
      </div>
    </div>

    <div class="sync-metric-tile">
      <div class="sync-metric-label">ACTIVE TRANSPORT</div>
      <div class="sync-metric-val mode">
        {clusterTransport}
      </div>
      <div class="sync-metric-sub">
        Worst RTT: {worstRttMs} ms
      </div>
    </div>
  </section>

  <!-- Section 2: Restrained Topology Visualizer -->
  <section class="sync-section-card" aria-label="Sync Topology Map">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">Sync Topology</h2>
        <p class="sync-section-desc">UDP clock discipline hierarchy and peer latency.</p>
      </div>
    </div>

    <div class="sync-topology-container">
      {#if leaderNodeData}
        <div class="sync-topo-leader">
          <div class="sync-topo-node-badge is-leader">
            <div class="sync-topo-icon">★</div>
            <div class="sync-topo-details">
              <span class="sync-topo-name">{leaderNodeData.node.name || 'Leader Node'}</span>
              <span class="sync-topo-ip">{leaderNodeData.node.ip}</span>
            </div>
            <span class="sync-role-pill leader-pill">LEADER</span>
          </div>
        </div>

        <div class="sync-topo-bus">
          <div class="sync-topo-line"></div>
        </div>

        <div class="sync-topo-followers">
          {#if followers.length === 0}
            <div class="sync-topo-empty">No active followers registered. Cluster is operating in single-node mode.</div>
          {:else}
            {#each followers as follower}
              <div class="sync-topo-follower-card">
                <div class="sync-topo-drop-line"></div>
                <div class="sync-topo-node-badge">
                  <div class="sync-topo-details">
                    <span class="sync-topo-name">{follower.node.name || 'Follower'}</span>
                    <span class="sync-topo-ip">{follower.node.ip}</span>
                  </div>
                  <div class="sync-topo-stats">
                    <span class="sync-pill-tag">
                      {follower.syncStatus?.clockOffsetMs !== undefined
                        ? (follower.syncStatus.clockOffsetMs >= 0 ? `+${follower.syncStatus.clockOffsetMs}ms` : `${follower.syncStatus.clockOffsetMs}ms`)
                        : '—'}
                    </span>
                    <span class="sync-pill-tag rtt">
                      {follower.syncStatus?.peers?.[0]?.latencyMs !== undefined
                        ? `${follower.syncStatus.peers[0].latencyMs}ms RTT`
                        : '—'}
                    </span>
                  </div>
                </div>
              </div>
            {/each}
          {/if}
        </div>
      {:else}
        <div class="sync-empty-state">No cluster leader identified. Configure nodes to form a sync group.</div>
      {/if}
    </div>
  </section>

  <!-- Section 3: Synchronized Playback Control (Gated on Parity) -->
  <section class="sync-section-card" aria-label="Synchronized Cluster Controls">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">Synchronized Cluster Playback</h2>
        <p class="sync-section-desc">Commands are sent to the Leader only; followers derive output locally via NodeSync.</p>
      </div>
      {#if !isClusterReady}
        <div class="sync-gate-notice">
          ⚠ Show content or clock lock not ready across all nodes. Align content before starting synchronized playback.
        </div>
      {/if}
    </div>

    {#if controlFeedback}
      <div class="sync-control-banner">{controlFeedback}</div>
    {/if}

    <div class="sync-control-row">
      <!-- Scene trigger -->
      <div class="sync-control-block">
        <label for="scene-select" class="sync-ctrl-label">Synchronized Scene</label>
        <div class="sync-ctrl-input-group">
          <select id="scene-select" class="sync-ctrl-select" bind:value={selectedSceneIndex} disabled={!isClusterReady}>
            {#if availablePresets.length === 0}
              <option value={0}>Scene 0 (Default)</option>
            {:else}
              {#each availablePresets as p}
                <option value={p.index}>Scene {String(p.index).padStart(2, '0')}: {p.name}</option>
              {/each}
            {/if}
          </select>
          <button
            class="sync-action-btn sync-btn-primary"
            disabled={!isClusterReady || sceneTriggerPending}
            onclick={handleTriggerSceneOnLeader}
          >
            {sceneTriggerPending ? 'Activating…' : 'Activate on Sync Group'}
          </button>
        </div>
      </div>

      <!-- Timeline trigger -->
      <div class="sync-control-block">
        <span class="sync-ctrl-label">Synchronized Timeline</span>
        <div class="sync-ctrl-input-group">
          <div class="sync-timeline-status-badge">
            BPM: {leaderTimeline?.bpm ?? 120} · {leaderTimeline?.enabled ? 'RUNNING' : 'STOPPED'}
          </div>
          {#if leaderTimeline?.enabled}
            <button
              class="sync-action-btn sync-btn-secondary"
              disabled={!isClusterReady || timelineTriggerPending}
              onclick={() => handleToggleTimelineOnLeader(false)}
            >
              Stop Timeline
            </button>
          {:else}
            <button
              class="sync-action-btn sync-btn-primary"
              disabled={!isClusterReady || timelineTriggerPending}
              onclick={() => handleToggleTimelineOnLeader(true)}
            >
              Start Timeline
            </button>
          {/if}
        </div>
      </div>
    </div>
  </section>

  <!-- Section 4: Fleet Member Detail List -->
  <section class="sync-section-card" aria-label="Fleet Nodes Parity and Clock Status">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">Fleet Nodes & Parity</h2>
        <p class="sync-section-desc">Individual node clock locks, network jitter, and manifest parity.</p>
      </div>
    </div>

    <div class="sync-node-list">
      {#each nodes as node}
        {@const data = fleetData[node.ip]}
        <div class="sync-node-row" class:offline={!data?.online}>
          <div class="sync-node-main">
            <div class="sync-node-title-row">
              <span class="sync-node-name">{node.name || 'Unnamed Node'}</span>
              <span class="sync-node-ip">{node.ip}</span>
              {#if data?.syncStatus?.activeRole === 'master'}
                <span class="sync-role-pill leader-pill">LEADER</span>
              {:else if data?.syncStatus?.activeRole === 'worker'}
                <span class="sync-role-pill follower-pill">FOLLOWER</span>
              {:else}
                <span class="sync-role-pill standalone-pill">STANDALONE</span>
              {/if}
            </div>

            <div class="sync-node-metrics">
              {#if data?.online}
                <span class="sync-metric-item">
                  Clock:
                  {#if data.syncStatus?.clockLocked}
                    <strong class="text-success">Locked</strong>
                  {:else}
                    <strong class="text-warn">Drifting</strong>
                  {/if}
                </span>

                <span class="sync-metric-item">
                  Offset:
                  <strong>
                    {data.syncStatus?.clockOffsetMs !== undefined
                      ? (data.syncStatus.clockOffsetMs >= 0 ? `+${data.syncStatus.clockOffsetMs}ms` : `${data.syncStatus.clockOffsetMs}ms`)
                      : '—'}
                  </strong>
                </span>

                <span class="sync-metric-item">
                  Jitter: <strong>{data.syncStatus?.clockJitterUs ?? 0}µs</strong>
                </span>

                {#if data.syncStatus?.activeRole === 'worker' && data.syncStatus.masterAddress}
                  <span class="sync-metric-item">
                    Leader: <strong>{data.syncStatus.masterAddress}</strong>
                  </span>
                {/if}
              {:else}
                <span class="sync-metric-item text-warn">Node Unreachable ({data?.error || 'Offline'})</span>
              {/if}
            </div>
          </div>

          <div class="sync-node-parity">
            {#if !data?.online}
              <span class="sync-parity-badge offline">OFFLINE</span>
            {:else if data.parity?.status === 'MATCHED'}
              <span class="sync-parity-badge matched">MATCHED</span>
            {:else if data.parity?.status === 'DRIFT'}
              <div class="sync-drift-col">
                <span class="sync-parity-badge drift">SHOW DRIFT</span>
                <button class="sync-diff-link" onclick={() => (diffTargetNode = data)}>
                  Inspect Diff
                </button>
              </div>
            {:else if data.parity?.status === 'INCOMPATIBLE'}
              <span class="sync-parity-badge incompatible" title={data.parity.reason}>INCOMPATIBLE</span>
            {:else}
              <span class="sync-parity-badge unavailable">UNAVAILABLE</span>
            {/if}
          </div>
        </div>
      {/each}
    </div>
  </section>
</div>

<!-- Modal 1: Configure Sync Group (Admin Only) -->
{#if showConfigureModal}
  <div class="sync-modal-backdrop" onclick={() => (showConfigureModal = false)}>
    <div class="sync-modal-box" onclick={(e) => e.stopPropagation()}>
      <div class="sync-modal-header">
        <h3 class="sync-modal-title">Configure Sync Group</h3>
        <button class="sync-modal-close" onclick={() => (showConfigureModal = false)}>✕</button>
      </div>

      <div class="sync-modal-body">
        {#if configError}
          <div class="sync-modal-alert error">{configError}</div>
        {/if}
        {#if configSuccess}
          <div class="sync-modal-alert success">{configSuccess}</div>
        {/if}

        <div class="sync-form-group">
          <label for="cfg-group-name" class="sync-form-label">Cluster Group Label</label>
          <input
            id="cfg-group-name"
            type="text"
            class="sync-form-input"
            bind:value={configGroupName}
            placeholder="e.g. Main Façade"
          />
        </div>

        <div class="sync-form-group">
          <span class="sync-form-label">Election Mode</span>
          <div class="sync-radio-group">
            <label class="sync-radio-opt">
              <input type="radio" bind:group={configMode} value="auto" />
              <span>Automatic Election (Nodes elect highest priority leader)</span>
            </label>
            <label class="sync-radio-opt">
              <input type="radio" bind:group={configMode} value="explicit" />
              <span>Explicit Leader (Designate one authoritative master node)</span>
            </label>
          </div>
        </div>

        {#if configMode === 'explicit'}
          <div class="sync-form-group">
            <label for="cfg-explicit-leader" class="sync-form-label">Designated Leader Node</label>
            <select id="cfg-explicit-leader" class="sync-form-select" bind:value={configLeaderIp}>
              {#each nodes as n}
                <option value={n.ip}>{n.name || n.ip} ({n.ip})</option>
              {/each}
            </select>
          </div>
        {/if}

        <div class="sync-form-group">
          <span class="sync-form-label">Cluster Member Nodes</span>
          <div class="sync-checkbox-list">
            {#each nodes as n}
              <label class="sync-checkbox-opt">
                <input
                  type="checkbox"
                  checked={configSelectedIps.includes(n.ip)}
                  onchange={(e) => {
                    const target = e.currentTarget;
                    if (target.checked) {
                      configSelectedIps = [...configSelectedIps, n.ip];
                    } else {
                      configSelectedIps = configSelectedIps.filter((ip) => ip !== n.ip);
                    }
                  }}
                />
                <span>{n.name || n.ip} ({n.ip})</span>
              </label>
            {/each}
          </div>
        </div>

        <p class="sync-form-hint">
          A cryptographically secure cluster sync token will be generated and distributed to all selected nodes.
        </p>
      </div>

      <div class="sync-modal-footer">
        <button class="sync-action-btn sync-btn-secondary" onclick={() => (showConfigureModal = false)}>
          Cancel
        </button>
        <button
          class="sync-action-btn sync-btn-primary"
          disabled={configSaving}
          onclick={handleSaveClusterConfig}
        >
          {configSaving ? 'Applying to Fleet…' : 'Save & Distribute Sync Config'}
        </button>
      </div>
    </div>
  </div>
{/if}

<!-- Modal 2: Deploy Show Content (Admin Only) -->
{#if showDeployModal}
  <div class="sync-modal-backdrop" onclick={() => !deployRunning && (showDeployModal = false)}>
    <div class="sync-modal-box sync-modal-lg" onclick={(e) => e.stopPropagation()}>
      <div class="sync-modal-header">
        <h3 class="sync-modal-title">Deploy Show Content Across Fleet</h3>
        <button class="sync-modal-close" disabled={deployRunning} onclick={() => (showDeployModal = false)}>✕</button>
      </div>

      <div class="sync-modal-body">
        {#if deployError}
          <div class="sync-modal-alert error">{deployError}</div>
        {/if}

        <div class="sync-form-group">
          <label for="dep-source" class="sync-form-label">Source Node (Golden Master)</label>
          <select id="dep-source" class="sync-form-select" bind:value={deploySourceIp} disabled={deployRunning}>
            {#each nodes as n}
              <option value={n.ip}>{n.name || n.ip} ({n.ip})</option>
            {/each}
          </select>
        </div>

        <div class="sync-form-group">
          <span class="sync-form-label">Target Nodes to Deploy</span>
          <div class="sync-checkbox-list">
            {#each nodes.filter((n) => n.ip !== deploySourceIp) as n}
              <label class="sync-checkbox-opt">
                <input
                  type="checkbox"
                  disabled={deployRunning}
                  checked={deployTargetIps.includes(n.ip)}
                  onchange={(e) => {
                    const target = e.currentTarget;
                    if (target.checked) {
                      deployTargetIps = [...deployTargetIps, n.ip];
                    } else {
                      deployTargetIps = deployTargetIps.filter((ip) => ip !== n.ip);
                    }
                  }}
                />
                <span>{n.name || n.ip} ({n.ip})</span>
              </label>
            {/each}
          </div>
        </div>

        <div class="sync-form-group">
          <span class="sync-form-label">Content Categories to Synchronize</span>
          <div class="sync-cat-grid">
            <label class="sync-checkbox-opt">
              <input type="checkbox" bind:checked={deployOptions.scenes} disabled={deployRunning} />
              <span>Scene Presets</span>
            </label>
            <label class="sync-checkbox-opt">
              <input type="checkbox" bind:checked={deployOptions.sceneMotion} disabled={deployRunning} />
              <span>Scene Motion Tracks</span>
            </label>
            <label class="sync-checkbox-opt">
              <input type="checkbox" bind:checked={deployOptions.timeline} disabled={deployRunning} />
              <span>Timeline Sequence</span>
            </label>
            <label class="sync-checkbox-opt">
              <input type="checkbox" bind:checked={deployOptions.cueLists} disabled={deployRunning} />
              <span>Cue Lists</span>
            </label>
            <label class="sync-checkbox-opt">
              <input type="checkbox" bind:checked={deployOptions.schedules} disabled={deployRunning} />
              <span>Calendar Schedules (Optional)</span>
            </label>
          </div>
        </div>

        <div class="sync-form-group">
          <label class="sync-checkbox-opt highlight-opt">
            <input type="checkbox" bind:checked={deployOptions.mirrorExactly} disabled={deployRunning} />
            <span><strong>Mirror Exactly:</strong> Delete obsolete presets or cue lists on targets not present on source. (Does not affect device hardware or networking).</span>
          </label>
        </div>

        {#if Object.keys(deployResults).length > 0}
          <div class="sync-deploy-progress-box">
            <h4 class="sync-progress-title">Deployment Progress</h4>
            {#each Object.entries(deployResults) as [ip, res]}
              <div class="sync-progress-row">
                <span class="sync-progress-ip">{ip}</span>
                <span class="sync-progress-state {res.state}">
                  {res.state.toUpperCase()} {res.currentAsset ? `· ${res.currentAsset}` : ''}
                </span>
                {#if res.error}
                  <span class="sync-progress-err">{res.error}</span>
                {/if}
              </div>
            {/each}
          </div>
        {/if}
      </div>

      <div class="sync-modal-footer">
        <button class="sync-action-btn sync-btn-secondary" disabled={deployRunning} onclick={() => (showDeployModal = false)}>
          Close
        </button>
        <button class="sync-action-btn sync-btn-primary" disabled={deployRunning} onclick={handleDeployShow}>
          {deployRunning ? 'Deploying Content…' : 'Start Deployment'}
        </button>
      </div>
    </div>
  </div>
{/if}

<!-- Modal 3: Show Diff Inspector -->
{#if diffTargetNode}
  <div class="sync-modal-backdrop" onclick={() => (diffTargetNode = null)}>
    <div class="sync-modal-box" onclick={(e) => e.stopPropagation()}>
      <div class="sync-modal-header">
        <h3 class="sync-modal-title">
          Show Content Diff: {diffTargetNode.node.name || diffTargetNode.node.ip}
        </h3>
        <button class="sync-modal-close" onclick={() => (diffTargetNode = null)}>✕</button>
      </div>

      <div class="sync-modal-body">
        <p class="sync-diff-desc">
          Comparing against current Leader ({leaderNodeData?.node.name || leaderNodeData?.node.ip}).
        </p>

        <div class="sync-diff-table">
          {#if diffTargetNode.parity?.diffs && diffTargetNode.parity.diffs.length > 0}
            {#each diffTargetNode.parity.diffs as diff}
              <div class="sync-diff-row state-{diff.state}">
                <span class="sync-diff-label">{diff.label}</span>
                <span class="sync-diff-status badge-{diff.state}">{diff.state.toUpperCase()}</span>
              </div>
            {/each}
          {:else}
            <div class="sync-diff-empty">No detailed difference items available.</div>
          {/if}
        </div>
      </div>

      <div class="sync-modal-footer">
        <button class="sync-action-btn sync-btn-secondary" onclick={() => (diffTargetNode = null)}>
          Done
        </button>
      </div>
    </div>
  </div>
{/if}

<style>
  /* ── Layout & Typography ───────────────────────────────────────── */
  .sync-root {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
    padding: 1.5rem;
    color: #e2e8f0;
    max-width: 1400px;
    margin: 0 auto;
  }

  .sync-cue-banner {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.85rem 1.25rem;
    background: rgba(16, 185, 129, 0.08);
    border: 1px solid rgba(16, 185, 129, 0.25);
    border-radius: 8px;
    font-size: 0.875rem;
    color: #a7f3d0;
  }

  .sync-banner-icon {
    font-weight: 700;
    color: #10b981;
  }

  .sync-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    flex-wrap: wrap;
    gap: 1rem;
    border-bottom: 1px solid #1e293b;
    padding-bottom: 1.25rem;
  }

  .sync-badge-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
  }

  .sync-tag {
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.08em;
    padding: 0.2rem 0.5rem;
    background: #1e293b;
    border-radius: 4px;
    color: #94a3b8;
  }

  .sync-status-pill {
    font-size: 0.75rem;
    font-weight: 600;
    padding: 0.2rem 0.6rem;
    border-radius: 9999px;
  }

  .sync-status-pill.sync-ready {
    background: rgba(16, 185, 129, 0.2);
    color: #34d399;
  }

  .sync-status-pill.sync-warn {
    background: rgba(245, 158, 11, 0.2);
    color: #fbbf24;
  }

  .sync-status-pill.sync-neutral {
    background: rgba(148, 163, 184, 0.2);
    color: #cbd5e1;
  }

  .sync-title {
    font-size: 1.75rem;
    font-weight: 700;
    color: #f8fafc;
    margin: 0;
  }

  .sync-desc {
    color: #94a3b8;
    margin: 0.25rem 0 0 0;
    font-size: 0.875rem;
  }

  .sync-actions {
    display: flex;
    gap: 0.75rem;
    align-items: center;
  }

  .sync-action-btn {
    font-size: 0.875rem;
    font-weight: 600;
    padding: 0.55rem 1rem;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .sync-btn-primary {
    background: #10b981;
    color: #022c22;
  }
  .sync-btn-primary:hover:not(:disabled) {
    background: #34d399;
  }
  .sync-btn-primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .sync-btn-secondary {
    background: #1e293b;
    color: #e2e8f0;
    border: 1px solid #334155;
  }
  .sync-btn-secondary:hover:not(:disabled) {
    background: #334155;
  }

  .sync-btn-icon {
    background: #1e293b;
    color: #e2e8f0;
    border: 1px solid #334155;
    padding: 0.55rem 0.85rem;
  }

  /* ── Metric Summary Tiles ───────────────────────────────────────── */
  .sync-summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 1rem;
  }

  .sync-metric-tile {
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .sync-metric-label {
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.05em;
    color: #64748b;
  }

  .sync-metric-val {
    font-size: 1.5rem;
    font-weight: 700;
    color: #f8fafc;
  }

  .sync-metric-val.highlight {
    color: #38bdf8;
    font-size: 1.25rem;
  }

  .sync-metric-val.success {
    color: #34d399;
  }

  .sync-metric-val.warn {
    color: #fbbf24;
  }

  .sync-metric-val.mode {
    color: #a78bfa;
  }

  .sync-metric-denom {
    font-size: 1rem;
    color: #64748b;
  }

  .sync-metric-sub {
    font-size: 0.75rem;
    color: #94a3b8;
  }

  /* ── Section Cards ─────────────────────────────────────────────── */
  .sync-section-card {
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .sync-card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem;
  }

  .sync-section-title {
    font-size: 1.1rem;
    font-weight: 600;
    color: #f1f5f9;
    margin: 0;
  }

  .sync-section-desc {
    font-size: 0.8rem;
    color: #94a3b8;
    margin: 0.2rem 0 0 0;
  }

  .sync-gate-notice {
    font-size: 0.8rem;
    color: #fbbf24;
    background: rgba(245, 158, 11, 0.1);
    padding: 0.4rem 0.75rem;
    border-radius: 6px;
    border: 1px solid rgba(245, 158, 11, 0.3);
  }

  /* ── Topology View ─────────────────────────────────────────────── */
  .sync-topology-container {
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 1.5rem 0;
  }

  .sync-topo-leader {
    display: flex;
    justify-content: center;
  }

  .sync-topo-node-badge {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.6rem 1rem;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 8px;
  }

  .sync-topo-node-badge.is-leader {
    border-color: #10b981;
    background: rgba(16, 185, 129, 0.1);
  }

  .sync-topo-icon {
    color: #10b981;
    font-size: 1.2rem;
  }

  .sync-topo-details {
    display: flex;
    flex-direction: column;
  }

  .sync-topo-name {
    font-size: 0.875rem;
    font-weight: 600;
    color: #f8fafc;
  }

  .sync-topo-ip {
    font-size: 0.75rem;
    color: #94a3b8;
  }

  .sync-role-pill {
    font-size: 0.65rem;
    font-weight: 700;
    padding: 0.2rem 0.45rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
  }

  .leader-pill {
    background: rgba(16, 185, 129, 0.2);
    color: #34d399;
  }

  .follower-pill {
    background: rgba(56, 189, 248, 0.2);
    color: #38bdf8;
  }

  .standalone-pill {
    background: rgba(148, 163, 184, 0.2);
    color: #94a3b8;
  }

  .sync-topo-bus {
    width: 80%;
    height: 24px;
    position: relative;
    margin: 0 auto;
  }

  .sync-topo-line {
    position: absolute;
    top: 50%;
    left: 10%;
    right: 10%;
    height: 2px;
    background: #334155;
  }

  .sync-topo-followers {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 1.5rem;
    width: 100%;
  }

  .sync-topo-follower-card {
    display: flex;
    flex-direction: column;
    align-items: center;
  }

  .sync-topo-drop-line {
    width: 2px;
    height: 16px;
    background: #334155;
  }

  .sync-topo-stats {
    display: flex;
    gap: 0.35rem;
  }

  .sync-pill-tag {
    font-size: 0.7rem;
    background: #0f172a;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    color: #34d399;
    font-family: monospace;
  }

  .sync-pill-tag.rtt {
    color: #38bdf8;
  }

  .sync-topo-empty,
  .sync-empty-state {
    color: #64748b;
    font-size: 0.85rem;
    padding: 1.5rem;
    text-align: center;
  }

  /* ── Synchronized Controls ─────────────────────────────────────── */
  .sync-control-row {
    display: flex;
    flex-wrap: wrap;
    gap: 1.5rem;
  }

  .sync-control-block {
    flex: 1 1 320px;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sync-ctrl-label {
    font-size: 0.8rem;
    font-weight: 600;
    color: #cbd5e1;
  }

  .sync-ctrl-input-group {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }

  .sync-ctrl-select {
    flex: 1;
    background: #1e293b;
    color: #f8fafc;
    border: 1px solid #334155;
    padding: 0.55rem;
    border-radius: 6px;
  }

  .sync-timeline-status-badge {
    padding: 0.55rem 0.75rem;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 6px;
    font-size: 0.8rem;
    color: #a78bfa;
    font-weight: 600;
  }

  .sync-control-banner {
    padding: 0.75rem 1rem;
    background: rgba(56, 189, 248, 0.1);
    border: 1px solid rgba(56, 189, 248, 0.3);
    border-radius: 6px;
    font-size: 0.85rem;
    color: #38bdf8;
  }

  /* ── Fleet List ────────────────────────────────────────────────── */
  .sync-node-list {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }

  .sync-node-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 0.85rem 1.25rem;
    flex-wrap: wrap;
    gap: 1rem;
  }

  .sync-node-row.offline {
    opacity: 0.6;
    border-color: #27272a;
  }

  .sync-node-main {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .sync-node-title-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .sync-node-name {
    font-size: 0.95rem;
    font-weight: 600;
    color: #f8fafc;
  }

  .sync-node-ip {
    font-size: 0.8rem;
    color: #94a3b8;
    font-family: monospace;
  }

  .sync-node-metrics {
    display: flex;
    gap: 1rem;
    font-size: 0.78rem;
    color: #94a3b8;
    flex-wrap: wrap;
  }

  .text-success {
    color: #34d399;
  }
  .text-warn {
    color: #fbbf24;
  }

  .sync-node-parity {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .sync-parity-badge {
    font-size: 0.75rem;
    font-weight: 700;
    padding: 0.3rem 0.7rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
  }

  .sync-parity-badge.matched {
    background: rgba(16, 185, 129, 0.2);
    color: #34d399;
  }

  .sync-parity-badge.drift {
    background: rgba(245, 158, 11, 0.2);
    color: #fbbf24;
  }

  .sync-parity-badge.incompatible {
    background: rgba(244, 63, 94, 0.2);
    color: #fb7185;
  }

  .sync-parity-badge.offline,
  .sync-parity-badge.unavailable {
    background: rgba(148, 163, 184, 0.2);
    color: #94a3b8;
  }

  .sync-drift-col {
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 0.25rem;
  }

  .sync-diff-link {
    background: none;
    border: none;
    color: #38bdf8;
    font-size: 0.75rem;
    text-decoration: underline;
    cursor: pointer;
    padding: 0;
  }

  /* ── Modals ────────────────────────────────────────────────────── */
  .sync-modal-backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.75);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 999;
    padding: 1rem;
  }

  .sync-modal-box {
    background: #0f172a;
    border: 1px solid #334155;
    border-radius: 8px;
    width: 100%;
    max-width: 520px;
    max-height: 90vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .sync-modal-lg {
    max-width: 680px;
  }

  .sync-modal-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem 1.25rem;
    border-bottom: 1px solid #1e293b;
  }

  .sync-modal-title {
    font-size: 1.1rem;
    font-weight: 600;
    color: #f8fafc;
    margin: 0;
  }

  .sync-modal-close {
    background: none;
    border: none;
    color: #94a3b8;
    font-size: 1.2rem;
    cursor: pointer;
  }

  .sync-modal-body {
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    overflow-y: auto;
  }

  .sync-modal-footer {
    display: flex;
    justify-content: flex-end;
    gap: 0.75rem;
    padding: 1rem 1.25rem;
    border-top: 1px solid #1e293b;
    background: #090e1a;
  }

  .sync-modal-alert {
    padding: 0.75rem;
    border-radius: 6px;
    font-size: 0.85rem;
  }
  .sync-modal-alert.error {
    background: rgba(244, 63, 94, 0.15);
    border: 1px solid rgba(244, 63, 94, 0.3);
    color: #fb7185;
  }
  .sync-modal-alert.success {
    background: rgba(16, 185, 129, 0.15);
    border: 1px solid rgba(16, 185, 129, 0.3);
    color: #34d399;
  }

  .sync-form-group {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .sync-form-label {
    font-size: 0.8rem;
    font-weight: 600;
    color: #cbd5e1;
  }

  .sync-form-input,
  .sync-form-select {
    background: #1e293b;
    color: #f8fafc;
    border: 1px solid #334155;
    padding: 0.55rem;
    border-radius: 6px;
  }

  .sync-form-hint {
    font-size: 0.75rem;
    color: #94a3b8;
    margin: 0;
  }

  .sync-radio-group,
  .sync-checkbox-list {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sync-cat-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.5rem;
  }

  .sync-radio-opt,
  .sync-checkbox-opt {
    display: flex;
    align-items: flex-start;
    gap: 0.5rem;
    font-size: 0.85rem;
    color: #cbd5e1;
    cursor: pointer;
  }

  .highlight-opt {
    padding: 0.75rem;
    background: rgba(245, 158, 11, 0.08);
    border: 1px solid rgba(245, 158, 11, 0.25);
    border-radius: 6px;
  }

  /* ── Diff Inspector Table ──────────────────────────────────────── */
  .sync-diff-desc {
    font-size: 0.85rem;
    color: #94a3b8;
    margin: 0;
  }

  .sync-diff-table {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    max-height: 400px;
    overflow-y: auto;
  }

  .sync-diff-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.55rem 0.85rem;
    background: #1e293b;
    border-radius: 4px;
    font-size: 0.825rem;
  }

  .sync-diff-row.state-missing {
    border-left: 3px solid #fb7185;
  }
  .sync-diff-row.state-changed {
    border-left: 3px solid #fbbf24;
  }
  .sync-diff-row.state-extra {
    border-left: 3px solid #38bdf8;
  }
  .sync-diff-row.state-match {
    border-left: 3px solid #34d399;
  }

  .badge-missing {
    color: #fb7185;
    font-weight: 700;
  }
  .badge-changed {
    color: #fbbf24;
    font-weight: 700;
  }
  .badge-extra {
    color: #38bdf8;
    font-weight: 700;
  }
  .badge-match {
    color: #34d399;
    font-weight: 600;
  }

  .sync-deploy-progress-box {
    background: #090e1a;
    border: 1px solid #1e293b;
    border-radius: 6px;
    padding: 0.85rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sync-progress-title {
    font-size: 0.8rem;
    font-weight: 700;
    color: #94a3b8;
    margin: 0;
  }

  .sync-progress-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.8rem;
    padding: 0.25rem 0;
  }

  .sync-progress-ip {
    font-family: monospace;
    color: #e2e8f0;
  }

  .sync-progress-state.complete {
    color: #34d399;
    font-weight: 700;
  }
  .sync-progress-state.deploying,
  .sync-progress-state.verifying {
    color: #38bdf8;
  }
  .sync-progress-state.partial,
  .sync-progress-state.failed {
    color: #fb7185;
    font-weight: 700;
  }
</style>
