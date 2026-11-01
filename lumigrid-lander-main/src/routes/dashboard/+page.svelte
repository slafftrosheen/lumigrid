<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import NetworkConfigCard from '$lib/components/dashboard/NetworkConfigCard.svelte';
  import {
    getSelectedNodeIp,
    setSelectedNodeIp,
  } from '$lib/auth/storage';
  import {
    getSavedNodes,
    addSavedNode,
    renameSavedNode,
    removeSavedNode,
    normalizeNodeAddress,
    upsertNodeRecord,
    type SavedNodeInfo,
    getNodeDisplayName,
  } from '$lib/fleet';
  import { getFleetNodes } from '$lib/api/fleet';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';
  import {
    getNodeLiveState,
    getNodeLiveSnapshot,
    nodePixelStore,
    connect,
    refreshHealth,
    recoverStateViaHttp,
    captureNodeOperation,
    reconnect as reconnectNode,
    LANES,
    PIXELS_PER_LANE,
    BUF_SIZE,
    type NodeLiveState,
  } from '$lib/state/nodeStore';
  import {
    postFactoryAbort,
    postFactoryEffect,
    postFactoryAutoLoop,
    getNodeInfo,
    type PublicNodeInfo,
  } from '$lib/api/node';
  import { getDeviceConfig, type DeviceConfig } from '$lib/api/config';

  let nodeIp = $state('');
  let nodeInfo = $state<PublicNodeInfo | null>(null);
  let liveState = $state<NodeLiveState>(getNodeLiveSnapshot());
  let canvasEl = $state<HTMLCanvasElement | null>(null);
  let rafId = 0;
  let unsubscribeState: (() => void) | null = null;
  let unsubscribePixels: (() => void) | null = null;
  let actionToast = $state('');
  let actionLoading = $state(false);
  let localBri = $state(128);
  let isAdmin = $state(false);
  let clock = $state(Date.now());
  let clockTimer: ReturnType<typeof setInterval> | null = null;
  let brightnessTimer: ReturnType<typeof setTimeout> | null = null;
  let brightnessDirty = $state(false);
  let pendingBrightness = $state<number | null>(null);
  let brightnessSnapshot: NodeLiveState['snapshot'] = null;
  let viewOwner = captureNodeOperation('');
  let disposed = false;
  let discoveryRequest = 0;
  let hasStatus = $derived(liveState.snapshot !== null);
  let statusFresh = $derived(hasStatus && clock - liveState.lastSnapshotAt <= 10000 &&
    (liveState.connection === 'online' || liveState.connection === 'degraded'));
  let healthFresh = $derived(liveState.health !== null && !liveState.healthError &&
    clock - liveState.healthUpdatedAt <= 20000 && statusFresh);
  let powerEstimate = $derived(liveState.snapshot?.output.power);
  let estimatedPowerMw = $derived(powerEstimate?.estimatedLimitedMw ?? liveState.health?.output.current_power_mw);
  let connectionLabel = $derived(liveState.connection === 'online'
    ? (hasStatus ? (statusFresh ? 'LIVE' : 'STATUS STALE') : 'WAITING FOR STATUS')
    : liveState.connection.toUpperCase().replaceAll('-', ' '));

  // Fleet management states
  let activeTab = $state<'node' | 'fleet'>('node');
  let savedFleet = $state<SavedNodeInfo[]>([]);
  let activeFleetNode = $derived(savedFleet.find(node => node.ip === nodeIp));
  let isScanningFleet = $state(false);
  let discoveryTimer: ReturnType<typeof setInterval> | null = null;
  let newIpInput = $state('');
  let newNameInput = $state('');
  let fleetInputError = $state('');
  let fleetDialog = $state<{ kind: 'rename' | 'remove'; ip: string; currentName: string } | null>(null);
  let fleetDialogValue = $state('');
  let deviceConfig = $state<DeviceConfig | null>(null);
  let showConnInfo = $state(false);

  // Derive the AP SSID the firmware will broadcast, matching LumigridRuntime.cpp logic:
  //   valid config with non-default name -> "LumiGrid-{name}"
  //   otherwise -> "LumiGrid-Factory" (first boot / factory state)
  const apSsid = $derived.by(() => {
    const name = deviceConfig?.deviceName;
    if (!deviceConfig) return 'Unavailable';
    if (name && name.trim().length > 0 && name.trim() !== 'lumigrid') {
      return `LumiGrid-${name.trim()}`;
    }
    return 'LumiGrid-Factory';
  });

  function toast(msg: string) {
    const owner = captureNodeOperation(nodeIp);
    actionToast = msg;
    setTimeout(() => {
      if (!disposed && owner.isCurrent() && actionToast === msg) actionToast = '';
    }, 3000);
  }

  function formatUptime(s?: number): string {
    if (!s) return '—';
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  function formatBytes(b?: number): string {
    if (!b) return '—';
    if (b >= 1048576) return `${(b / 1048576).toFixed(1)} MB`;
    if (b >= 1024) return `${(b / 1024).toFixed(1)} KB`;
    return `${b} B`;
  }

  function formatTimeAgo(ts?: number): string {
    if (!ts) return 'Never';
    const diff = Math.floor((Date.now() - ts) / 1000);
    if (diff < 60) return `${diff}s ago`;
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
    return `${Math.floor(diff / 3600)}h ago`;
  }

  async function handleBlackout() {
    if (!nodeIp || actionLoading) return;
    const owner = captureNodeOperation(nodeIp);
    actionLoading = true;
    try {
      await postFactoryAbort(owner.ip);
      if (disposed || !owner.isCurrent()) return;
      toast('Blackout requested. Waiting for node status.');
      refreshHealth(owner.ip).catch(() => {});
      recoverStateViaHttp(owner.ip).catch(() => {});
    } catch (e) {
      if (!disposed && owner.isCurrent()) toast(`Blackout not confirmed: ${e}`);
    } finally {
      if (!disposed && owner.isCurrent()) actionLoading = false;
    }
  }

  async function handleToggleAutoLoop() {
    if (!nodeIp || actionLoading || !statusFresh) return;
    const owner = captureNodeOperation(nodeIp);
    actionLoading = true;
    const nextState = !(liveState.status?.autoLoop ?? false);
    try {
      await postFactoryAutoLoop(owner.ip, nextState);
      if (disposed || !owner.isCurrent()) return;
      toast(`Auto loop ${nextState ? 'enable' : 'disable'} requested. Waiting for node status.`);
      recoverStateViaHttp(owner.ip).catch(() => {});
    } catch (e) {
      if (!disposed && owner.isCurrent()) toast(`Auto loop not confirmed: ${e}`);
    } finally {
      if (!disposed && owner.isCurrent()) actionLoading = false;
    }
  }

  async function handleSetBrightness(e: Event) {
    const val = Number((e.target as HTMLInputElement).value);
    localBri = val;
    brightnessDirty = true;
    if (!nodeIp || actionLoading || !statusFresh || !liveState.status?.effect) return;
    const owner = captureNodeOperation(nodeIp);
    const effect = liveState.status.effect;
    actionLoading = true;
    pendingBrightness = val;
    brightnessSnapshot = liveState.snapshot;
    if (brightnessTimer) clearTimeout(brightnessTimer);
    try {
      await postFactoryEffect(owner.ip, effect as any, val);
      if (disposed || !owner.isCurrent()) return;
      if (pendingBrightness !== null) {
        toast('Brightness requested. Waiting for node status.');
        brightnessTimer = setTimeout(() => {
          if (!disposed && owner.isCurrent() && pendingBrightness === val) {
            pendingBrightness = null;
            toast('Brightness is not yet confirmed. Your draft is preserved.');
          }
        }, 8000);
      }
      recoverStateViaHttp(owner.ip).catch(() => {});
    } catch (error) {
      if (!disposed && owner.isCurrent()) {
        pendingBrightness = null;
        toast(`Brightness not confirmed. Your draft is preserved. ${error}`);
      }
    } finally {
      if (!disposed && owner.isCurrent()) actionLoading = false;
    }
  }

  // Fleet management functions
  function refreshFleetList() {
    savedFleet = getSavedNodes();
  }

  async function pollFleetDiscovery() {
    if (!nodeIp || isScanningFleet) return;
    const owner = captureNodeOperation(nodeIp);
    const request = ++discoveryRequest;
    isScanningFleet = true;
    try {
      const resp = await getFleetNodes(owner.ip);
      if (disposed || !owner.isCurrent() || request !== discoveryRequest) return;
      if (resp?.self) {
        upsertNodeRecord({
          id: resp.self.id,
          name: resp.self.name,
          address: owner.ip,
          family: resp.self.family,
          model: resp.self.model,
          firmware: resp.self.firmware,
          presence: 'online',
          discoverySource: 'mdns',
          lastSeen: Date.now(),
        });
      }
      if (resp?.nodes && Array.isArray(resp.nodes)) {
        for (const dn of resp.nodes) {
          upsertNodeRecord({
            id: dn.id,
            name: dn.name,
            address: dn.ip,
            family: dn.family,
            model: dn.model,
            firmware: dn.firmware,
            presence: dn.status,
            discoverySource: 'mdns',
            lastSeen: Date.now() - dn.ageMs,
          });
        }
      }
      refreshFleetList();
    } catch {
      // transient discovery failure or unauthenticated
    } finally {
      if (!disposed && owner.isCurrent() && request === discoveryRequest) isScanningFleet = false;
    }
  }

  function handleSelectFleetNode(ip: string) {
    setSelectedNodeIp(ip);
    connect(ip);
    activeTab = 'node';
    toast(`Selected node ${ip}. Waiting for its status.`);
  }

  function loadNodeDetails() {
    const owner = captureNodeOperation(nodeIp);
    getNodeInfo(owner.ip).then(info => {
      if (!disposed && owner.isCurrent()) nodeInfo = info;
    }).catch(() => {});
    getDeviceConfig(owner.ip).then(cfg => {
      if (!disposed && owner.isCurrent()) deviceConfig = cfg;
    }).catch(() => {});
    pollFleetDiscovery();
  }

  function handleAddFleetNode() {
    const address = normalizeNodeAddress(newIpInput);
    if (!address) {
      fleetInputError = 'Enter a node address such as 192.168.1.50 or lumigrid.local.';
      return;
    }

    fleetInputError = '';
    const label = newNameInput.trim();
    const existing = savedFleet.find(node => node.ip === address);
    if (existing) {
      if (label && label !== existing.name) {
        renameSavedNode(address, label);
        refreshFleetList();
        toast(`Updated node ${address}`);
      } else {
        toast(`Node ${address} is already registered`);
      }
      newIpInput = '';
      newNameInput = '';
      return;
    }

    addSavedNode(address, label || undefined);
    newIpInput = '';
    newNameInput = '';
    refreshFleetList();
    toast(`Added node ${address} to fleet`);
  }

  function handleRenameNode(ip: string, currentName: string) {
    fleetDialogValue = currentName;
    fleetDialog = { kind: 'rename', ip, currentName };
  }

  function handleRemoveNode(ip: string) {
    const currentName = savedFleet.find(node => node.ip === ip)?.name || ip;
    fleetDialogValue = '';
    fleetDialog = { kind: 'remove', ip, currentName };
  }

  function closeFleetDialog() {
    fleetDialog = null;
    fleetDialogValue = '';
  }

  function confirmFleetDialog(value: string) {
    if (!fleetDialog) return;
    const { kind, ip } = fleetDialog;

    if (kind === 'rename') {
      const nextName = value.trim();
      if (!nextName) return;
      renameSavedNode(ip, nextName);
      refreshFleetList();
      toast(`Renamed node ${ip}`);
      closeFleetDialog();
      return;
    }

    removeSavedNode(ip);
    refreshFleetList();
    if (nodeIp === ip) {
      nodeIp = getSelectedNodeIp();
      if (nodeIp) connect(nodeIp);
    }
    toast(`Removed node ${ip}`);
    closeFleetDialog();
  }

  // Live LED Output Canvas Rendering
  let currentPixelBuf: Uint8Array = new Uint8Array(BUF_SIZE);

  function renderLeds() {
    if (!canvasEl) return;
    const ctx = canvasEl.getContext('2d');
    if (!ctx) return;

    const width = canvasEl.width;
    const height = canvasEl.height;
    ctx.fillStyle = '#050508';
    ctx.fillRect(0, 0, width, height);

    const laneHeight = height / LANES;
    const pixelWidth = width / PIXELS_PER_LANE;

    for (let lane = 0; lane < LANES; lane++) {
      const laneOffset = lane * PIXELS_PER_LANE * 3;
      const y = lane * laneHeight;

      for (let px = 0; px < PIXELS_PER_LANE; px++) {
        const idx = laneOffset + px * 3;
        const r = currentPixelBuf[idx];
        const g = currentPixelBuf[idx + 1];
        const b = currentPixelBuf[idx + 2];

        if (r > 0 || g > 0 || b > 0) {
          ctx.fillStyle = `rgb(${r}, ${g}, ${b})`;
          ctx.fillRect(px * pixelWidth, y + 1, Math.max(1, pixelWidth - 0.5), laneHeight - 2);
        } else {
          ctx.fillStyle = 'rgba(255, 255, 255, 0.02)';
          ctx.fillRect(px * pixelWidth, y + 2, Math.max(1, pixelWidth - 1), laneHeight - 4);
        }
      }
    }

    rafId = requestAnimationFrame(renderLeds);
  }

  onMount(() => {
    disposed = false;
    nodeIp = getSelectedNodeIp();
    isAdmin = nodeAuthStore.getRole(nodeIp) === 'admin';
    refreshFleetList();

    if (!nodeIp && savedFleet.length > 0) {
      activeTab = 'fleet';
    }

    if (nodeIp) {
      connect(nodeIp);
    }
    clockTimer = setInterval(() => { clock = Date.now(); }, 1000);

    discoveryTimer = setInterval(() => {
      if (nodeIp) {
        pollFleetDiscovery();
      }
    }, 10000);

    unsubscribeState = getNodeLiveState().subscribe(val => {
      liveState = val;
      isAdmin = val.authenticated && val.role === 'admin';
      if (val.nodeIp !== nodeIp || !viewOwner.isCurrent()) {
        nodeIp = val.nodeIp;
        viewOwner = captureNodeOperation(nodeIp);
        nodeInfo = null;
        deviceConfig = null;
        actionToast = '';
        actionLoading = isScanningFleet = brightnessDirty = false;
        pendingBrightness = null;
        if (brightnessTimer) clearTimeout(brightnessTimer);
        discoveryRequest++;
        if (nodeIp) {
          loadNodeDetails();
        }
      }
      if (pendingBrightness !== null && val.snapshot !== brightnessSnapshot &&
          val.status?.brightness === pendingBrightness) {
        // A confirmation can arrive after the operator has begun another draft.
        brightnessDirty = localBri !== pendingBrightness;
        pendingBrightness = null;
        if (brightnessTimer) clearTimeout(brightnessTimer);
        toast(brightnessDirty ? 'Previous brightness confirmed. Your newer draft is preserved.' : 'Node confirmed brightness.');
      }
      if (!brightnessDirty && val.status?.brightness !== undefined) {
        localBri = val.status.brightness;
      }
    });

    unsubscribePixels = nodePixelStore.subscribe(buf => {
      if (buf && buf.length === BUF_SIZE) {
        currentPixelBuf.set(buf);
      }
    });

    rafId = requestAnimationFrame(renderLeds);
  });

  onDestroy(() => {
    disposed = true;
    if (clockTimer) clearInterval(clockTimer);
    if (brightnessTimer) clearTimeout(brightnessTimer);
    if (discoveryTimer) clearInterval(discoveryTimer);
    if (rafId) cancelAnimationFrame(rafId);
    if (unsubscribeState) unsubscribeState();
    if (unsubscribePixels) unsubscribePixels();
  });
</script>

<svelte:head>
  <title>LumiGrid Operator Console</title>
</svelte:head>

<div class="overview-page">
  {#if actionToast}
    <div class="toast-bar" role="status">{actionToast}</div>
  {/if}

  <!-- View Mode Selector Bar -->
  <div class="mode-bar">
    <div class="mode-selector">
      <button
        class="mode-btn"
        class:active={activeTab === 'node'}
        aria-pressed={activeTab === 'node'}
        onclick={() => (activeTab = 'node')}
        type="button"
      >
        <span>Active Node Focus</span>
        {#if nodeIp}
          <span class="mode-pill mono">{nodeIp}</span>
        {/if}
      </button>

      <button
        class="mode-btn"
        class:active={activeTab === 'fleet'}
        aria-pressed={activeTab === 'fleet'}
        onclick={() => (activeTab = 'fleet')}
        type="button"
      >
        <span>Fleet Devices</span>
        <span class="mode-pill count">{savedFleet.length}</span>
      </button>
    </div>

    {#if activeTab === 'node' && nodeIp}
      <button
        class="icon-refresh-btn"
        onclick={() => {
          reconnectNode(nodeIp);
          refreshHealth();
        }}
        title="Reconnect and refresh node telemetry"
        type="button"
      >
        ↻ Sync Live
      </button>
    {/if}
  </div>

  <!-- TAB: FLEET OVERVIEW -->
  {#if activeTab === 'fleet'}
    <div class="fleet-view">
      <!-- Register New Node -->
      <section class="section-card add-node-card">
        <h3>Register Fleet Node</h3>
        <div class="add-node-form">
          <input
            type="text"
            placeholder="Node address (192.168.1.50 or lumigrid.local)"
            bind:value={newIpInput}
            aria-invalid={fleetInputError ? 'true' : undefined}
            aria-describedby={fleetInputError ? 'fleet-address-error' : undefined}
            oninput={() => { if (fleetInputError) fleetInputError = ''; }}
            class="input-ip"
          />
          <input
            type="text"
            placeholder="Node Label (e.g. Stage Left Truss)"
            bind:value={newNameInput}
            class="input-name"
          />
          <button
            class="btn-add-fleet"
            onclick={handleAddFleetNode}
            disabled={!newIpInput.trim()}
            type="button"
          >
            + Add to Fleet
          </button>
        </div>
        {#if fleetInputError}
          <p id="fleet-address-error" class="field-error" role="alert">{fleetInputError}</p>
        {/if}
      </section>

      <!-- Fleet Nodes Table / Cards -->
      <section class="section-card fleet-table-card">
        <div class="card-header">
          <div class="fleet-header-title">
            <h3>Discovered Fleet Nodes</h3>
            <span class="fleet-count-tag">{savedFleet.length} configured</span>
          </div>
          {#if nodeIp}
            <button
              class="btn-scan-fleet"
              onclick={pollFleetDiscovery}
              disabled={isScanningFleet}
              type="button"
            >
              {isScanningFleet ? 'Scanning...' : '↻ Discover Fleet (mDNS)'}
            </button>
          {/if}
        </div>

        {#if savedFleet.length === 0}
          <div class="empty-fleet">
            <p>No nodes registered in fleet yet. Add a node IP above or connect to a node to discover the fleet via mDNS.</p>
          </div>
        {:else}
          <div class="fleet-table-wrap">
            <table class="fleet-table">
              <thead>
                <tr>
                  <th>Node Identity</th>
                  <th>IP Address</th>
                  <th>Presence</th>
                  <th>Hardware / FW</th>
                  <th>Last Seen</th>
                  <th>Health</th>
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {#each savedFleet as fn}
                  {@const isSelected = fn.ip === nodeIp}
                  {@const isConnected = isSelected && statusFresh}
                  <tr class:selected-row={isSelected}>
                    <td data-label="Node Identity">
                      <span class="fn-name">{fn.name}</span>
                      {#if fn.id}
                        <span class="fn-id mono">{fn.id}</span>
                      {/if}
                    </td>
                    <td class="mono" data-label="Address">{fn.ip}</td>
                    <td data-label="Presence">
                      {#if isConnected}
                        <span class="badge badge-live">● FOCUSED (LIVE)</span>
                      {:else if fn.presence === 'online'}
                        <span class="badge badge-live">● ONLINE</span>
                      {:else if fn.presence === 'stale'}
                        <span class="badge badge-stale">◐ STALE</span>
                      {:else if fn.presence === 'offline'}
                        <span class="badge badge-offline">○ OFFLINE</span>
                      {:else if isSelected && liveState.connection === 'connecting'}
                        <span class="badge badge-connecting">CONNECTING</span>
                      {:else}
                        <span class="badge badge-registered">REGISTERED</span>
                      {/if}
                    </td>
                    <td data-label="Hardware / FW">
                      <span class="mono fn-hw">
                        {fn.model || 'lg-led-v1'}{#if fn.firmware} (v{fn.firmware}){/if}
                      </span>
                    </td>
                    <td class="fn-time" data-label="Last Seen">{formatTimeAgo(fn.lastSeen)}</td>
                    <td data-label="Health">
                      {#if isSelected && liveState.health}
                        <span class="badge" class:badge-live={!liveState.health.output.fault} class:badge-fault={liveState.health.output.fault}>
                          {liveState.health.output.state}
                        </span>
                      {:else}
                        <span class="text-muted">—</span>
                      {/if}
                    </td>
                    <td class="fn-actions" data-label="Actions">
                      <button
                        class="btn-action-select"
                        onclick={() => handleSelectFleetNode(fn.ip)}
                        disabled={isSelected}
                        type="button"
                      >
                        {isSelected ? 'Focused' : 'Focus'}
                      </button>
                      <button
                        class="btn-action-rename"
                        onclick={() => handleRenameNode(fn.ip, fn.name)}
                        type="button"
                        title="Set local alias"
                      >
                        Set local alias
                      </button>
                      <button
                        class="btn-action-remove"
                        onclick={() => handleRemoveNode(fn.ip)}
                        type="button"
                        title="Remove from fleet"
                      >
                        Remove
                      </button>
                    </td>
                  </tr>
                {/each}
              </tbody>
            </table>
          </div>
        {/if}
      </section>
    </div>

  <!-- TAB: ACTIVE NODE VIEW -->
  {:else if !nodeIp}
    <div class="no-node-card">
      <div class="card-icon">⬡</div>
      <h2>No Active Node Selected</h2>
      <p>Select a node from the fleet list or add an IP address to establish live hardware control.</p>
      <button class="btn-primary" onclick={() => (activeTab = 'fleet')} type="button" style="margin-top: 1rem;">
        Open Fleet Devices
      </button>
    </div>
  {:else}
    <!-- ── NODE HEADER ──────────────────────────────────────────────────────── -->
    <header class="node-header">
      <div class="node-header-main">
        <div class="node-title-row">
          <div class="node-heading-copy">
            <h2 class="node-ip-heading">{activeFleetNode?.name || nodeIp}</h2>
            {#if activeFleetNode?.name && activeFleetNode.name !== nodeIp}
              <span class="node-address">{nodeIp}</span>
            {/if}
          </div>
          <span class="status-badge {statusFresh ? 'online' : liveState.connection === 'online' ? 'degraded' : liveState.connection}">
            <span class="dot"></span>
            {connectionLabel}
          </span>
          <span class="role-pill">
            {liveState.authenticated ? liveState.role.toUpperCase() : 'AUTH PENDING'}
          </span>
        </div>
        <div class="node-sub-meta">
          <span>Uptime: <strong>{formatUptime(liveState.health?.system.uptime_s || (liveState.snapshot ? Math.floor(liveState.snapshot.generatedAtMs / 1000) : undefined))}</strong></span>
          <span>·</span>
          <span>Firmware: <strong>{nodeInfo?.firmwareVersion || '—'}</strong></span>
          <span>·</span>
          <span>Outputs: <strong>{!hasStatus ? 'UNKNOWN' : liveState.snapshot?.output.enabled ? 'ENABLED' : 'DISABLED'}</strong></span>
          {#if liveState.health}
            <span>·</span>
            <span>FPS: <strong>{liveState.health.system.fps.toFixed(1)}</strong></span>
          {/if}
        </div>
        {#if !statusFresh}
          <p class="field-error" role="status">{hasStatus
            ? 'Status is stale. Showing the last received values; live controls are waiting for fresh status.'
            : 'Waiting for this node’s status. Output state and brightness are unknown.'}</p>
        {/if}
      </div>
    </header>

    <!-- ── LIVE OUTPUT & LED REPRESENTATION ──────────────────────────────────── -->
    <section class="section-card live-output-section">
      <div class="card-header">
        <div class="title-with-fps">
          <h3>Live LED Output</h3>
          <span class="frames-indicator">8 addressable outputs · live pixel preview</span>
        </div>
        <div class="status-flags">
          <span class="flag-pill" class:active={liveState.status?.outputsEnabled}>
            {!hasStatus ? 'OUTPUTS UNKNOWN' : liveState.status?.outputsEnabled ? 'OUTPUTS ON' : 'OUTPUTS OFF'}
          </span>
          <span class="flag-pill loop" class:active={liveState.status?.autoLoop}>
            {!hasStatus ? 'LOOP UNKNOWN' : liveState.status?.autoLoop ? 'AUTO LOOP' : 'LOOP OFF'}
          </span>
          <span class="flag-pill test" class:active={liveState.status?.testActive}>
            {!hasStatus ? 'TEST UNKNOWN' : liveState.status?.testActive ? 'TEST RUNNING' : 'STANDBY'}
          </span>
        </div>
      </div>

      <div class="canvas-wrap">
        <canvas
          bind:this={canvasEl}
          width={PIXELS_PER_LANE * 4}
          height={LANES * 12}
          class="live-canvas"
        ></canvas>
      </div>

      <div class="output-controls-bar">
        <div class="meta-item">
          <span class="meta-label">Active Effect</span>
          <span class="meta-value effect-name">{liveState.status?.effect || 'Unknown'}</span>
        </div>

        <div class="meta-item brightness-item">
          <div class="label-val">
            <span class="meta-label">Brightness</span>
            <span class="meta-value">{hasStatus ? `${localBri} / 255` : 'Unknown'}</span>
          </div>
          <input
            type="range"
            min="0"
            max="255"
            value={localBri}
            aria-label="Brightness"
            oninput={(e) => { localBri = Number(e.currentTarget.value); brightnessDirty = true; }}
            onchange={handleSetBrightness}
            class="bri-slider"
            disabled={actionLoading || !statusFresh || !liveState.status?.effect}
          />
          {#if brightnessDirty}
            <span class="stat-sub">{pendingBrightness !== null ? 'Awaiting confirmation' : 'Draft preserved'} · Node: {liveState.status?.brightness ?? 'unknown'}</span>
          {/if}
        </div>

        <div class="meta-item">
          <span class="meta-label">Output Stage</span>
          <span class="meta-value" class:fault={liveState.health?.output.fault}>
            {liveState.snapshot?.output.state ?? 'Unknown'}
          </span>
        </div>
      </div>
    </section>

    <!-- ── QUICK ACTIONS ────────────────────────────────────────────────────── -->
    <section class="section-card quick-actions-section">
      <h3>Quick Actions</h3>
      <div class="actions-grid">
        <button
          class="action-btn blackout-btn"
          onclick={handleBlackout}
          disabled={actionLoading || !liveState.authenticated}
          type="button"
        >
          <span class="btn-icon">⬛</span>
          <span class="btn-text">
            <strong>Request Blackout</strong>
            <small>Wait for node confirmation</small>
          </span>
        </button>

        <button
          class="action-btn"
          onclick={handleToggleAutoLoop}
          disabled={actionLoading || !statusFresh}
          type="button"
        >
          <span class="btn-icon">⟳</span>
          <span class="btn-text">
            <strong>Toggle Auto-Loop</strong>
            <small>{liveState.status?.autoLoop ? 'Disable auto-cycling' : 'Enable timeline loop'}</small>
          </span>
        </button>

        <a href="/dashboard/scenes" class="action-btn nav-action">
          <span class="btn-icon">◆</span>
          <span class="btn-text">
            <strong>Scene Studio</strong>
            <small>16 outputs · layered scenes</small>
          </span>
        </a>

        <a href="/dashboard/timeline" class="action-btn nav-action">
          <span class="btn-icon">▤</span>
          <span class="btn-text">
            <strong>Timeline Studio</strong>
            <small>16 tracks · 16-step sequencer</small>
          </span>
        </a>

        {#if isAdmin}
          <a href="/dashboard/factory" class="action-btn nav-action">
            <span class="btn-icon">⚙</span>
            <span class="btn-text">
              <strong>Factory Console</strong>
              <small>Burn-in & bench tests</small>
            </span>
          </a>
        {/if}

        <a href="/dashboard/diagnostics" class="action-btn nav-action">
          <span class="btn-icon">⚕</span>
          <span class="btn-text">
            <strong>Diagnostics</strong>
            <small>Health, logs & audit</small>
          </span>
        </a>
      </div>
    </section>

    <!-- ── HEALTH & TELEMETRY ───────────────────────────────────────────────── -->
    <section class="section-card health-section">
      <h3>System & Subsystem Health</h3>
      {#if !healthFresh}
        <p class="field-error" role="status">{liveState.health
          ? 'Health telemetry is stale. These are the last received values.'
          : 'Health telemetry is unavailable for this node.'}</p>
      {/if}
      <div class="health-grid">
        <div class="health-stat-card">
          <span class="stat-label">Free Heap</span>
          <span class="stat-value">{formatBytes(liveState.health?.system.free_heap)}</span>
          <span class="stat-sub">Min: {formatBytes(liveState.health?.system.min_free_heap)}</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Wi-Fi RSSI</span>
          <span class="stat-value">{liveState.health?.network.wifi_rssi ? `${liveState.health.network.wifi_rssi} dBm` : '—'}</span>
          <span class="stat-sub">RF link quality</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">NVS Storage</span>
          <span class="stat-value">{liveState.health?.nvs.used ?? '—'} entries</span>
          <span class="stat-sub">{liveState.health?.nvs.free ?? '—'} available</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Config State</span>
          <span class="stat-value" class:text-ok={liveState.health?.config.valid}>
            {!liveState.health ? 'Unknown' : liveState.health.config.valid ? 'Valid' : 'Degraded'}
          </span>
          <span class="stat-sub">{liveState.health?.config.persistence_state || 'Unavailable'}</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Output Stage</span>
          <span class="stat-value" class:text-fault={liveState.health?.output.fault}>
            {liveState.health?.output.state || 'Unknown'}
          </span>
          <span class="stat-sub">Estimated power: {estimatedPowerMw !== undefined && Number.isFinite(estimatedPowerMw) ? `${(estimatedPowerMw / 1000).toFixed(1)} W` : 'Unavailable'}</span>
          {#if powerEstimate?.estimateComplete === false}
            <span class="stat-sub text-fault">Incomplete estimate</span>
          {:else if !powerEstimate}
            <span class="stat-sub">Completeness unknown</span>
          {/if}
        </div>

        <div class="health-stat-card">
          <span class="stat-label">DMX Ingestion</span>
          <span class="stat-value">{liveState.health?.dmx.sources_active ?? '—'} active</span>
          <span class="stat-sub">{liveState.health?.dmx.packets_accepted ?? '—'} accepted</span>
        </div>
      </div>
    </section>

    <!-- ── CONNECTION INFO ─────────────────────────────────────────────────── -->
    <section class="section-card conn-info-section">
      <button
        class="conn-info-toggle"
        onclick={() => (showConnInfo = !showConnInfo)}
        type="button"
        aria-expanded={showConnInfo}
      >
        <h3>Connection Info</h3>
        <span class="toggle-caret" class:open={showConnInfo}>▾</span>
      </button>
      {#if showConnInfo}
        <div class="conn-info-grid">
          <div class="conn-row">
            <span class="conn-key">Node IP</span>
            <span class="conn-val mono">{nodeIp}</span>
          </div>
          <div class="conn-row">
            <span class="conn-key">Web UI URL</span>
            <a class="conn-val mono conn-link" href="http://{nodeIp}" target="_blank" rel="noopener">http://{nodeIp}</a>
          </div>
          {#if nodeInfo?.mac}
            <div class="conn-row">
              <span class="conn-key">MAC Address</span>
              <span class="conn-val mono">{nodeInfo.mac}</span>
            </div>
          {/if}
          <div class="conn-row">
            <span class="conn-key">Wi-Fi AP SSID</span>
            <span class="conn-val mono">{apSsid}</span>
          </div>
          <div class="conn-row">
            <span class="conn-key">AP Password</span>
            <span class="conn-val conn-hint">Per-device Wi-Fi key shown on the serial console</span>
          </div>
          {#if deviceConfig?.deviceName}
            <div class="conn-row">
              <span class="conn-key">Device Name</span>
              <span class="conn-val mono">{deviceConfig.deviceName}</span>
            </div>
          {/if}
        </div>
      {/if}
    </section>

    <!-- ── NETWORK CONFIGURATION & RECOVERY AP ───────────────────────────────── -->
    <NetworkConfigCard
      {nodeIp}
      {isAdmin}
      onNetworkChanged={() => {
        reconnectNode();
      }}
    />
  {/if}
</div>

<OperatorDialog
  open={fleetDialog?.kind === 'rename'}
  title="Rename fleet node"
  description={fleetDialog ? `Update the operator label for ${fleetDialog.ip}.` : ''}
  confirmLabel="Save Name"
  inputLabel="Node label"
  inputPlaceholder="e.g. Stage Left Truss"
  bind:value={fleetDialogValue}
  confirmDisabled={!fleetDialogValue.trim()}
  onconfirm={confirmFleetDialog}
  oncancel={closeFleetDialog}
/>

<OperatorDialog
  open={fleetDialog?.kind === 'remove'}
  title="Remove fleet node?"
  description={fleetDialog ? `Remove ${fleetDialog.currentName} (${fleetDialog.ip}) from this browser's registered fleet. This does not erase the physical node.` : ''}
  confirmLabel="Remove Node"
  tone="danger"
  onconfirm={confirmFleetDialog}
  oncancel={closeFleetDialog}
/>

<style>
  .overview-page {
    width: 100%;
    max-width: 1180px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  .toast-bar {
    position: fixed;
    top: 1.5rem;
    right: 2rem;
    background: rgba(29, 29, 31, 0.95);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.4);
    color: #f5f5f7;
    padding: 0.75rem 1.25rem;
    border-radius: 12px;
    font-size: 0.88rem;
    box-shadow: 0 12px 32px rgba(0, 0, 0, 0.6);
    z-index: 1000;
  }

  /* Mode Bar */
  .mode-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: var(--dash-surface);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    padding: 0.6rem 1rem;
    flex-wrap: wrap;
    gap: 0.75rem;
  }
  .mode-selector {
    display: flex;
    gap: 0.4rem;
  }
  .mode-btn {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    background: transparent;
    border: none;
    color: #86868b;
    padding: 0.45rem 0.85rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    border-radius: 8px;
    transition: all 0.15s ease;
  }
  .mode-btn:hover {
    color: #f5f5f7;
    background: rgba(255, 255, 255, 0.04);
  }
  .mode-btn.active {
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.12);
  }
  .mode-pill {
    font-size: 0.7rem;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
  }
  .mode-pill.mono {
    font-family: monospace;
    background: rgba(var(--dash-accent-rgb), 0.18);
    color: var(--dash-accent);
  }
  .mode-pill.count {
    background: rgba(255, 255, 255, 0.1);
    color: #f5f5f7;
  }

  .icon-refresh-btn {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    color: #f5f5f7;
    padding: 0.4rem 0.85rem;
    font-size: 0.8rem;
    cursor: pointer;
  }
  .icon-refresh-btn:hover {
    background: rgba(255, 255, 255, 0.1);
  }

  /* Fleet View */
  .fleet-view {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
  .add-node-form {
    display: flex;
    gap: 0.75rem;
    flex-wrap: wrap;
  }
  .input-ip {
    flex: 1;
    min-width: 220px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.5rem 0.75rem;
  }
  .input-name {
    flex: 1;
    min-width: 180px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: 0.85rem;
    padding: 0.5rem 0.75rem;
  }
  .btn-add-fleet {
    background: var(--dash-accent);
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0.5rem 1.2rem;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
  }
  .btn-add-fleet:hover:not(:disabled) {
    background: var(--dash-accent-hover);
  }
  .btn-add-fleet:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .field-error {
    margin: .55rem 0 0;
    color: #ff6961;
    font-size: .76rem;
    line-height: 1.4;
  }
  .input-ip[aria-invalid="true"] {
    border-color: rgba(255,69,58,.55);
    box-shadow: 0 0 0 3px rgba(255,69,58,.10);
  }

  .fleet-count-tag {
    font-size: 0.75rem;
    color: #86868b;
  }
  .fleet-table-wrap {
    overflow-x: auto;
  }
  .fleet-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.82rem;
  }
  .fleet-table th {
    text-align: left;
    padding: 0.65rem 0.75rem;
    color: #86868b;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }
  .fleet-table td {
    padding: 0.65rem 0.75rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  }
  .selected-row {
    background: rgba(var(--dash-accent-rgb), 0.08);
  }
  .fn-name {
    font-weight: 600;
    color: #f5f5f7;
  }
  .fn-time {
    color: #86868b;
    font-size: 0.75rem;
  }
  .badge-live {
    background: rgba(48, 209, 88, 0.15);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.35);
  }
  .badge-connecting {
    background: rgba(255, 159, 10, 0.15);
    color: #ff9f0a;
    border: 1px solid rgba(255, 159, 10, 0.35);
  }
  .badge-stale {
    background: rgba(255, 159, 10, 0.15);
    color: #ff9f0a;
    border: 1px solid rgba(255, 159, 10, 0.35);
  }
  .badge-offline {
    background: rgba(255, 255, 255, 0.05);
    color: #86868b;
    border: 1px solid rgba(255, 255, 255, 0.1);
  }
  .badge-registered {
    background: rgba(255, 255, 255, 0.05);
    color: #86868b;
    border: 1px solid rgba(255, 255, 255, 0.1);
  }
  .fn-id {
    display: block;
    font-size: 0.7rem;
    color: #737378;
    margin-top: 0.15rem;
  }
  .fn-hw {
    font-size: 0.76rem;
    color: #a1a1a6;
  }
  .fleet-header-title {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .btn-scan-fleet {
    background: rgba(var(--dash-accent-rgb), 0.12);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    border-radius: 6px;
    color: var(--dash-accent);
    padding: 0.3rem 0.75rem;
    font-size: 0.78rem;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-scan-fleet:hover:not(:disabled) {
    background: rgba(var(--dash-accent-rgb), 0.22);
    border-color: rgba(var(--dash-accent-rgb), 0.45);
  }
  .btn-scan-fleet:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .badge-fault {
    background: rgba(255, 69, 58, 0.15);
    color: #ff453a;
  }
  .fn-actions {
    display: flex;
    gap: 0.35rem;
  }
  .btn-action-select {
    background: rgba(var(--dash-accent-rgb), 0.15);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.35);
    border-radius: 6px;
    color: var(--dash-accent);
    padding: 0.25rem 0.55rem;
    font-size: 0.75rem;
    cursor: pointer;
  }
  .btn-action-select:disabled {
    background: rgba(var(--dash-accent-rgb), 0.4);
    color: #fff;
    cursor: default;
  }
  .btn-action-rename, .btn-action-remove {
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: #86868b;
    padding: 0.25rem 0.5rem;
    font-size: 0.75rem;
    cursor: pointer;
  }
  .btn-action-rename:hover { color: #f5f5f7; }
  .btn-action-remove:hover { color: #ff453a; border-color: rgba(255, 69, 58, 0.3); }

  .empty-fleet {
    padding: 2rem;
    text-align: center;
    color: #86868b;
    font-size: 0.85rem;
  }

  /* Node Focus Details */
  .no-node-card {
    text-align: center;
    padding: 4rem 2rem;
    background: var(--dash-surface);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-xl);
    box-shadow: var(--dash-shadow);
  }
  .no-node-card .card-icon {
    font-size: 3rem;
    color: var(--dash-accent);
    margin-bottom: 1rem;
  }

  .node-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 1.25rem 1.5rem;
    background: var(--dash-surface);
    backdrop-filter: blur(20px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    flex-wrap: wrap;
    gap: 1rem;
  }
  .node-title-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-bottom: 0.35rem;
    flex-wrap: wrap;
  }
  .node-heading-copy {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: .08rem;
  }
  .node-address {
    color: #737378;
    font: .72rem ui-monospace, SFMono-Regular, Menlo, monospace;
  }
  .node-ip-heading {
    font-size: 1.5rem;
    font-weight: 700;
    color: #f5f5f7;
    font-family: monospace;
    margin: 0;
  }
  .status-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    padding: 0.2rem 0.6rem;
    border-radius: 999px;
    font-size: 0.75rem;
    font-weight: 600;
  }
  .status-badge .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
  }
  .status-badge.online {
    background: rgba(48, 209, 88, 0.12);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.3);
  }
  .status-badge.online .dot {
    background: #30d158;
    box-shadow: 0 0 6px #30d158;
  }
  .status-badge.connecting, .status-badge.degraded {
    background: rgba(255, 149, 0, 0.12);
    color: #ff9500;
    border: 1px solid rgba(255, 149, 0, 0.3);
  }
  .status-badge.connecting .dot, .status-badge.degraded .dot { background: var(--dash-warm); }
  .status-badge.offline, .status-badge.auth-required, .status-badge.transport-blocked, .status-badge.idle, .status-badge.error {
    background: rgba(255, 69, 58, 0.12);
    color: #ff453a;
    border: 1px solid rgba(255, 69, 58, 0.3);
  }
  .status-badge.offline .dot, .status-badge.error .dot { background: var(--dash-danger); }

  .role-pill {
    font-size: 0.72rem;
    font-weight: 600;
    padding: 0.2rem 0.55rem;
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.08);
    color: #a1a1a6;
    letter-spacing: 0.05em;
  }
  .node-sub-meta {
    font-size: 0.82rem;
    color: #8e8e93;
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .node-sub-meta strong { color: #f5f5f7; }

  /* Section Card */
  .section-card {
    position: relative;
    overflow: hidden;
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    padding: 1.5rem;
  }
  .section-card::before {
    content: "";
    position: absolute;
    inset: 0 0 auto;
    height: 1px;
    background: linear-gradient(90deg, rgba(255,255,255,.18), transparent 58%);
    pointer-events: none;
  }
  .section-card h3 {
    margin: 0 0 1rem;
    font-size: 1.1rem;
    font-weight: 600;
    color: #f5f5f7;
  }

  /* Live Output */
  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1rem;
    flex-wrap: wrap;
    gap: 0.75rem;
  }
  .card-header h3 { margin: 0; }
  .frames-indicator {
    font-size: 0.78rem;
    color: #8e8e93;
    font-family: monospace;
    margin-left: 0.5rem;
  }
  .status-flags {
    display: flex;
    gap: 0.4rem;
  }
  .flag-pill {
    font-size: 0.72rem;
    padding: 0.2rem 0.55rem;
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.05);
    color: #8e8e93;
    font-weight: 600;
  }
  .flag-pill.active {
    background: rgba(48, 209, 88, 0.15);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.3);
  }
  .flag-pill.loop {
    background: rgba(0, 212, 255, 0.12);
    color: #00d4ff;
    border: 1px solid rgba(0, 212, 255, 0.3);
  }
  .flag-pill.test {
    background: rgba(255, 149, 0, 0.15);
    color: #ff9500;
    border: 1px solid rgba(255, 149, 0, 0.3);
  }
  .canvas-wrap {
    background: #0a0a0c;
    border-radius: 12px;
    overflow: hidden;
    border: 1px solid rgba(255, 255, 255, 0.08);
    height: 120px;
    display: flex;
    align-items: center;
  }
  .live-canvas {
    width: 100%;
    height: 100%;
    display: block;
  }
  .output-controls-bar {
    display: grid;
    grid-template-columns: 1fr 2fr 1fr;
    gap: 1.5rem;
    margin-top: 1.25rem;
    align-items: center;
  }
  @media (max-width: 768px) {
    .output-controls-bar { grid-template-columns: 1fr; }
  }
  .meta-item {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .meta-label {
    font-size: 0.75rem;
    color: #8e8e93;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .meta-value {
    font-size: 1rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .effect-name {
    color: #00d4ff;
    font-family: monospace;
  }
  .brightness-item .label-val {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .bri-slider {
    accent-color: var(--dash-accent);
    width: 100%;
    margin-top: 0.25rem;
  }

  /* Quick Actions Grid */
  .actions-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 1rem;
  }
  .action-btn {
    position: relative;
    overflow: hidden;
    display: flex;
    align-items: center;
    gap: 0.85rem;
    padding: 1rem 1.1rem;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 14px;
    color: #f5f5f7;
    text-decoration: none;
    cursor: pointer;
    text-align: left;
    transition: all 0.2s;
  }
  .action-btn:hover {
    background: rgba(255,255,255,.09);
    border-color: rgba(255,255,255,.16);
    transform: translateY(-2px);
    box-shadow: 0 12px 26px rgba(0,0,0,.22);
  }
  .action-btn.nav-action::after {
    content: "›";
    margin-left: auto;
    color: #636366;
    font-size: 1.25rem;
    transition: transform .18s ease, color .18s ease;
  }
  .action-btn.nav-action:hover::after {
    color: var(--dash-accent);
    transform: translateX(2px);
  }
  .action-btn.blackout-btn {
    background: rgba(255, 59, 48, 0.12);
    border-color: rgba(255, 59, 48, 0.35);
  }
  .action-btn.blackout-btn:hover {
    background: rgba(255, 59, 48, 0.22);
    border-color: rgba(255, 59, 48, 0.5);
  }
  .action-btn .btn-icon {
    font-size: 1.5rem;
    line-height: 1;
  }
  .action-btn .btn-text {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }
  .action-btn strong {
    font-size: 0.92rem;
    font-weight: 600;
  }
  .action-btn small {
    font-size: 0.75rem;
    color: #8e8e93;
  }

  /* Health Grid */
  .health-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 1rem;
  }
  .health-stat-card {
    background: linear-gradient(180deg, rgba(255,255,255,.035), rgba(255,255,255,.018));
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 12px;
    padding: 0.9rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    transition: transform .18s ease, border-color .18s ease, background .18s ease;
  }
  .health-stat-card:hover {
    transform: translateY(-1px);
    border-color: rgba(255,255,255,.11);
    background: rgba(255,255,255,.045);
  }
  .stat-label {
    font-size: 0.72rem;
    color: #8e8e93;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }
  .stat-value {
    font-size: 1.1rem;
    font-weight: 700;
    color: #f5f5f7;
    font-family: monospace;
  }
  .stat-sub {
    font-size: 0.72rem;
    color: #636366;
  }
  .mono { font-family: monospace; }
  .text-ok { color: #30d158; }
  .text-fault { color: #ff453a; }
  .btn-primary {
    background: var(--dash-accent);
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0.5rem 1.2rem;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
  }

  @media (max-width: 720px) {
    .mode-bar { align-items: stretch; }
    .mode-selector { width: 100%; display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .mode-btn { min-width: 0; justify-content: center; flex-wrap: wrap; white-space: normal; }
    .mode-pill.mono { max-width: 100%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .icon-refresh-btn { width: 100%; }
    .add-node-form { flex-direction: column; }
    .input-ip, .input-name { min-width: 0; width: 100%; }
    .btn-add-fleet { min-height: 42px; }
    .fleet-table-wrap { overflow: visible; }
    .fleet-table,
    .fleet-table tbody,
    .fleet-table tr,
    .fleet-table td {
      display: block;
      width: 100%;
    }
    .fleet-table thead { display: none; }
    .fleet-table {
      min-width: 0;
      border-collapse: separate;
      border-spacing: 0 10px;
    }
    .fleet-table tr {
      padding: .8rem;
      border: 1px solid rgba(255,255,255,.075);
      border-radius: 14px;
      background: linear-gradient(180deg, rgba(255,255,255,.035), rgba(255,255,255,.018));
    }
    .fleet-table tr.selected-row {
      border-color: rgba(var(--dash-accent-rgb),.28);
      background: linear-gradient(135deg, rgba(var(--dash-accent-rgb),.11), rgba(255,255,255,.018));
    }
    .fleet-table td {
      display: grid;
      grid-template-columns: 82px minmax(0, 1fr);
      align-items: center;
      gap: .65rem;
      padding: .42rem .2rem;
      border-bottom: 1px solid rgba(255,255,255,.04);
    }
    .fleet-table td:last-child { border-bottom: 0; }
    .fleet-table td::before {
      content: attr(data-label);
      color: #737378;
      font-size: .62rem;
      font-weight: 800;
      letter-spacing: .06em;
      text-transform: uppercase;
    }
    .fn-actions {
      display: grid;
      grid-template-columns: 82px repeat(3, minmax(0, 1fr));
      gap: .45rem;
    }
    .fn-actions::before {
      content: attr(data-label);
      align-self: center;
      color: #737378;
      font-size: .62rem;
      font-weight: 800;
      letter-spacing: .06em;
      text-transform: uppercase;
    }
    .fn-actions button { min-height: 38px; }
    .node-header { padding: 1rem; }
    .section-card { padding: 1rem; }
    .actions-grid, .health-grid { grid-template-columns: 1fr; }
  }

  /* ── Connection Info Panel ─────────────────────────────────────────────── */
  .conn-info-section {
    padding: 0;
  }

  .conn-info-toggle {
    width: 100%;
    background: transparent;
    border: none;
    padding: 1.1rem 1.25rem;
    display: flex;
    align-items: center;
    justify-content: space-between;
    cursor: pointer;
    color: inherit;
    gap: 0.5rem;
  }

  .conn-info-toggle h3 {
    margin: 0;
    font-size: 1rem;
    font-weight: 600;
    color: #f5f5f7;
  }

  .toggle-caret {
    font-size: 1.1rem;
    color: #86868b;
    transition: transform 0.2s ease;
    display: inline-block;
  }

  .toggle-caret.open {
    transform: rotate(180deg);
  }

  .conn-info-grid {
    padding: 0 1.25rem 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
  }

  .conn-row {
    display: flex;
    align-items: baseline;
    gap: 1rem;
    font-size: 0.88rem;
  }

  .conn-key {
    flex: 0 0 130px;
    font-size: 0.78rem;
    font-weight: 600;
    color: #86868b;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }

  .conn-val {
    color: #f5f5f7;
    font-size: 0.88rem;
  }

  .conn-val.mono {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 0.83rem;
  }

  .conn-val.conn-hint {
    color: #636366;
    font-style: italic;
  }

  .conn-link {
    color: var(--dash-accent);
    text-decoration: none;
  }

  .conn-link:hover {
    text-decoration: underline;
  }

</style>
