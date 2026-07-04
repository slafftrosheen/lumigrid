<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getNodeStatus, setBrightness, setPower, applyPreset, emergencyOff } from '$lib/api/wled';
  import type { NodeStatus } from '$lib/api/wled';

  // ── Feature 4: Multi-Node Fleet Manager ──────────────────────────────────────

  let nodeList  = $state<string[]>([]);
  let statuses  = $state<Record<string, NodeStatus>>({});
  let loading   = $state(true);
  let bulkBri   = $state(128);
  let bulkPreset = $state(1);
  let selected  = $state<Set<string>>(new Set());
  let polling: ReturnType<typeof setInterval> | null = null;

  // Track uptime events in localStorage for Feature 6
  function recordUptimeEvent(ip: string, online: boolean) {
    const key = `lg_uptime_${ip}`;
    const raw = localStorage.getItem(key);
    const events: { ts: number; online: boolean }[] = raw ? JSON.parse(raw) : [];
    const now = Date.now();
    // Only record if state changed or >5 min since last event
    const last = events[events.length - 1];
    if (!last || last.online !== online || now - last.ts > 300_000) {
      events.push({ ts: now, online });
      // Keep last 1000 events per node
      if (events.length > 1000) events.splice(0, events.length - 1000);
      localStorage.setItem(key, JSON.stringify(events));
    }
  }

  async function refreshAll() {
    loading = statuses && Object.keys(statuses).length === 0;
    const results = await Promise.all(nodeList.map(ip => getNodeStatus(ip)));
    const next: Record<string, NodeStatus> = {};
    for (const s of results) {
      next[s.ip] = s;
      recordUptimeEvent(s.ip, s.error === null);
    }
    statuses = next;
    loading = false;
  }

  onMount(() => {
    nodeList = JSON.parse(localStorage.getItem('lg_nodes') ?? '[]');
    if (nodeList.length) refreshAll();
    else loading = false;
    polling = setInterval(refreshAll, 10_000);
  });
  onDestroy(() => { if (polling) clearInterval(polling); });

  function toggleSelect(ip: string) {
    const s = new Set(selected);
    s.has(ip) ? s.delete(ip) : s.add(ip);
    selected = s;
  }
  function selectAll() { selected = new Set(nodeList); }
  function clearSel()  { selected = new Set(); }

  async function bulkAction(fn: (ip: string) => Promise<unknown>) {
    const targets = selected.size ? [...selected] : nodeList;
    await Promise.allSettled(targets.map(fn));
    await refreshAll();
  }

  function formatUptime(s: number): string {
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d) return `${d}d ${h}h`;
    if (h) return `${h}h ${m}m`;
    return `${m}m`;
  }

  function tempColor(t: number): string {
    if (t < 45) return '#34c759';
    if (t < 65) return '#ff9500';
    return '#ff453a';
  }
</script>

