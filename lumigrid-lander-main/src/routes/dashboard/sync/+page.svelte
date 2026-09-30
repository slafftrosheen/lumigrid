<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import OperatorPanelDialog from '$lib/components/dashboard/OperatorPanelDialog.svelte';
  import {
    getSavedNodes,
    getSelectedNodeIp,
    NODE_REGISTRY_CHANGED_EVENT,
    type SavedNodeInfo,
  } from '$lib/auth/storage';
  import {
    getSyncStatus,
    getShowManifest,
    comparePlaybackManifest,
    compareScheduleManifest,
    compareFullShowManifest,
    evaluateClusterTopology,
    generateSyncToken,
    buildClusterPlan,
    type SyncStatus,
    type ShowManifest,
    type ShowParityResult,
    type ClusterTopology,
    type ReachabilityState,
  } from '$lib/api/sync';
  import {
    deployShowToNodes,
    type ShowDeploymentOptions,
    type ShowDeploymentNodeResult,
    DEFAULT_SHOW_DEPLOYMENT_OPTIONS,
  } from '$lib/api/showDeployment';
  import { getDeviceConfig, saveDeviceConfig } from '$lib/api/config';
  import { getPresets, type ScenePreset } from '$lib/api/scenes';
  import { getSequence, type Sequence } from '$lib/api/timeline';
  import {
    getCueLists,
    type CueListSummary,
  } from '$lib/api/cues';
  import {
    getShowTransportStatus,
    runScene,
    startTimeline,
    stopTimeline,
    startCueList,
    stopCue,
    pauseCue,
    resumeCue,
    nextCue,
    previousCue,
    jumpCue,
    setCueCrossfade,
    type ShowTransportStatus,
  } from '$lib/api/showTransport';

  interface FleetNodeSyncData {
    node: SavedNodeInfo;
    online: boolean;
    reachabilityState: ReachabilityState;
    syncStatus?: SyncStatus;
    manifest?: ShowManifest;
    playbackParity?: ShowParityResult;
    scheduleParity?: ShowParityResult;
    error?: string;
  }

  let nodes = $state<SavedNodeInfo[]>([]);
  let fleetData = $state<Record<string, FleetNodeSyncData>>({});
  let loadedLeaderIp = $state<string>('');
  let availablePresets = $state<ScenePreset[]>([]);
  let leaderTimeline = $state<Sequence | null>(null);
  let availableCueLists = $state<CueListSummary[]>([]);
  let leaderShowTransport = $state<ShowTransportStatus | null>(null);

  let refreshGeneration = 0;
  let loading = $state(false);
  let pollInterval: ReturnType<typeof setInterval> | null = null;
  let isAdmin = $state(false);

  // Group filtering
  let selectedGroupLabel = $state<string>('All');

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
  let configCryptoError = $state('');

  // Deploy Show form state
  let deploySourceIp = $state('');
  let deployTargetIps = $state<string[]>([]);
  let deployOptions = $state<ShowDeploymentOptions>({ ...DEFAULT_SHOW_DEPLOYMENT_OPTIONS });
  let deployRunning = $state(false);
  let deployResults = $state<Record<string, ShowDeploymentNodeResult>>({});
  let deployError = $state('');

  // Synchronized Transport Control state
  let activeTransportTab = $state<'scene' | 'timeline' | 'cue'>('scene');
  let selectedSceneIndex = $state<number>(0);
  let sceneTriggerPending = $state(false);
  let timelineTriggerPending = $state(false);

  // Cue transport state
  let selectedCueListIndex = $state<number>(0);
  let cueJumpIndex = $state<number>(0);
  let manualCrossfadeValue = $state<number>(0);
  let manualCrossfadeThrottler: ReturnType<typeof setTimeout> | null = null;
  let cueTriggerPending = $state(false);

  let controlFeedback = $state('');

  // Available groups detected across fleet
  let availableGroups = $derived.by(() => {
    const labels = new Set<string>();
    for (const d of Object.values(fleetData)) {
      if (d.syncStatus?.groupLabel) {
        labels.add(d.syncStatus.groupLabel);
      }
    }
    return Array.from(labels).sort();
  });

  // Partition nodes by active group
  let activeGroupNodes = $derived.by(() => {
    const all = Object.values(fleetData);
    if (selectedGroupLabel === 'All') return all;
    return all.filter((d) => d.syncStatus?.groupLabel === selectedGroupLabel);
  });

  let otherFleetNodes = $derived.by(() => {
    if (selectedGroupLabel === 'All') return [];
    return Object.values(fleetData).filter((d) => d.syncStatus?.groupLabel !== selectedGroupLabel);
  });

  let onlineNodes = $derived(activeGroupNodes.filter((d) => d.online));
  let totalCount = $derived(activeGroupNodes.length);
  let onlineCount = $derived(onlineNodes.length);
  let clockLockedCount = $derived(onlineNodes.filter((d) => d.syncStatus?.clockLocked).length);

  // Evaluated topology across active group ONLY
  let clusterTopology = $derived.by(() => {
    const groupData = activeGroupNodes.filter((d) => d.online && d.syncStatus);
    return evaluateClusterTopology(
      groupData.map((d) => ({ ip: d.node.ip, status: d.syncStatus }))
    );
  });

  let leaderIp = $derived(clusterTopology.leaderIp || '');
  let leaderNodeData = $derived(leaderIp ? fleetData[leaderIp] || null : null);

  let playbackParityCount = $derived(
    onlineNodes.filter((d) => d.playbackParity?.status === 'MATCHED').length
  );
  let scheduleParityCount = $derived(
    onlineNodes.filter((d) => d.scheduleParity?.status === 'MATCHED').length
  );

  let clusterTransport = $derived.by(() => {
    if (leaderShowTransport?.mode) {
      return leaderShowTransport.mode.toUpperCase();
    }
    if (leaderNodeData?.syncStatus?.transport) {
      return leaderNodeData.syncStatus.transport.toUpperCase();
    }
    return 'STANDALONE';
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

  // Cluster Ready Gate:
  // 1. All nodes in active group are online
  // 2. Topology is VALID_LEADER and ready (no split-brain, no leader disagreement)
  // 3. All followers are sync running and clock locked
  // 4. Playback parity is MATCHED (schedule differences do not gate group playback)
  let isClusterReady = $derived.by(() => {
    if (totalCount === 0 || onlineCount < totalCount) return false;
    if (clusterTopology.state !== 'VALID_LEADER' || !clusterTopology.isReady) return false;
    if (!leaderNodeData?.online || leaderNodeData.syncStatus?.activeRole !== 'master') return false;
    for (const d of onlineNodes) {
      if (!d.syncStatus?.running || !d.syncStatus.clockLocked) return false;
      if (d.playbackParity?.status !== 'MATCHED') return false;
    }
    return true;
  });

  let followers = $derived(
    onlineNodes.filter((d) => d.node.ip !== leaderIp)
  );

  function handleRegistryChange() {
    refreshCluster();
  }

  onMount(async () => {
    nodes = getSavedNodes();
    if (nodes.length > 0) {
      configSelectedIps = nodes.map((n) => n.ip);
      configLeaderIp = nodes[0].ip;
      deploySourceIp = nodes[0].ip;
      deployTargetIps = nodes.slice(1).map((n) => n.ip);
    }
    if (typeof window !== 'undefined') {
      window.addEventListener(NODE_REGISTRY_CHANGED_EVENT, handleRegistryChange);
    }
    await refreshCluster();
    pollInterval = setInterval(refreshCluster, 3000);
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
    if (manualCrossfadeThrottler) clearTimeout(manualCrossfadeThrottler);
    if (typeof window !== 'undefined') {
      window.removeEventListener(NODE_REGISTRY_CHANGED_EVENT, handleRegistryChange);
    }
  });

  async function loadLeaderAssets(ip: string) {
    try {
      const [presets, seq, cues, transportStatus] = await Promise.all([
        getPresets(ip).catch(() => []),
        getSequence(ip).catch(() => null),
        getCueLists(ip).catch(() => []),
        getShowTransportStatus(ip).catch(() => null),
      ]);
      if (loadedLeaderIp === ip) {
        availablePresets = presets;
        leaderTimeline = seq;
        availableCueLists = cues;
        leaderShowTransport = transportStatus;
        if (cues.length > 0 && selectedCueListIndex === 0) {
          selectedCueListIndex = cues[0].index;
        }
      }
    } catch {
      // Ignored in background refresh
    }
  }

  async function refreshCluster() {
    const myGen = ++refreshGeneration;
    nodes = getSavedNodes();
    if (nodes.length === 0) {
      fleetData = {};
      return;
    }

    const nextData: Record<string, FleetNodeSyncData> = {};
    const queue = [...nodes];
    const concurrency = 4;

    async function fetchNode(item: SavedNodeInfo) {
      try {
        let syncStatus: SyncStatus | null = null;
        let manifest: ShowManifest | null = null;
        let reachabilityState: ReachabilityState = 'OFFLINE';

        try {
          syncStatus = await getSyncStatus(item.ip);
        } catch {
          syncStatus = null;
        }

        if (syncStatus) {
          if (syncStatus.syncProtocol !== undefined && syncStatus.syncProtocol !== 5 && syncStatus.protocolVersion !== 5) {
            reachabilityState = 'ONLINE / SYNC INCOMPATIBLE';
          } else if (!syncStatus.syncEnabled) {
            reachabilityState = 'ONLINE / SYNC DISABLED';
          } else {
            try {
              manifest = await getShowManifest(item.ip);
            } catch {
              manifest = null;
            }
            if (!manifest) {
              reachabilityState = 'ONLINE / MANIFEST UNAVAILABLE';
            } else {
              reachabilityState = 'ONLINE';
            }
          }
          nextData[item.ip] = {
            node: item,
            online: true,
            reachabilityState,
            syncStatus,
            manifest: manifest || undefined,
          };
          return;
        }

        // Try probing basic device config to determine if node is reachable
        try {
          const cfg = await getDeviceConfig(item.ip);
          if (cfg) {
            reachabilityState = 'ONLINE / SYNC DISABLED';
            nextData[item.ip] = {
              node: item,
              online: true,
              reachabilityState,
              error: 'Node reachable, sync service disabled',
            };
            return;
          }
        } catch {
          // Node truly offline
        }

        nextData[item.ip] = {
          node: item,
          online: false,
          reachabilityState: 'OFFLINE',
          error: 'Node unreachable',
        };
      } catch (err: any) {
        nextData[item.ip] = {
          node: item,
          online: false,
          reachabilityState: 'OFFLINE',
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

    if (myGen !== refreshGeneration) return;

    // Evaluate cluster topology to find the real leader
    const onlineList = Object.values(nextData).filter((d) => d.online && d.syncStatus);
    const topo = evaluateClusterTopology(
      onlineList.map((d) => ({ ip: d.node.ip, status: d.syncStatus }))
    );
    const discoveredLeaderIp = topo.leaderIp;

    // Evaluate playback and schedule parity against authoritative leader
    if (discoveredLeaderIp && nextData[discoveredLeaderIp]?.manifest) {
      const leaderManifest = nextData[discoveredLeaderIp].manifest!;
      for (const data of Object.values(nextData)) {
        if (data.online && data.manifest) {
          data.playbackParity = comparePlaybackManifest(leaderManifest, data.manifest);
          data.scheduleParity = compareScheduleManifest(leaderManifest, data.manifest);
        } else {
          data.playbackParity = { playbackStatus: 'UNAVAILABLE', scheduleStatus: 'UNAVAILABLE', status: 'UNAVAILABLE', diffs: [] };
          data.scheduleParity = { playbackStatus: 'UNAVAILABLE', scheduleStatus: 'UNAVAILABLE', status: 'UNAVAILABLE', diffs: [] };
        }
      }
    }

    fleetData = nextData;

    // Leader change handling: reset leader cache and reload assets
    if (discoveredLeaderIp && discoveredLeaderIp !== loadedLeaderIp) {
      loadedLeaderIp = discoveredLeaderIp;
      availablePresets = [];
      leaderTimeline = null;
      availableCueLists = [];
      leaderShowTransport = null;
      await loadLeaderAssets(discoveredLeaderIp);
    } else if (discoveredLeaderIp && availablePresets.length === 0) {
      await loadLeaderAssets(discoveredLeaderIp);
    }
  }

  function openConfigureModal() {
    configCryptoError = '';
    configError = '';
    configSuccess = '';
    try {
      generateSyncToken();
    } catch (err: any) {
      configCryptoError = 'Web Crypto is not available in this browser. Secure sync token cannot be generated.';
    }
    showConfigureModal = true;
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
    if (!leaderIp || !isClusterReady) return;
    sceneTriggerPending = true;
    controlFeedback = '';
    try {
      const res = await runScene(leaderIp, selectedSceneIndex);
      if (res.accepted) {
        controlFeedback = `Scene ${selectedSceneIndex + 1} activated via Show Transport on Leader (${leaderNodeData?.node.name || leaderIp}). Followers locked via NodeSync.`;
      } else {
        controlFeedback = `Transport response: ${res.error || 'rejected'}`;
      }
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error triggering scene: ${err?.message || err}`;
    } finally {
      sceneTriggerPending = false;
    }
  }

  async function handleToggleTimelineOnLeader(enable: boolean) {
    if (!leaderIp || !isClusterReady) return;
    timelineTriggerPending = true;
    controlFeedback = '';
    try {
      const res = enable ? await startTimeline(leaderIp) : await stopTimeline(leaderIp);
      if (res.accepted) {
        controlFeedback = `Timeline ${enable ? 'Started' : 'Stopped'} via Show Transport on Leader. Followers synchronized against shared BPM.`;
      } else {
        controlFeedback = `Transport response: ${res.error || 'rejected'}`;
      }
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error updating timeline: ${err?.message || err}`;
    } finally {
      timelineTriggerPending = false;
    }
  }

  async function handleStartCueList() {
    if (!leaderIp || !isClusterReady) return;
    cueTriggerPending = true;
    controlFeedback = '';
    try {
      const res = await startCueList(leaderIp, selectedCueListIndex);
      controlFeedback = res.accepted ? `Cue List #${selectedCueListIndex + 1} started on Leader.` : `Error: ${res.error || 'rejected'}`;
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error starting cue: ${err?.message || err}`;
    } finally {
      cueTriggerPending = false;
    }
  }

  async function handleStopCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await stopCue(leaderIp);
      controlFeedback = 'Cue playback stopped.';
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error stopping cue: ${err?.message || err}`;
    }
  }

  async function handlePauseCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await pauseCue(leaderIp);
      controlFeedback = 'Cue playback paused.';
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error pausing cue: ${err?.message || err}`;
    }
  }

  async function handleResumeCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await resumeCue(leaderIp);
      controlFeedback = 'Cue playback resumed.';
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error resuming cue: ${err?.message || err}`;
    }
  }

  async function handleNextCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await nextCue(leaderIp);
      controlFeedback = 'Advanced to next cue.';
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error stepping cue: ${err?.message || err}`;
    }
  }

  async function handlePreviousCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await previousCue(leaderIp);
      controlFeedback = 'Stepped to previous cue.';
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error stepping cue: ${err?.message || err}`;
    }
  }

  async function handleJumpCue() {
    if (!leaderIp || !isClusterReady) return;
    try {
      await jumpCue(leaderIp, cueJumpIndex);
      controlFeedback = `Jumped to cue #${cueJumpIndex + 1}.`;
      await refreshCluster();
    } catch (err: any) {
      controlFeedback = `Error jumping cue: ${err?.message || err}`;
    }
  }

  function handleManualCrossfade(e: Event) {
    if (!leaderIp || !isClusterReady) return;
    const target = e.target as HTMLInputElement;
    const val = parseFloat(target.value);
    manualCrossfadeValue = val;
    if (manualCrossfadeThrottler) return;
    manualCrossfadeThrottler = setTimeout(async () => {
      manualCrossfadeThrottler = null;
      try {
        await setCueCrossfade(leaderIp, manualCrossfadeValue);
      } catch {
        // Throttled
      }
    }, 100);
  }
</script>

<div class="sync-root">
  <!-- Cluster Topology Error & Conflict Banners -->
  {#if clusterTopology.state === 'NO_LEADER'}
    <div class="sync-alert-banner alert-warn" role="alert">
      <span class="sync-banner-icon">⚠</span>
      <div class="sync-banner-body">
        <strong>Topology Error: No Active Leader Detected</strong>
        <p>No node in the active group has reported role <code>master</code>. Group playback controls are suppressed until a leader is elected or explicitly configured.</p>
      </div>
    </div>
  {:else if clusterTopology.state === 'SPLIT_BRAIN'}
    <div class="sync-alert-banner alert-danger" role="alert">
      <span class="sync-banner-icon">⛔</span>
      <div class="sync-banner-body">
        <strong>Topology Conflict: Split Brain Detected</strong>
        <p>{clusterTopology.error}. Multiple nodes are broadcasting authority simultaneously. Group playback is locked to prevent hardware conflicts.</p>
      </div>
    </div>
  {:else if clusterTopology.state === 'LEADER_DISAGREEMENT'}
    <div class="sync-alert-banner alert-danger" role="alert">
      <span class="sync-banner-icon">⛔</span>
      <div class="sync-banner-body">
        <strong>Topology Conflict: Leader Disagreement</strong>
        <p>{clusterTopology.error}. Follower nodes are tracking different leader addresses.</p>
      </div>
    </div>
  {/if}

  {#if clusterTopology.state === 'VALID_LEADER' && !clusterTopology.isReady}
    <div class="sync-alert-banner alert-warn" role="alert">
      <span class="sync-banner-icon">⚠</span>
      <div class="sync-banner-body">
        <strong>Follower Transport Notice</strong>
        <p>{clusterTopology.error || 'One or more follower nodes reported transport apply error.'}</p>
      </div>
    </div>
  {/if}

  <!-- Header & Actions -->
  <header class="sync-header">
    <div class="sync-header-info">
      <div class="sync-badge-row">
        <span class="sync-tag">SHOW TRANSPORT v2</span>
        {#if isClusterReady}
          <span class="sync-status-pill sync-ready">● READY FOR SYNC</span>
        {:else if onlineCount < totalCount}
          <span class="sync-status-pill sync-warn">● NODES OFFLINE</span>
        {:else if playbackParityCount < totalCount}
          <span class="sync-status-pill sync-warn">● PLAYBACK CONTENT DRIFT</span>
        {:else if clusterTopology.state !== 'VALID_LEADER'}
          <span class="sync-status-pill sync-danger">● TOPOLOGY CONFLICT</span>
        {:else}
          <span class="sync-status-pill sync-neutral">● INITIALIZING</span>
        {/if}

        {#if availableGroups.length > 0}
          <div class="sync-group-filter-wrap">
            <label for="group-filter-select" class="sync-filter-label">Group:</label>
            <select id="group-filter-select" class="sync-filter-select" bind:value={selectedGroupLabel}>
              <option value="All">All Groups ({Object.keys(fleetData).length} nodes)</option>
              {#each availableGroups as grp}
                <option value={grp}>{grp}</option>
              {/each}
            </select>
          </div>
        {/if}
      </div>
      <h1 class="sync-title">Multi-Node Cluster Management</h1>
      <p class="sync-desc">
        Unified Scene, Timeline, and Cue transport coordination across distributed LumiGrid nodes with authoritative clock discipline.
      </p>
    </div>

    <div class="sync-actions">
      {#if isAdmin}
        <button class="sync-action-btn sync-btn-secondary" onclick={openConfigureModal}>
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

  <!-- Section 1: Cluster Summary Tiles -->
  <section class="sync-summary-grid" aria-label="Cluster Metrics Summary">
    <div class="sync-metric-tile">
      <div class="sync-metric-label">LEADER NODE</div>
      <div class="sync-metric-val highlight">
        {leaderNodeData?.syncStatus?.activeRole === 'master' ? (leaderNodeData?.node.name || leaderNodeData?.node.ip) : (clusterTopology.state === 'NO_LEADER' ? 'Electing / None' : 'Conflict')}
      </div>
      <div class="sync-metric-sub">
        {leaderNodeData?.node.ip ?? (clusterTopology.state === 'SPLIT_BRAIN' ? 'Multiple Masters' : '—')}
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
      <div class="sync-metric-label">PLAYBACK PARITY</div>
      <div class="sync-metric-val" class:success={playbackParityCount === onlineCount && onlineCount > 0} class:warn={playbackParityCount < onlineCount}>
        {playbackParityCount} <span class="sync-metric-denom">/ {onlineCount}</span>
      </div>
      <div class="sync-metric-sub">
        {playbackParityCount === onlineCount ? 'Scenes, cues, & timeline match' : 'Playback drift detected'}
      </div>
    </div>

    <div class="sync-metric-tile">
      <div class="sync-metric-label">SCHEDULE PARITY</div>
      <div class="sync-metric-val" class:success={scheduleParityCount === onlineCount && onlineCount > 0} class:warn={scheduleParityCount < onlineCount}>
        {scheduleParityCount} <span class="sync-metric-denom">/ {onlineCount}</span>
      </div>
      <div class="sync-metric-sub">
        {scheduleParityCount === onlineCount ? 'Calendar schedules match' : 'Schedules differ (optional)'}
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

  <!-- Section 2: Topology Visualizer -->
  <section class="sync-section-card" aria-label="Sync Topology Map">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">Sync Topology & Protocol v4</h2>
        <p class="sync-section-desc">UDP clock discipline hierarchy and peer latency. Commands target Leader only.</p>
      </div>
    </div>

    <div class="sync-topology-container">
      {#if leaderNodeData && leaderNodeData.syncStatus?.activeRole === 'master'}
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
            <div class="sync-topo-empty">No active followers registered. Cluster operating in standalone leader mode.</div>
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
                    {#if follower.syncStatus?.transportApplyOk === false}
                      <span class="sync-pill-tag err" title={follower.syncStatus.transportError || 'Transport apply error'}>
                        APPLY ERR
                      </span>
                    {/if}
                  </div>
                </div>
              </div>
            {/each}
          {/if}
        </div>
      {:else}
        <div class="sync-empty-state">
          {clusterTopology.state === 'NO_LEADER'
            ? 'No cluster leader identified. Configure nodes to form a sync group.'
            : 'Cluster topology in conflict. Resolve node roles before proceeding.'}
        </div>
      {/if}
    </div>
  </section>

  <!-- Section 3: Show Transport v2 & Synchronized Playback Control -->
  <section class="sync-section-card" aria-label="Synchronized Cluster Controls">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">Synchronized Show Transport</h2>
        <p class="sync-section-desc">
          Unified transport controls sent to Leader ({leaderNodeData?.syncStatus?.activeRole === 'master' ? (leaderNodeData.node.name || leaderIp) : 'NO LEADER'}). Followers derive output locally via NodeSync protocol v4.
        </p>
      </div>

      <!-- Mode Selector Tabs -->
      <div class="transport-tabs">
        <button
          class="transport-tab-btn"
          class:active={activeTransportTab === 'scene'}
          class:is-active-mode={clusterTransport === 'SCENE'}
          onclick={() => (activeTransportTab = 'scene')}
        >
          Scene Transport
        </button>
        <button
          class="transport-tab-btn"
          class:active={activeTransportTab === 'timeline'}
          class:is-active-mode={clusterTransport === 'TIMELINE'}
          onclick={() => (activeTransportTab = 'timeline')}
        >
          Timeline Transport
        </button>
        <button
          class="transport-tab-btn"
          class:active={activeTransportTab === 'cue'}
          class:is-active-mode={clusterTransport === 'CUE'}
          onclick={() => (activeTransportTab = 'cue')}
        >
          Cue Playback
        </button>
      </div>

      {#if !isClusterReady}
        <div class="sync-gate-notice">
          {#if clusterTopology.state !== 'VALID_LEADER'}
            ⚠ Group transport locked due to topology state: {clusterTopology.state}.
          {:else}
            ⚠ Playback parity or clock lock not ready across all group members. Align content before starting synchronized playback.
          {/if}
        </div>
      {/if}
    </div>

    {#if controlFeedback}
      <div class="sync-control-banner">{controlFeedback}</div>
    {/if}

    <!-- TAB 1: SCENE TRANSPORT -->
    {#if activeTransportTab === 'scene'}
      <div class="sync-control-row">
        <div class="sync-control-block">
          <label for="scene-select" class="sync-ctrl-label">Synchronized Scene Activation</label>
          <div class="sync-ctrl-input-group">
            <select id="scene-select" class="sync-ctrl-select" bind:value={selectedSceneIndex} disabled={!isClusterReady}>
              {#if availablePresets.length === 0}
                <option value={-1} disabled selected>No scenes available</option>
              {:else}
                {#each availablePresets as p}
                  <option value={p.index}>Scene {String(p.index + 1).padStart(2, '0')}: {p.name}</option>
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
          <p class="sync-ctrl-hint">
            Acquires Scene Playback on leader and broadcasts scene index to all followers via NodeSync.
          </p>
        </div>
      </div>

    <!-- TAB 2: TIMELINE TRANSPORT -->
    {:else if activeTransportTab === 'timeline'}
      <div class="sync-control-row">
        <div class="sync-control-block">
          <span class="sync-ctrl-label">Synchronized Timeline Playback</span>
          <div class="sync-ctrl-input-group">
            <div class="sync-timeline-status-badge">
              BPM: {leaderShowTransport?.timeline?.bpm ?? leaderTimeline?.bpm ?? 120} · {leaderShowTransport?.timeline?.running ?? leaderTimeline?.enabled ? 'RUNNING' : 'STOPPED'}
            </div>
            {#if leaderShowTransport?.timeline?.running ?? leaderTimeline?.enabled}
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
          <p class="sync-ctrl-hint">
            Broadcasts shared timeline BPM and beat clock. Follower local timelines yield control to cluster transport.
          </p>
        </div>
      </div>

    <!-- TAB 3: CUE LIST TRANSPORT -->
    {:else if activeTransportTab === 'cue'}
      <div class="sync-cue-transport-panel">
        <div class="sync-cue-monitor-bar">
          <div class="sync-cue-status-group">
            <span class="sync-cue-state-badge {leaderShowTransport?.cue?.state || leaderNodeData?.syncStatus?.cue?.state || 'idle'}">
              {(leaderShowTransport?.cue?.state || leaderNodeData?.syncStatus?.cue?.state || 'IDLE').toUpperCase()}
            </span>
            <span class="sync-cue-num">
              Current: Cue #{(leaderShowTransport?.cue?.cueIndex ?? leaderNodeData?.syncStatus?.cue?.cueIndex ?? 0) + 1}
            </span>
            <span class="sync-cue-arrow">➔</span>
            <span class="sync-cue-num next">
              Next: Cue #{(leaderShowTransport?.cue?.nextCueIndex ?? leaderNodeData?.syncStatus?.cue?.nextCueIndex ?? 0) + 1}
            </span>
          </div>

          <div class="sync-cue-time">
            {#if leaderShowTransport?.cue?.remainingMs !== undefined}
              <span>{(leaderShowTransport.cue.remainingMs / 1000).toFixed(1)}s remaining</span>
            {/if}
          </div>
        </div>

        <div class="sync-cue-controls-grid">
          <!-- Cue list selection & start -->
          <div class="sync-cue-select-col">
            <label for="cue-list-select" class="sync-ctrl-label">Select Cue List</label>
            <div class="sync-ctrl-input-group">
              <select id="cue-list-select" class="sync-ctrl-select" bind:value={selectedCueListIndex} disabled={!isClusterReady}>
                {#if availableCueLists.length === 0}
                  <option value={0}>Cue List 0 (Default)</option>
                {:else}
                  {#each availableCueLists as cl}
                    <option value={cl.index}>List {cl.index + 1}: {cl.name} ({cl.mode})</option>
                  {/each}
                {/if}
              </select>
              <button
                class="sync-action-btn sync-btn-primary"
                disabled={!isClusterReady || cueTriggerPending}
                onclick={handleStartCueList}
              >
                {cueTriggerPending ? 'Starting…' : '▶ Start List'}
              </button>
            </div>
          </div>

          <!-- Transport buttons -->
          <div class="sync-cue-buttons-col">
            <span class="sync-ctrl-label">Transport Controls</span>
            <div class="sync-cue-btn-row">
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handlePauseCue} title="Pause Cue">
                ⏸ Pause
              </button>
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handleResumeCue} title="Resume Cue">
                ⏯ Resume
              </button>
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handleStopCue} title="Stop Cue">
                ⏹ Stop
              </button>
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handlePreviousCue} title="Previous Cue">
                ⏮ Prev
              </button>
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handleNextCue} title="Next Cue">
                ⏭ Next
              </button>
            </div>
          </div>

          <!-- Jump to Cue -->
          <div class="sync-cue-jump-col">
            <label for="cue-jump-input" class="sync-ctrl-label">Jump to Cue</label>
            <div class="sync-ctrl-input-group">
              <input
                id="cue-jump-input"
                type="number"
                min="0"
                max="31"
                class="sync-ctrl-number"
                bind:value={cueJumpIndex}
                disabled={!isClusterReady}
              />
              <button class="sync-action-btn sync-btn-secondary" disabled={!isClusterReady} onclick={handleJumpCue}>
                Jump
              </button>
            </div>
          </div>

          <!-- Manual Crossfade Slider -->
          <div class="sync-cue-fader-col">
            <label for="sync-manual-fader" class="sync-ctrl-label">
              Manual Crossfade (T-Bar): {Math.round(manualCrossfadeValue * 100)}%
            </label>
            <input
              id="sync-manual-fader"
              type="range"
              min="0"
              max="1"
              step="0.01"
              value={manualCrossfadeValue}
              oninput={handleManualCrossfade}
              disabled={!isClusterReady}
              class="sync-fader-slider"
            />
          </div>
        </div>
      </div>
    {/if}
  </section>

  <!-- Section 4: Fleet Member Detail List -->
  <section class="sync-section-card" aria-label="Fleet Nodes Parity and Clock Status">
    <div class="sync-card-header">
      <div>
        <h2 class="sync-section-title">
          {selectedGroupLabel === 'All' ? 'Fleet Members & Parity' : `Group Members: ${selectedGroupLabel}`}
        </h2>
        <p class="sync-section-desc">Clock locks, network jitter, playback manifest parity, and schedule parity.</p>
      </div>
    </div>

    <div class="sync-node-list">
      {#each activeGroupNodes as data}
        {@const node = data.node}
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
              {#if data?.syncStatus?.groupLabel}
                <span class="sync-group-tag">{data.syncStatus.groupLabel}</span>
              {/if}
              {#if data?.reachabilityState && data.reachabilityState !== 'ONLINE'}
                <span class="sync-role-pill reachability-pill" class:reachability-offline={data.reachabilityState === 'OFFLINE'}>
                  {data.reachabilityState}
                </span>
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
                    Tracking: <strong>{data.syncStatus.masterAddress}</strong>
                  </span>
                {/if}

                {#if data.syncStatus?.transportApplyOk === false}
                  <span class="sync-metric-item text-warn">
                    Transport Apply Error ({data.syncStatus.transportError || 'Failed'})
                  </span>
                {/if}
              {:else}
                <span class="sync-metric-item text-warn">Node Unreachable ({data?.reachabilityState || data?.error || 'Offline'})</span>
              {/if}
            </div>
          </div>

          <div class="sync-node-parity-group">
            <!-- Playback parity badge -->
            <div class="sync-parity-item">
              <span class="sync-parity-title">Playback:</span>
              {#if !data?.online}
                <span class="sync-parity-badge offline">{data?.reachabilityState === 'OFFLINE' ? 'OFFLINE' : 'UNREACHABLE'}</span>
              {:else if data.playbackParity?.status === 'MATCHED'}
                <span class="sync-parity-badge matched">MATCHED</span>
              {:else if data.playbackParity?.status === 'DRIFT'}
                <span class="sync-parity-badge drift">DRIFT</span>
              {:else if data.playbackParity?.status === 'INCOMPATIBLE'}
                <span class="sync-parity-badge incompatible" title={data.playbackParity.reason}>INCOMPATIBLE</span>
              {:else}
                <span class="sync-parity-badge unavailable">UNAVAILABLE</span>
              {/if}
            </div>

            <!-- Schedule parity badge -->
            <div class="sync-parity-item">
              <span class="sync-parity-title">Schedule:</span>
              {#if !data?.online}
                <span class="sync-parity-badge offline">{data?.reachabilityState === 'OFFLINE' ? 'OFFLINE' : 'UNREACHABLE'}</span>
              {:else if data.scheduleParity?.status === 'MATCHED'}
                <span class="sync-parity-badge matched">MATCHED</span>
              {:else if data.scheduleParity?.status === 'DRIFT'}
                <span class="sync-parity-badge drift sched">DIFF</span>
              {:else}
                <span class="sync-parity-badge unavailable">—</span>
              {/if}
            </div>

            {#if data.playbackParity?.status === 'DRIFT' || data.scheduleParity?.status === 'DRIFT'}
              <button class="sync-diff-link" onclick={() => (diffTargetNode = data)}>
                Inspect Diff
              </button>
            {/if}
          </div>
        </div>
      {/each}
    </div>

    <!-- Section for nodes not in this group -->
    {#if otherFleetNodes.length > 0}
      <div class="sync-other-nodes-header">
        <h3 class="sync-other-title">Other Fleet Nodes (Not in {selectedGroupLabel})</h3>
        <p class="sync-other-desc">These nodes do not participate in the active group's sync protocol or readiness gate.</p>
      </div>

      <div class="sync-node-list">
        {#each otherFleetNodes as data}
          {@const node = data.node}
          <div class="sync-node-row is-other">
            <div class="sync-node-main">
              <div class="sync-node-title-row">
                <span class="sync-node-name">{node.name || 'Unnamed Node'}</span>
                <span class="sync-node-ip">{node.ip}</span>
                <span class="sync-role-pill standalone-pill">
                  {data?.syncStatus?.groupLabel ? `Group: ${data.syncStatus.groupLabel}` : 'No Group'}
                </span>
                {#if data?.reachabilityState && data.reachabilityState !== 'ONLINE'}
                  <span class="sync-role-pill reachability-pill" class:reachability-offline={data.reachabilityState === 'OFFLINE'}>
                    {data.reachabilityState}
                  </span>
                {/if}
              </div>
            </div>
          </div>
        {/each}
      </div>
    {/if}
  </section>
</div>

<!-- Modal 1: Configure Sync Group (Admin Only) -->
<OperatorPanelDialog
  open={showConfigureModal}
  title="Configure Sync Group"
  description="Manage cluster election mode, group label, and cluster membership."
  maxWidth="560px"
  onclose={() => (showConfigureModal = false)}
>
  {#snippet children()}
    {#if configCryptoError}
      <div class="sync-modal-alert error">{configCryptoError}</div>
    {/if}
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
      A cryptographically secure cluster sync token will be generated via Web Crypto and distributed to all selected nodes.
    </p>
  {/snippet}

  {#snippet footer()}
    <button class="sync-action-btn sync-btn-secondary" onclick={() => (showConfigureModal = false)}>
      Cancel
    </button>
    <button
      class="sync-action-btn sync-btn-primary"
      disabled={configSaving || Boolean(configCryptoError)}
      onclick={handleSaveClusterConfig}
    >
      {configSaving ? 'Applying to Fleet…' : 'Save & Distribute Sync Config'}
    </button>
  {/snippet}
</OperatorPanelDialog>

<!-- Modal 2: Deploy Show Content (Admin Only) -->
<OperatorPanelDialog
  open={showDeployModal}
  title="Deploy Show Content Across Fleet"
  description="Mirror show configuration and content from golden master across fleet members."
  maxWidth="640px"
  allowBackdropClose={!deployRunning}
  onclose={() => !deployRunning && (showDeployModal = false)}
>
  {#snippet children()}
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
        <span><strong>Mirror Exactly:</strong> Delete obsolete presets, motion tracks, or cue lists on targets not present on source.</span>
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
  {/snippet}

  {#snippet footer()}
    <button class="sync-action-btn sync-btn-secondary" disabled={deployRunning} onclick={() => (showDeployModal = false)}>
      Close
    </button>
    <button class="sync-action-btn sync-btn-primary" disabled={deployRunning} onclick={handleDeployShow}>
      {deployRunning ? 'Deploying Content…' : 'Start Deployment'}
    </button>
  {/snippet}
</OperatorPanelDialog>

<!-- Modal 3: Show Diff Inspector -->
<OperatorPanelDialog
  open={Boolean(diffTargetNode)}
  title={`Show Content Diff: ${diffTargetNode?.node.name || diffTargetNode?.node.ip || ''}`}
  description={`Comparing against current Leader (${leaderNodeData?.node.name || leaderIp || 'None'}).`}
  maxWidth="640px"
  onclose={() => (diffTargetNode = null)}
>
  {#snippet children()}
    {#if diffTargetNode}
      <p class="sync-diff-desc">
        Comparing against current Leader ({leaderNodeData?.node.name || leaderNodeData?.node.ip}).
      </p>

      <h4 class="sync-diff-subheading">Playback Assets ({diffTargetNode.playbackParity?.status || 'UNAVAILABLE'})</h4>
      <div class="sync-diff-table">
        {#if diffTargetNode.playbackParity?.diffs && diffTargetNode.playbackParity.diffs.length > 0}
          {#each diffTargetNode.playbackParity.diffs as diff}
            <div class="sync-diff-row state-{diff.state}">
              <span class="sync-diff-label">{diff.label}</span>
              <span class="sync-diff-status badge-{diff.state}">{diff.state.toUpperCase()}</span>
            </div>
          {/each}
        {:else}
          <div class="sync-diff-empty">All playback assets matched.</div>
        {/if}
      </div>

      <h4 class="sync-diff-subheading" style="margin-top: 1rem;">Calendar Schedules ({diffTargetNode.scheduleParity?.status || 'UNAVAILABLE'})</h4>
      <div class="sync-diff-table">
        {#if diffTargetNode.scheduleParity?.diffs && diffTargetNode.scheduleParity.diffs.length > 0}
          {#each diffTargetNode.scheduleParity.diffs as diff}
            <div class="sync-diff-row state-{diff.state}">
              <span class="sync-diff-label">{diff.label}</span>
              <span class="sync-diff-status badge-{diff.state}">{diff.state.toUpperCase()}</span>
            </div>
          {/each}
        {:else}
          <div class="sync-diff-empty">No schedule differences.</div>
        {/if}
      </div>
    {/if}
  {/snippet}

  {#snippet footer()}
    <button class="sync-action-btn sync-btn-secondary" onclick={() => (diffTargetNode = null)}>
      Done
    </button>
  {/snippet}
</OperatorPanelDialog>

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

  .sync-alert-banner {
    display: flex;
    align-items: center;
    gap: 1rem;
    padding: 1rem 1.25rem;
    border-radius: 8px;
    font-size: 0.875rem;
  }

  .sync-alert-banner.alert-warn {
    background: rgba(245, 158, 11, 0.12);
    border: 1px solid rgba(245, 158, 11, 0.35);
    color: #fde68a;
  }

  .sync-alert-banner.alert-danger {
    background: rgba(239, 68, 68, 0.12);
    border: 1px solid rgba(239, 68, 68, 0.35);
    color: #fca5a5;
  }

  .sync-banner-icon {
    font-size: 1.4rem;
    font-weight: 700;
  }

  .sync-banner-body strong {
    display: block;
    font-size: 0.95rem;
    margin-bottom: 0.25rem;
  }

  .sync-banner-body p {
    margin: 0;
    font-size: 0.82rem;
  }

  .sync-banner-body code {
    background: rgba(0, 0, 0, 0.3);
    padding: 0.1rem 0.35rem;
    border-radius: 4px;
    font-family: monospace;
  }

  .sync-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    flex-wrap: wrap;
    gap: 1rem;
    border-bottom: 1px solid var(--dash-surface, rgba(255, 255, 255, 0.05));
    padding-bottom: 1.25rem;
  }

  .sync-badge-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
  }

  .sync-tag {
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.08em;
    padding: 0.2rem 0.5rem;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
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
    color: var(--dash-ok, #30d158);
  }

  .sync-status-pill.sync-warn {
    background: rgba(245, 158, 11, 0.2);
    color: var(--dash-warm, #ff9500);
  }

  .sync-status-pill.sync-danger {
    background: rgba(239, 68, 68, 0.2);
    color: #f87171;
  }

  .sync-status-pill.sync-neutral {
    background: rgba(148, 163, 184, 0.2);
    color: #cbd5e1;
  }

  .sync-group-filter-wrap {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    margin-left: 0.5rem;
  }

  .sync-filter-label {
    font-size: 0.75rem;
    color: #94a3b8;
    font-weight: 600;
  }

  .sync-filter-select {
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #f8fafc;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    padding: 0.2rem 0.5rem;
    border-radius: 4px;
    font-size: 0.75rem;
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
    background: var(--dash-ok, #30d158);
    color: #022c22;
  }
  .sync-btn-primary:hover:not(:disabled) {
    background: var(--dash-ok, #30d158);
  }
  .sync-btn-primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .sync-btn-secondary {
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #e2e8f0;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
  }
  .sync-btn-secondary:hover:not(:disabled) {
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
  }
  .sync-btn-secondary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .sync-btn-icon {
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #e2e8f0;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    padding: 0.55rem 0.85rem;
  }

  /* ── Metric Summary Tiles ───────────────────────────────────────── */
  .sync-summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 1rem;
  }

  .sync-metric-tile {
    background: var(--dash-surface-muted, #111114);
    border: 1px solid var(--dash-surface, rgba(255, 255, 255, 0.05));
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
    color: var(--dash-led, #00d4ff);
    font-size: 1.25rem;
  }

  .sync-metric-val.success {
    color: var(--dash-ok, #30d158);
  }

  .sync-metric-val.warn {
    color: var(--dash-warm, #ff9500);
  }

  .sync-metric-val.mode {
    color: var(--dash-accent, #ff2d55);
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
    background: var(--dash-surface-muted, #111114);
    border: 1px solid var(--dash-surface, rgba(255, 255, 255, 0.05));
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
    gap: 0.75rem;
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
    color: var(--dash-warm, #ff9500);
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
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: 8px;
  }

  .sync-topo-node-badge.is-leader {
    border-color: var(--dash-ok, #30d158);
    background: rgba(16, 185, 129, 0.1);
  }

  .sync-topo-icon {
    color: var(--dash-ok, #30d158);
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
    color: var(--dash-ok, #30d158);
  }

  .follower-pill {
    background: rgba(56, 189, 248, 0.2);
    color: var(--dash-led, #00d4ff);
  }

  .standalone-pill {
    background: rgba(148, 163, 184, 0.2);
    color: #94a3b8;
  }

  .sync-group-tag {
    font-size: 0.65rem;
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
    color: #cbd5e1;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
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
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
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
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-topo-stats {
    display: flex;
    gap: 0.35rem;
  }

  .sync-pill-tag {
    font-size: 0.7rem;
    background: var(--dash-surface-muted, #111114);
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    color: var(--dash-ok, #30d158);
    font-family: monospace;
  }

  .sync-pill-tag.rtt {
    color: var(--dash-led, #00d4ff);
  }

  .sync-pill-tag.err {
    background: rgba(239, 68, 68, 0.2);
    color: #f87171;
  }

  .sync-topo-empty,
  .sync-empty-state {
    color: #64748b;
    font-size: 0.85rem;
    padding: 1.5rem;
    text-align: center;
  }

  /* ── Transport Tabs & Controls ─────────────────────────────────── */
  .transport-tabs {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }

  .transport-tab-btn {
    font-size: 0.8rem;
    font-weight: 600;
    padding: 0.4rem 0.85rem;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #94a3b8;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .transport-tab-btn.active {
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
    color: #f8fafc;
    border-color: #475569;
  }

  .transport-tab-btn.is-active-mode {
    border-color: var(--dash-accent, #ff2d55);
    color: #c4b5fd;
    box-shadow: 0 0 8px rgba(167, 139, 250, 0.25);
  }

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

  .sync-ctrl-hint {
    font-size: 0.75rem;
    color: #64748b;
    margin: 0;
  }

  .sync-ctrl-input-group {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }

  .sync-ctrl-select {
    flex: 1;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #f8fafc;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    padding: 0.55rem;
    border-radius: 6px;
  }

  .sync-ctrl-number {
    width: 70px;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    color: #f8fafc;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    padding: 0.55rem;
    border-radius: 6px;
    text-align: center;
  }

  .sync-timeline-status-badge {
    padding: 0.55rem 0.75rem;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: 6px;
    font-size: 0.8rem;
    color: var(--dash-accent, #ff2d55);
    font-weight: 600;
  }

  .sync-control-banner {
    padding: 0.75rem 1rem;
    background: rgba(56, 189, 248, 0.1);
    border: 1px solid rgba(56, 189, 248, 0.3);
    border-radius: 6px;
    font-size: 0.85rem;
    color: var(--dash-led, #00d4ff);
  }

  /* ── Synchronized Cue Playback Panel ───────────────────────────── */
  .sync-cue-transport-panel {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .sync-cue-monitor-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: 6px;
    padding: 0.75rem 1rem;
  }

  .sync-cue-status-group {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .sync-cue-state-badge {
    font-size: 0.7rem;
    font-weight: 800;
    padding: 0.2rem 0.55rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
    color: #94a3b8;
  }

  .sync-cue-state-badge.holding {
    background: rgba(59, 130, 246, 0.2);
    color: #60a5fa;
  }

  .sync-cue-state-badge.fading {
    background: rgba(16, 185, 129, 0.2);
    color: var(--dash-ok, #30d158);
  }

  .sync-cue-state-badge.paused {
    background: rgba(245, 158, 11, 0.2);
    color: var(--dash-warm, #ff9500);
  }

  .sync-cue-num {
    font-size: 0.85rem;
    font-weight: 600;
    color: #f8fafc;
  }

  .sync-cue-num.next {
    color: #94a3b8;
  }

  .sync-cue-arrow {
    color: #64748b;
  }

  .sync-cue-time {
    font-size: 0.8rem;
    color: #94a3b8;
  }

  .sync-cue-controls-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 1.25rem;
    background: rgba(15, 23, 42, 0.6);
    border: 1px solid var(--dash-surface, rgba(255, 255, 255, 0.05));
    border-radius: 6px;
    padding: 1rem;
  }

  .sync-cue-select-col,
  .sync-cue-buttons-col,
  .sync-cue-jump-col,
  .sync-cue-fader-col {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }

  .sync-cue-btn-row {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }

  .sync-fader-slider {
    width: 100%;
    accent-color: var(--dash-ok, #30d158);
    margin-top: 0.35rem;
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
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: 6px;
    padding: 0.85rem 1.25rem;
    flex-wrap: wrap;
    gap: 1rem;
  }

  .sync-node-row.offline {
    opacity: 0.6;
    border-color: #27272a;
  }

  .sync-node-row.is-other {
    background: var(--dash-surface-muted, #111114);
    border-style: dashed;
    border-color: var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-node-main {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .sync-node-title-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.6rem;
  }

  .sync-node-name {
    font-size: 0.95rem;
    font-weight: 600;
    color: #f8fafc;
  }

  .sync-node-ip {
    font-size: 0.8rem;
    color: #94a3b8;
  }

  .sync-node-metrics {
    display: flex;
    flex-wrap: wrap;
    gap: 1rem;
    font-size: 0.8rem;
    color: #cbd5e1;
  }

  .sync-metric-item strong {
    font-weight: 600;
  }

  .text-success {
    color: var(--dash-ok, #30d158);
  }

  .text-warn {
    color: var(--dash-warm, #ff9500);
  }

  .sync-node-parity-group {
    display: flex;
    align-items: center;
    gap: 1rem;
  }

  .sync-parity-item {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .sync-parity-title {
    font-size: 0.75rem;
    color: #64748b;
    text-transform: uppercase;
  }

  .sync-parity-badge {
    font-size: 0.7rem;
    font-weight: 700;
    padding: 0.2rem 0.5rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
  }

  .sync-parity-badge.matched {
    background: rgba(16, 185, 129, 0.15);
    color: var(--dash-ok, #30d158);
  }

  .sync-parity-badge.drift {
    background: rgba(245, 158, 11, 0.15);
    color: var(--dash-warm, #ff9500);
  }

  .sync-parity-badge.drift.sched {
    background: rgba(148, 163, 184, 0.15);
    color: #cbd5e1;
  }

  .sync-parity-badge.incompatible {
    background: rgba(239, 68, 68, 0.2);
    color: #f87171;
  }

  .sync-parity-badge.offline,
  .sync-parity-badge.unavailable {
    background: var(--dash-border, rgba(255, 255, 255, 0.12));
    color: #94a3b8;
  }

  .sync-diff-link {
    background: none;
    border: none;
    color: var(--dash-led, #00d4ff);
    text-decoration: underline;
    font-size: 0.75rem;
    cursor: pointer;
    padding: 0;
  }

  .sync-other-nodes-header {
    margin-top: 1rem;
    padding-top: 1rem;
    border-top: 1px solid var(--dash-surface, rgba(255, 255, 255, 0.05));
  }

  .sync-other-title {
    font-size: 0.95rem;
    font-weight: 600;
    color: #94a3b8;
    margin: 0;
  }

  .sync-other-desc {
    font-size: 0.75rem;
    color: #64748b;
    margin: 0.2rem 0 0 0;
  }

  /* ── Modals & Reachability Pills ──────────────────────────────────── */
  .sync-role-pill.reachability-pill {
    background: rgba(255, 149, 0, 0.15);
    color: var(--dash-warm, #ff9500);
    border: 1px solid rgba(255, 149, 0, 0.35);
  }
  .sync-role-pill.reachability-pill.reachability-offline {
    background: rgba(255, 45, 85, 0.15);
    color: var(--dash-accent, #ff2d55);
    border: 1px solid rgba(255, 45, 85, 0.35);
  }

  .sync-modal-alert {
    padding: 0.75rem;
    border-radius: 6px;
    font-size: 0.85rem;
  }

  .sync-modal-alert.error {
    background: rgba(255, 45, 85, 0.15);
    border: 1px solid rgba(255, 45, 85, 0.4);
    color: var(--dash-accent, #ff2d55);
  }

  .sync-modal-alert.success {
    background: rgba(48, 209, 88, 0.15);
    border: 1px solid rgba(48, 209, 88, 0.4);
    color: var(--dash-ok, #30d158);
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
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    color: #f8fafc;
    padding: 0.55rem;
    border-radius: 6px;
    font-size: 0.875rem;
  }

  .sync-form-hint {
    font-size: 0.75rem;
    color: #64748b;
    margin: 0;
  }

  .sync-radio-group {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .sync-radio-opt,
  .sync-checkbox-opt {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.85rem;
    color: #cbd5e1;
    cursor: pointer;
  }

  .sync-checkbox-list {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    max-height: 150px;
    overflow-y: auto;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    padding: 0.75rem;
    border-radius: 6px;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-cat-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.5rem;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    padding: 0.75rem;
    border-radius: 6px;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-checkbox-opt.highlight-opt {
    background: rgba(245, 158, 11, 0.08);
    border: 1px solid rgba(245, 158, 11, 0.25);
    padding: 0.5rem;
    border-radius: 6px;
    font-size: 0.8rem;
  }

  /* ── Diff Table ────────────────────────────────────────────────── */
  .sync-diff-desc {
    font-size: 0.8rem;
    color: #94a3b8;
    margin: 0;
  }

  .sync-diff-subheading {
    font-size: 0.85rem;
    font-weight: 600;
    color: #cbd5e1;
    margin: 0.5rem 0 0.25rem 0;
  }

  .sync-diff-table {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    max-height: 200px;
    overflow-y: auto;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    padding: 0.5rem;
    border-radius: 6px;
  }

  .sync-diff-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.4rem 0.6rem;
    border-radius: 4px;
    font-size: 0.8rem;
  }

  .sync-diff-row.state-match {
    background: rgba(16, 185, 129, 0.08);
  }
  .sync-diff-row.state-changed {
    background: rgba(245, 158, 11, 0.15);
  }
  .sync-diff-row.state-missing {
    background: rgba(239, 68, 68, 0.15);
  }
  .sync-diff-row.state-extra {
    background: rgba(56, 189, 248, 0.15);
  }

  .sync-diff-status {
    font-size: 0.7rem;
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
  }

  .badge-match {
    color: var(--dash-ok, #30d158);
  }
  .badge-changed {
    color: var(--dash-warm, #ff9500);
  }
  .badge-missing {
    color: #f87171;
  }
  .badge-extra {
    color: var(--dash-led, #00d4ff);
  }

  .sync-diff-empty {
    font-size: 0.8rem;
    color: #64748b;
    padding: 0.5rem;
    text-align: center;
  }

  .sync-deploy-progress-box {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    background: var(--dash-surface, rgba(255, 255, 255, 0.05));
    padding: 0.75rem;
    border-radius: 6px;
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-progress-title {
    font-size: 0.8rem;
    font-weight: 600;
    color: #94a3b8;
    margin: 0;
  }

  .sync-progress-row {
    display: flex;
    justify-content: space-between;
    font-size: 0.8rem;
    padding: 0.25rem 0;
    border-bottom: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
  }

  .sync-progress-ip {
    color: #f8fafc;
  }

  .sync-progress-state.complete {
    color: var(--dash-ok, #30d158);
  }
  .sync-progress-state.deploying {
    color: var(--dash-led, #00d4ff);
  }
  .sync-progress-state.partial {
    color: var(--dash-warm, #ff9500);
  }
  .sync-progress-state.failed {
    color: #f87171;
  }

  .sync-progress-err {
    font-size: 0.75rem;
    color: #f87171;
  }
</style>
