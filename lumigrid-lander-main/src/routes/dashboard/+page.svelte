<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import {
    getNodeLiveState,
    getNodeLiveSnapshot,
    nodePixelStore,
    connect,
    refreshHealth,
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
  } from '$lib/api/node';

  let nodeIp = $state('');
  let liveState = $state<NodeLiveState>(getNodeLiveSnapshot());
  let canvasEl = $state<HTMLCanvasElement | null>(null);
  let rafId = 0;
  let unsubscribeState: (() => void) | null = null;
  let unsubscribePixels: (() => void) | null = null;
  let actionToast = $state('');
  let actionLoading = $state(false);
  let localBri = $state(128);

  function toast(msg: string) {
    actionToast = msg;
    setTimeout(() => {
      if (actionToast === msg) actionToast = '';
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
    if (b >= 1024) return `${(b / 1024).toFixed(0)} KB`;
    return `${b} B`;
  }

  async function handleBlackout() {
    if (!nodeIp || actionLoading) return;
    actionLoading = true;
    try {
      await postFactoryAbort(nodeIp);
      toast('Emergency blackout applied.');
      refreshHealth(nodeIp).catch(() => {});
    } catch (e) {
      toast(`Blackout failed: ${e}`);
    }
    actionLoading = false;
  }

  async function handleToggleAutoLoop() {
    if (!nodeIp || actionLoading) return;
    actionLoading = true;
    const nextState = !(liveState.status?.autoLoop ?? false);
    try {
      await postFactoryAutoLoop(nodeIp, nextState);
      toast(`Auto loop ${nextState ? 'enabled' : 'disabled'}`);
    } catch (e) {
      toast(`Toggle failed: ${e}`);
    }
    actionLoading = false;
  }

  async function handleSetBrightness(e: Event) {
    const val = Number((e.target as HTMLInputElement).value);
    localBri = val;
    if (!nodeIp) return;
    try {
      await postFactoryEffect(nodeIp, (liveState.status?.effect || 'identity') as any, val);
    } catch (err) {
      // transient
    }
  }

  // Live LED Output Canvas Rendering
  let currentPixelBuf: Uint8Array = new Uint8Array(BUF_SIZE);

  function renderLeds() {
    if (!canvasEl) return;
    const ctx = canvasEl.getContext('2d');
    if (!ctx) return;

    const width = canvasEl.width;
    const height = canvasEl.height;
    ctx.fillStyle = '#0a0a0c';
    ctx.fillRect(0, 0, width, height);

    const laneH = height / LANES;
    const pixelW = width / PIXELS_PER_LANE;

    for (let lane = 0; lane < LANES; lane++) {
      const laneOffset = lane * PIXELS_PER_LANE * 3;
      const y = lane * laneH;

      for (let px = 0; px < PIXELS_PER_LANE; px++) {
        const off = laneOffset + px * 3;
        const r = currentPixelBuf[off] ?? 0;
        const g = currentPixelBuf[off + 1] ?? 0;
        const b = currentPixelBuf[off + 2] ?? 0;

        if (r > 0 || g > 0 || b > 0) {
          ctx.fillStyle = `rgb(${r},${g},${b})`;
          ctx.fillRect(px * pixelW, y + 1, Math.max(1, pixelW - 0.5), laneH - 2);
        } else {
          ctx.fillStyle = 'rgba(255,255,255,0.02)';
          ctx.fillRect(px * pixelW, y + 2, Math.max(1, pixelW - 0.5), laneH - 4);
        }
      }
    }

    rafId = requestAnimationFrame(renderLeds);
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    if (nodeIp) {
      connect(nodeIp);
      refreshHealth(nodeIp).catch(() => {});
    }

    unsubscribeState = getNodeLiveState().subscribe(val => {
      liveState = val;
      if (val.status?.brightness !== undefined && val.status.brightness > 0) {
        localBri = val.status.brightness;
      }
    });

    unsubscribePixels = nodePixelStore.subscribe(buf => {
      currentPixelBuf = buf;
    });

    if (canvasEl) {
      canvasEl.width = canvasEl.clientWidth || 864;
      canvasEl.height = canvasEl.clientHeight || 120;
    }
    rafId = requestAnimationFrame(renderLeds);
  });

  onDestroy(() => {
    if (rafId) cancelAnimationFrame(rafId);
    unsubscribeState?.();
    unsubscribePixels?.();
  });
</script>

<svelte:head>
  <title>Node Overview — LumiGrid</title>
</svelte:head>

<div class="overview-page">
  {#if actionToast}
    <div class="toast-bar">{actionToast}</div>
  {/if}

  {#if !nodeIp}
    <div class="no-node-card">
      <div class="card-icon">⬡</div>
      <h2>No Active Node Selected</h2>
      <p>Select or enter a LumiGrid LED node IP in the sidebar to start live monitoring and control.</p>
    </div>
  {:else}
    <!-- ── NODE HEADER ──────────────────────────────────────────────────────── -->
    <header class="node-header">
      <div class="header-left">
        <div class="node-title-row">
          <span class="node-ip-heading">{nodeIp}</span>
          <span class="status-badge {liveState.connection}">
            <span class="dot"></span>
            {liveState.connection === 'connected' ? 'Connected' : liveState.connection === 'connecting' ? 'Connecting' : 'Disconnected'}
          </span>
          <span class="role-tag {liveState.role}">
            {liveState.role.toUpperCase()}
          </span>
        </div>
        <div class="header-sub-row">
          <span>Uptime: <strong>{formatUptime(liveState.health?.system.uptime_s)}</strong></span>
          <span class="sep">·</span>
          <span>Output: <strong class:text-fault={liveState.health?.output.fault}>{liveState.health?.output.state ?? 'Ready'}</strong></span>
          <span class="sep">·</span>
          <span>FPS: <strong>{liveState.health?.system.fps ?? 0}</strong></span>
        </div>
      </div>
      <div class="header-right">
        <button class="icon-refresh-btn" onclick={() => { connect(nodeIp); refreshHealth(nodeIp); }} title="Refresh node state">
          ↺ Refresh
        </button>
      </div>
    </header>

    <!-- ── LIVE OUTPUT ──────────────────────────────────────────────────────── -->
    <section class="section-card live-output-card">
      <div class="card-header">
        <div class="header-title-group">
          <h3>Live LED Output</h3>
          <span class="frames-indicator">
            {liveState.frameCount} frames received
          </span>
        </div>
        <div class="status-flags">
          <span class="flag-pill" class:active={liveState.status?.outputsEnabled}>
            {liveState.status?.outputsEnabled ? 'Outputs Active' : 'Outputs Inactive'}
          </span>
          {#if liveState.status?.autoLoop}
            <span class="flag-pill loop">Auto-Loop</span>
          {/if}
          {#if liveState.status?.testActive}
            <span class="flag-pill test">Test Mode</span>
          {/if}
        </div>
      </div>

      <div class="canvas-wrap">
        <canvas bind:this={canvasEl} class="live-canvas"></canvas>
      </div>

      <div class="output-controls-bar">
        <div class="meta-item">
          <span class="meta-label">Active Effect</span>
          <span class="meta-value effect-name">{liveState.status?.effect || 'idle'}</span>
        </div>

        <div class="meta-item brightness-item">
          <div class="label-val">
            <span class="meta-label">Master Brightness</span>
            <span class="meta-value">{Math.round((localBri / 255) * 100)}%</span>
          </div>
          <input
            type="range"
            min="0"
            max="255"
            value={localBri}
            onchange={handleSetBrightness}
            class="bri-slider"
          />
        </div>

        <div class="meta-item">
          <span class="meta-label">Topology</span>
          <span class="meta-value">{LANES} Lanes × {PIXELS_PER_LANE} px</span>
        </div>
      </div>
    </section>

    <!-- ── QUICK ACTIONS ────────────────────────────────────────────────────── -->
    <section class="section-card quick-actions-card">
      <h3>Quick Actions</h3>
      <div class="actions-grid">
        <button class="action-btn blackout-btn" onclick={handleBlackout} disabled={actionLoading}>
          <span class="btn-icon">⬛</span>
          <span class="btn-text">
            <strong>BLACKOUT</strong>
            <small>Emergency abort all outputs</small>
          </span>
        </button>

        <button class="action-btn toggle-btn" onclick={handleToggleAutoLoop} disabled={actionLoading}>
          <span class="btn-icon">⟳</span>
          <span class="btn-text">
            <strong>Auto-Loop</strong>
            <small>{liveState.status?.autoLoop ? 'Stop rotation' : 'Enable sequence'}</small>
          </span>
        </button>

        <a href="/dashboard/scenes" class="action-btn nav-action">
          <span class="btn-icon">◆</span>
          <span class="btn-text">
            <strong>Scene Studio</strong>
            <small>Compose 8-zone presets</small>
          </span>
        </a>

        <a href="/dashboard/timeline" class="action-btn nav-action">
          <span class="btn-icon">▤</span>
          <span class="btn-text">
            <strong>Timeline</strong>
            <small>Beat grid sequencer</small>
          </span>
        </a>

        <a href="/dashboard/factory" class="action-btn nav-action">
          <span class="btn-icon">⚙</span>
          <span class="btn-text">
            <strong>Factory Console</strong>
            <small>Burn-in & bench tests</small>
          </span>
        </a>

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
      <div class="health-grid">
        <div class="health-stat-card">
          <span class="stat-label">Free Heap</span>
          <span class="stat-value">{formatBytes(liveState.health?.system.free_heap)}</span>
          <span class="stat-sub">Min free: {formatBytes(liveState.health?.system.min_free_heap)}</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Wi-Fi RSSI</span>
          <span class="stat-value">{liveState.health?.network.wifi_rssi ? `${liveState.health.network.wifi_rssi} dBm` : '—'}</span>
          <span class="stat-sub">Signal status</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">NVS Storage</span>
          <span class="stat-value">{liveState.health?.nvs.used ?? 0} entries</span>
          <span class="stat-sub">{liveState.health?.nvs.free ?? 0} available</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Config State</span>
          <span class="stat-value" class:text-ok={liveState.health?.config.valid}>
            {liveState.health?.config.valid ? 'Valid' : 'Degraded'}
          </span>
          <span class="stat-sub">{liveState.health?.config.persistence_state || 'NVS'}</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">Output Subsystem</span>
          <span class="stat-value" class:text-fault={liveState.health?.output.fault}>
            {liveState.health?.output.state || 'Healthy'}
          </span>
          <span class="stat-sub">Power: {liveState.health?.output.current_power_mw ? `${(liveState.health.output.current_power_mw / 1000).toFixed(1)}W` : '0W'}</span>
        </div>

        <div class="health-stat-card">
          <span class="stat-label">DMX Ingestion</span>
          <span class="stat-value">{liveState.health?.dmx.sources_active ?? 0} active</span>
          <span class="stat-sub">{liveState.health?.dmx.packets_accepted ?? 0} accepted</span>
        </div>
      </div>
    </section>
  {/if}
</div>

<style>
  .overview-page {
    max-width: 1100px;
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
  }

  .toast-bar {
    position: fixed;
    top: 1.5rem;
    right: 2rem;
    background: rgba(29, 29, 31, 0.95);
    border: 1px solid rgba(255, 45, 85, 0.4);
    color: #f5f5f7;
    padding: 0.75rem 1.25rem;
    border-radius: 12px;
    font-size: 0.88rem;
    box-shadow: 0 12px 32px rgba(0, 0, 0, 0.6);
    z-index: 1000;
  }

  .no-node-card {
    text-align: center;
    padding: 4rem 2rem;
    background: rgba(29, 29, 31, 0.6);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 20px;
  }
  .no-node-card .card-icon {
    font-size: 3rem;
    color: #ff2d55;
    margin-bottom: 1rem;
  }
  .no-node-card h2 {
    color: #f5f5f7;
    margin: 0 0 0.5rem;
  }
  .no-node-card p {
    color: #a1a1a6;
    margin: 0;
    font-size: 0.95rem;
  }

  /* Node Header */
  .node-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 1.25rem 1.5rem;
    background: rgba(29, 29, 31, 0.75);
    backdrop-filter: blur(20px);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 18px;
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
  .node-ip-heading {
    font-size: 1.5rem;
    font-weight: 700;
    color: #f5f5f7;
    font-family: monospace;
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
  .status-badge.connected {
    background: rgba(48, 209, 88, 0.12);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.3);
  }
  .status-badge.connected .dot { background: #30d158; box-shadow: 0 0 6px rgba(48, 209, 88, 0.8); }
  .status-badge.connecting {
    background: rgba(0, 212, 255, 0.12);
    color: #00d4ff;
    border: 1px solid rgba(0, 212, 255, 0.3);
  }
  .status-badge.connecting .dot { background: #00d4ff; }
  .status-badge.disconnected {
    background: rgba(142, 142, 147, 0.12);
    color: #8e8e93;
    border: 1px solid rgba(142, 142, 147, 0.3);
  }
  .status-badge.disconnected .dot { background: #8e8e93; }
  .role-tag {
    font-size: 0.68rem;
    font-weight: 700;
    padding: 0.15rem 0.5rem;
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.08);
    color: #a1a1a6;
    letter-spacing: 0.05em;
  }
  .role-tag.admin {
    background: rgba(255, 149, 0, 0.15);
    color: #ff9500;
    border: 1px solid rgba(255, 149, 0, 0.3);
  }
  .header-sub-row {
    font-size: 0.82rem;
    color: #8e8e93;
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .header-sub-row strong { color: #f5f5f7; }
  .header-sub-row .sep { color: rgba(255, 255, 255, 0.15); }
  .text-fault { color: #ff453a !important; }
  .text-ok { color: #30d158 !important; }
  .icon-refresh-btn {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 10px;
    color: #f5f5f7;
    padding: 0.5rem 0.9rem;
    font-size: 0.82rem;
    cursor: pointer;
    transition: background 0.2s, border-color 0.2s;
  }
  .icon-refresh-btn:hover {
    background: rgba(255, 255, 255, 0.12);
    border-color: rgba(255, 255, 255, 0.2);
  }

  /* Section Card */
  .section-card {
    background: rgba(29, 29, 31, 0.7);
    backdrop-filter: blur(16px);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 18px;
    padding: 1.5rem;
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
    accent-color: #ff2d55;
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
    background: rgba(255, 255, 255, 0.09);
    border-color: rgba(255, 255, 255, 0.16);
    transform: translateY(-2px);
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
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 12px;
    padding: 0.9rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
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
</style>