<svelte:head>
  <title>Fleet Manager — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Fleet Manager</h1>
      <p class="sub">{nodeList.length} node{nodeList.length !== 1 ? 's' : ''} configured · polling every 10 s</p>
    </div>
    <button class="refresh-btn" onclick={refreshAll}>↺ Refresh</button>
  </header>

  {#if loading}
    <div class="loading">
      <div class="spinner"></div>
      <span>Connecting to nodes…</span>
    </div>
  {:else if !nodeList.length}
    <div class="empty">
      <div class="empty-icon">⬡</div>
      <p>No nodes configured yet.</p>
      <p class="sub">Add a node IP in the sidebar to get started.</p>
    </div>
  {:else}
    <!-- Bulk controls -->
    <div class="bulk-bar">
      <div class="bulk-sel">
        <button class="sm-btn" onclick={selectAll}>All</button>
        <button class="sm-btn" onclick={clearSel}>None</button>
        <span class="bulk-count">{selected.size || nodeList.length} targeted</span>
      </div>
      <div class="bulk-actions">
        <label class="bulk-label">Bri
          <input type="range" min="0" max="255" bind:value={bulkBri} class="bri-range" />
          <span class="bri-val">{bulkBri}</span>
        </label>
        <button class="action-btn" onclick={() => bulkAction(ip => setBrightness(ip, bulkBri))}>Set Bri</button>
        <button class="action-btn" onclick={() => bulkAction(ip => setPower(ip, true))}>Power On</button>
        <button class="action-btn" onclick={() => bulkAction(ip => setPower(ip, false))}>Power Off</button>
        <label class="bulk-label">Preset #
          <input type="number" min="1" max="250" bind:value={bulkPreset} class="preset-input" />
        </label>
        <button class="action-btn" onclick={() => bulkAction(ip => applyPreset(ip, bulkPreset))}>Apply</button>
        <button class="action-btn danger" onclick={() => bulkAction(emergencyOff)}>⚠ E-Off</button>
      </div>
    </div>

    <!-- Node grid -->
    <div class="node-grid">
      {#each nodeList as ip}
        {@const s = statuses[ip]}
        {@const online = s?.error === null}
        <div
          class="node-card"
          class:online
          class:offline={!online}
          class:selected={selected.has(ip)}
          onclick={() => toggleSelect(ip)}
          role="checkbox"
          aria-checked={selected.has(ip)}
          tabindex="0"
          onkeydown={(e) => e.key === ' ' && toggleSelect(ip)}
        >
          <div class="node-card-head">
            <div class="status-dot" class:online class:offline={!online}></div>
            <span class="node-name">{s?.info?.name ?? ip}</span>
            <span class="latency">{s?.latency ?? '—'} ms</span>
          </div>

          <div class="node-ip">{ip}</div>

          {#if online && s?.info}
            <div class="node-stats">
              <div class="stat">
                <span class="stat-label">Uptime</span>
                <span class="stat-val">{formatUptime(s.info.uptime)}</span>
              </div>
              <div class="stat">
                <span class="stat-label">Version</span>
                <span class="stat-val">{s.info.ver}</span>
              </div>
              <div class="stat">
                <span class="stat-label">Heap</span>
                <span class="stat-val">{Math.round((s.info.freeheap ?? 0) / 1024)} KB</span>
              </div>
              {#if s.info.u?.Lumigrid_PCA9685}
                {@const pca = s.info.u.Lumigrid_PCA9685 as Record<string, unknown>}
                {#if Array.isArray(pca.temp) && pca.temp.length}
                  <div class="stat">
                    <span class="stat-label">Temp</span>
                    <span class="stat-val" style:color={tempColor(Number(pca.temp[0]))}>
                      {Number(pca.temp[0]).toFixed(1)} °C
                    </span>
                  </div>
                {/if}
              {/if}
              {#if s.state}
                <div class="stat">
                  <span class="stat-label">Power</span>
                  <span class="stat-val" class:on={s.state.on}>{s.state.on ? 'ON' : 'OFF'}</span>
                </div>
                <div class="stat">
                  <span class="stat-label">Bri</span>
                  <span class="stat-val">{s.state.bri}</span>
                </div>
              {/if}
            </div>
          {:else if !online}
            <div class="node-error">{s?.error ?? 'Connecting…'}</div>
          {/if}

          <!-- Quick per-card actions -->
          {#if online}
            <div class="card-actions">
              <button class="cta" onclick={(e) => { e.stopPropagation(); setPower(ip, !(s?.state?.on ?? true)).then(refreshAll); }}>
                {s?.state?.on ? 'Off' : 'On'}
              </button>
              <a href="/dashboard/channels" class="cta link" onclick={(e) => { e.stopPropagation(); localStorage.setItem('lg_node_ip', ip); }}>
                Control
              </a>
            </div>
          {/if}
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .page { max-width: 1100px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .refresh-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 999px; color: #f5f5f7; padding: .5rem 1rem; cursor: pointer; font-size: .85rem; transition: background .2s; }
  .refresh-btn:hover { background: rgba(255,255,255,.14); }

  /* Loading */
  .loading { display: flex; align-items: center; gap: .75rem; color: #a1a1a6; padding: 3rem; justify-content: center; }
  .spinner { width: 20px; height: 20px; border: 2px solid rgba(255,255,255,.1); border-top-color: #ff2d55; border-radius: 50%; animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }

  /* Empty */
  .empty { text-align: center; padding: 4rem 2rem; color: #a1a1a6; }
  .empty-icon { font-size: 3rem; background: linear-gradient(135deg, #ff2d55, #00d4ff); -webkit-background-clip: text; background-clip: text; color: transparent; margin-bottom: .5rem; }

  /* Bulk bar */
  .bulk-bar { display: flex; align-items: center; gap: 1rem; flex-wrap: wrap; padding: .75rem 1rem; background: rgba(29,29,31,.7); border: 1px solid rgba(255,255,255,.08); border-radius: 14px; margin-bottom: 1.25rem; }
  .bulk-sel { display: flex; align-items: center; gap: .5rem; }
  .bulk-actions { display: flex; align-items: center; gap: .5rem; flex-wrap: wrap; flex: 1; }
  .bulk-count { font-size: .8rem; color: #a1a1a6; }
  .bulk-label { display: flex; align-items: center; gap: .4rem; font-size: .82rem; color: #a1a1a6; }
  .sm-btn { background: rgba(255,255,255,.07); border: 1px solid rgba(255,255,255,.10); border-radius: 6px; color: #f5f5f7; font-size: .75rem; padding: .25rem .55rem; cursor: pointer; }
  .sm-btn:hover { background: rgba(255,255,255,.13); }
  .action-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 8px; color: #f5f5f7; font-size: .8rem; padding: .35rem .7rem; cursor: pointer; white-space: nowrap; transition: background .2s; }
  .action-btn:hover { background: rgba(255,45,85,.25); border-color: rgba(255,45,85,.4); }
  .action-btn.danger { border-color: rgba(255,69,58,.4); color: #ff453a; }
  .action-btn.danger:hover { background: rgba(255,69,58,.25); }
  .bri-range { width: 80px; accent-color: #ff2d55; }
  .bri-val { min-width: 2.5ch; color: #f5f5f7; font-size: .82rem; }
  .preset-input { width: 50px; background: rgba(255,255,255,.06); border: 1px solid rgba(255,255,255,.12); border-radius: 6px; color: #f5f5f7; font-size: .82rem; padding: .25rem .4rem; text-align: center; }

  /* Node grid */
  .node-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 1rem; }
  .node-card {
    background: rgba(29,29,31,.72);
    backdrop-filter: blur(16px);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 18px;
    padding: 1rem;
    cursor: pointer;
    transition: border-color .2s, box-shadow .2s, transform .2s;
    display: flex;
    flex-direction: column;
    gap: .6rem;
  }
  .node-card:hover { border-color: rgba(255,255,255,.18); transform: translateY(-1px); }
  .node-card.selected { border-color: #ff2d55; box-shadow: 0 0 0 1px rgba(255,45,85,.3); }
  .node-card.offline { opacity: .55; }

  .node-card-head { display: flex; align-items: center; gap: .5rem; }
  .status-dot { width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0; }
  .status-dot.online { background: #34c759; box-shadow: 0 0 6px rgba(52,199,89,.6); }
  .status-dot.offline { background: #ff453a; }
  .node-name { flex: 1; font-weight: 600; font-size: .9rem; color: #f5f5f7; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .latency { font-size: .72rem; color: #a1a1a6; font-family: monospace; }
  .node-ip { font-size: .75rem; color: #a1a1a6; font-family: monospace; }
  .node-stats { display: grid; grid-template-columns: 1fr 1fr; gap: .35rem .5rem; }
  .stat { display: flex; flex-direction: column; gap: .05rem; }
  .stat-label { font-size: .65rem; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; }
  .stat-val { font-size: .82rem; color: #f5f5f7; font-weight: 600; }
  .stat-val.on { color: #34c759; }
  .node-error { font-size: .75rem; color: #ff453a; font-family: monospace; }
  .card-actions { display: flex; gap: .5rem; margin-top: .25rem; }
  .cta { flex: 1; background: rgba(255,45,85,.75); border: none; border-radius: 8px; color: #fff; font-size: .78rem; font-weight: 600; padding: .4rem; cursor: pointer; text-align: center; text-decoration: none; transition: background .2s; }
  .cta:hover { background: #ff3b30; }
  .cta.link { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); color: #f5f5f7; }
  .cta.link:hover { background: rgba(255,255,255,.14); }
</style>
