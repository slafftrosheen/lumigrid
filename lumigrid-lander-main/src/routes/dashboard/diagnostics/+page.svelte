<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getHealth, getLogs, getAuditLog } from '$lib/api/node';
  import type { HealthSnapshot, AuditEvent } from '$lib/api/node';

  let nodeIp = $state('');
  let loading = $state(true);
  let error = $state('');
  let health = $state<HealthSnapshot | null>(null);
  let logs = $state('');
  let audit = $state<AuditEvent[]>([]);
  let autoRefresh = $state(false);
  let refreshTimer: ReturnType<typeof setInterval> | null = null;
  let activeTab = $state<'health' | 'logs' | 'audit'>('health');

  async function load() {
    error = '';
    try {
      const [h, l, a] = await Promise.all([
        getHealth(nodeIp).catch(() => null),
        getLogs(nodeIp).catch(() => ''),
        getAuditLog(nodeIp).catch(() => [] as AuditEvent[]),
      ]);
      health = h;
      logs = l;
      audit = a;
    } catch (e) {
      error = String(e);
    }
    loading = false;
  }

  function toggleAutoRefresh() {
    autoRefresh = !autoRefresh;
    if (autoRefresh) {
      refreshTimer = setInterval(load, 3000);
    } else if (refreshTimer) {
      clearInterval(refreshTimer);
      refreshTimer = null;
    }
  }

  function formatUptime(s: number): string {
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  function formatBytes(b: number): string {
    if (b >= 1048576) return `${(b / 1048576).toFixed(1)} MB`;
    if (b >= 1024) return `${(b / 1024).toFixed(1)} KB`;
    return `${b} B`;
  }

  function formatPower(mw: number): string {
    if (mw >= 1000) return `${(mw / 1000).toFixed(1)} W`;
    return `${mw} mW`;
  }

  function rssiLabel(rssi: number): string {
    if (rssi >= -50) return 'Excellent';
    if (rssi >= -60) return 'Good';
    if (rssi >= -70) return 'Fair';
    return 'Weak';
  }

  function rssiColor(rssi: number): string {
    if (rssi >= -50) return '#30d158';
    if (rssi >= -60) return '#30d158';
    if (rssi >= -70) return '#ff9500';
    return '#ff453a';
  }

  function statusColor(ok: boolean): string {
    return ok ? '#30d158' : '#ff453a';
  }

  function controlSourceName(cs: number): string {
    const names = ['None', 'Scheduler', 'DMX', 'Manual', 'Preset', 'Sequence'];
    return names[cs] ?? `Unknown(${cs})`;
  }

  function auditRoleName(role: number): string {
    const names = ['Guest', 'Operator', 'Installer', 'Admin'];
    return names[role] ?? `Role(${role})`;
  }

  function auditTime(ts: number): string {
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString();
  }

  onMount(() => {
    nodeIp = localStorage.getItem('lg_node_ip') ?? '';
    if (nodeIp) load();
    else loading = false;
  });

  onDestroy(() => {
    if (refreshTimer) clearInterval(refreshTimer);
  });
</script>

{#if !nodeIp}
  <p class="loading">No node selected. Add a node IP in the sidebar.</p>
{:else if loading}
  <p class="loading">Loading diagnostics…</p>
{:else}
  <div class="diag-root">
    <header class="diag-header">
      <div>
        <h2>System Health</h2>
        <p class="diag-sub">{nodeIp} · Schema v{health?.schema_version ?? '?'}</p>
      </div>
      <div class="header-actions">
        <label class="auto-refresh-toggle">
          <input type="checkbox" checked={autoRefresh} onchange={toggleAutoRefresh} />
          <span>Auto-refresh</span>
        </label>
        <button class="btn-refresh" onclick={load}>Refresh</button>
      </div>
    </header>

    {#if error}
      <div class="error-bar-inline">{error}</div>
    {/if}

    <!-- Tab bar -->
    <div class="tab-bar">
      <button class="tab" class:active={activeTab === 'health'} onclick={() => activeTab = 'health'}>Health</button>
      <button class="tab" class:active={activeTab === 'logs'} onclick={() => activeTab = 'logs'}>Logs</button>
      <button class="tab" class:active={activeTab === 'audit'} onclick={() => activeTab = 'audit'}>Audit</button>
    </div>

    {#if activeTab === 'health' && health}
      <!-- System confidence -->
      <div class="confidence-card">
        <div class="confidence-header">
          <span class="confidence-label">System Confidence</span>
          <span class="confidence-state" style:color={statusColor(health.startup.state === 'Running')}>
            {health.startup.state}
          </span>
        </div>
        <div class="confidence-grid">
          <div class="conf-item">
            <span class="conf-label">Startup</span>
            <span class="conf-val" style:color={statusColor(health.startup.state === 'Running')}>
              {health.startup.state}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Config</span>
            <span class="conf-val" style:color={statusColor(health.config.valid)}>
              {health.config.valid ? 'Valid' : 'Error'}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Output</span>
            <span class="conf-val" style:color={statusColor(!health.output.fault)}>
              {health.output.state}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">E-Stop</span>
            <span class="conf-val" style:color={statusColor(!health.output.estop_latched)}>
              {health.output.estop_latched ? 'Latched' : 'Clear'}
            </span>
          </div>
        </div>
      </div>

      <!-- Stats grid -->
      <div class="stats-grid">
        <!-- System -->
        <div class="stat-card">
          <h3>System</h3>
          <div class="stat-row"><span>Uptime</span><span class="stat-val">{formatUptime(health.system.uptime_s)}</span></div>
          <div class="stat-row"><span>Free Heap</span><span class="stat-val">{formatBytes(health.system.free_heap)}</span></div>
          <div class="stat-row"><span>Min Free Heap</span><span class="stat-val">{formatBytes(health.system.min_free_heap)}</span></div>
          <div class="stat-row"><span>FPS</span><span class="stat-val">{health.system.fps.toFixed(1)}</span></div>
          <div class="stat-row"><span>Reset Reason</span><span class="stat-val">{health.system.reset_reason}</span></div>
        </div>

        <!-- Network -->
        <div class="stat-card">
          <h3>Network</h3>
          <div class="stat-row"><span>WiFi RSSI</span><span class="stat-val" style:color={rssiColor(health.network.wifi_rssi)}>{health.network.wifi_rssi} dBm</span></div>
          <div class="stat-row"><span>Signal</span><span class="stat-val" style:color={rssiColor(health.network.wifi_rssi)}>{rssiLabel(health.network.wifi_rssi)}</span></div>
        </div>

        <!-- NVS -->
        <div class="stat-card">
          <h3>Storage (NVS)</h3>
          <div class="stat-row"><span>Used</span><span class="stat-val">{health.nvs.used}</span></div>
          <div class="stat-row"><span>Free</span><span class="stat-val">{health.nvs.free}</span></div>
          <div class="stat-row"><span>Usage</span><span class="stat-val">{health.nvs.used + health.nvs.free > 0 ? ((health.nvs.used / (health.nvs.used + health.nvs.free)) * 100).toFixed(0) : 0}%</span></div>
        </div>

        <!-- Config -->
        <div class="stat-card">
          <h3>Configuration</h3>
          <div class="stat-row"><span>Valid</span><span class="stat-val" style:color={statusColor(health.config.valid)}>{health.config.valid ? 'Yes' : 'No'}</span></div>
          <div class="stat-row"><span>Errors</span><span class="stat-val">{health.config.error_count}</span></div>
          <div class="stat-row"><span>Persistence</span><span class="stat-val">{health.config.persistence_state}</span></div>
          <div class="stat-row"><span>Primary</span><span class="stat-val" style:color={statusColor(health.config.primary_valid)}>{health.config.primary_valid ? 'OK' : 'Invalid'}</span></div>
          <div class="stat-row"><span>Backup</span><span class="stat-val" style:color={statusColor(health.config.backup_valid)}>{health.config.backup_valid ? 'OK' : 'Invalid'}</span></div>
          {#if health.config.first_error}
            <div class="stat-row"><span>First Error</span><span class="stat-val err">{health.config.first_error}</span></div>
          {/if}
        </div>

        <!-- Output -->
        <div class="stat-card wide">
          <h3>Output</h3>
          <div class="stat-grid-2col">
            <div class="stat-row"><span>State</span><span class="stat-val">{health.output.state}</span></div>
            <div class="stat-row"><span>Fault</span><span class="stat-val" style:color={statusColor(!health.output.fault)}>{health.output.fault ? 'YES' : 'No'}</span></div>
            <div class="stat-row"><span>Fault Events</span><span class="stat-val">{health.output.fault_events}</span></div>
            <div class="stat-row"><span>Failed Commits</span><span class="stat-val">{health.output.failed_commits}</span></div>
            <div class="stat-row"><span>Control Source</span><span class="stat-val">{controlSourceName(health.output.control_source)}</span></div>
            <div class="stat-row"><span>Active Preset</span><span class="stat-val">P{health.output.active_preset}</span></div>
            <div class="stat-row"><span>Live Objects</span><span class="stat-val">{health.output.live_objects}</span></div>
            <div class="stat-row"><span>Power</span><span class="stat-val">{formatPower(health.output.current_power_mw)}</span></div>
            <div class="stat-row"><span>E-Stop Latched</span><span class="stat-val" style:color={statusColor(!health.output.estop_latched)}>{health.output.estop_latched ? 'YES' : 'No'}</span></div>
            <div class="stat-row"><span>Runtime E-Stop</span><span class="stat-val" style:color={statusColor(!health.output.runtime_estop)}>{health.output.runtime_estop ? 'YES' : 'No'}</span></div>
            <div class="stat-row"><span>Throttling</span><span class="stat-val">{health.output.throttling_events}</span></div>
            <div class="stat-row"><span>Watchdog Fallback</span><span class="stat-val" style:color={statusColor(!health.output.watchdog_fallback)}>{health.output.watchdog_fallback ? 'YES' : 'No'}</span></div>
          </div>
        </div>

        <!-- DMX -->
        <div class="stat-card wide">
          <h3>DMX</h3>
          <div class="stat-grid-2col">
            <div class="stat-row"><span>Sources Active</span><span class="stat-val">{health.dmx.sources_active}</span></div>
            <div class="stat-row"><span>Universes Active</span><span class="stat-val">{health.dmx.universes_active}</span></div>
            <div class="stat-row"><span>Sources Dropped</span><span class="stat-val">{health.dmx.sources_dropped}</span></div>
            <div class="stat-row"><span>Packets Accepted</span><span class="stat-val">{health.dmx.packets_accepted}</span></div>
            <div class="stat-row"><span>Rejected (Seq)</span><span class="stat-val">{health.dmx.packets_rejected_seq}</span></div>
            <div class="stat-row"><span>Ignored (Unpatched)</span><span class="stat-val">{health.dmx.packets_ignored_unpatched}</span></div>
            <div class="stat-row"><span>Submission Failures</span><span class="stat-val">{health.dmx.submission_failures}</span></div>
          </div>
          {#if health.dmx.configured_universes.length > 0}
            <div class="universe-list">
              <span class="uni-label">Universes:</span>
              {#each health.dmx.configured_universes as uid}
                <span class="uni-badge">{uid}</span>
              {/each}
            </div>
          {/if}
        </div>

        <!-- Sync -->
        <div class="stat-card">
          <h3>Sync</h3>
          <div class="stat-row"><span>Packet Loss</span><span class="stat-val">{health.sync.packet_loss}</span></div>
        </div>

        <!-- Startup -->
        <div class="stat-card">
          <h3>Startup</h3>
          <div class="stat-row"><span>State</span><span class="stat-val" style:color={statusColor(health.startup.state === 'Running')}>{health.startup.state}</span></div>
          <div class="stat-row"><span>Stage</span><span class="stat-val">{health.startup.stage}</span></div>
          {#if health.startup.failing_stage !== 'None'}
            <div class="stat-row"><span>Failing Stage</span><span class="stat-val err">{health.startup.failing_stage}</span></div>
            <div class="stat-row"><span>Failing Error</span><span class="stat-val err">{health.startup.failing_error}</span></div>
          {/if}
        </div>
      </div>

    {:else if activeTab === 'logs'}
      <div class="logs-panel">
        <pre class="log-viewer">{logs || 'No logs available'}</pre>
      </div>

    {:else if activeTab === 'audit'}
      <div class="audit-panel">
        {#if audit.length === 0}
          <p class="empty-msg">No audit events recorded</p>
        {:else}
          <table class="audit-table">
            <thead>
              <tr>
                <th>Time</th>
                <th>Role</th>
                <th>Action</th>
                <th>Detail</th>
              </tr>
            </thead>
            <tbody>
              {#each audit as evt}
                <tr>
                  <td class="audit-time">{auditTime(evt.timestamp_s)}</td>
                  <td><span class="role-badge">{auditRoleName(evt.role)}</span></td>
                  <td class="audit-action">{evt.action}</td>
                  <td class="audit-detail">{evt.detail || '—'}</td>
                </tr>
              {/each}
            </tbody>
          </table>
        {/if}
      </div>
    {/if}
  </div>
{/if}

<style>
  .diag-root {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    color: #f5f5f7;
  }
  .loading { color: #a1a1a6; padding: 2rem; }

  .diag-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 1rem;
  }
  .diag-header h2 { margin: 0; font-size: 1.15rem; font-weight: 700; }
  .diag-sub { margin: .2rem 0 0; font-size: .82rem; color: #a1a1a6; }

  .header-actions { display: flex; align-items: center; gap: .75rem; }
  .auto-refresh-toggle {
    display: flex; align-items: center; gap: .35rem;
    font-size: .8rem; color: #a1a1a6; cursor: pointer;
  }
  .auto-refresh-toggle input { accent-color: #ff2d55; width: 14px; height: 14px; }
  .btn-refresh {
    background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12);
    border-radius: 8px; color: #f5f5f7; font-size: .82rem; padding: .4rem .8rem;
    cursor: pointer; transition: background .2s;
  }
  .btn-refresh:hover { background: rgba(255,255,255,.14); }

  .error-bar-inline {
    background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3);
    border-radius: 10px; color: #ff453a; padding: .7rem 1rem; font-size: .85rem;
  }

  /* Tabs */
  .tab-bar {
    display: flex;
    gap: .25rem;
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 12px;
    padding: .3rem;
  }
  .tab {
    background: transparent; border: none; color: #a1a1a6;
    font-size: .85rem; font-weight: 500; padding: .5rem 1rem;
    border-radius: 8px; cursor: pointer; transition: background .15s, color .15s;
  }
  .tab:hover { background: rgba(255,255,255,.05); color: #f5f5f7; }
  .tab.active { background: rgba(255,45,85,.15); color: #ff2d55; font-weight: 600; }

  /* Confidence card */
  .confidence-card {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1.25rem;
  }
  .confidence-header {
    display: flex; align-items: center; justify-content: space-between;
    margin-bottom: 1rem;
  }
  .confidence-label { font-size: .95rem; font-weight: 600; }
  .confidence-state { font-size: .95rem; font-weight: 700; }
  .confidence-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: .75rem;
  }
  .conf-item {
    display: flex; flex-direction: column; gap: .2rem;
    background: rgba(255,255,255,.03);
    border-radius: 8px; padding: .6rem .75rem;
  }
  .conf-label { font-size: .68rem; font-weight: 600; color: #a1a1a6; text-transform: uppercase; letter-spacing: .04em; }
  .conf-val { font-size: .9rem; font-weight: 600; }

  /* Stats grid */
  .stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    gap: 1rem;
  }
  .stat-card {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1rem 1.15rem;
  }
  .stat-card.wide { grid-column: span 2; }
  .stat-card h3 {
    margin: 0 0 .75rem;
    font-size: .78rem;
    font-weight: 700;
    color: #a1a1a6;
    text-transform: uppercase;
    letter-spacing: .06em;
  }
  .stat-row {
    display: flex; justify-content: space-between; align-items: center;
    padding: .25rem 0;
    font-size: .82rem;
  }
  .stat-row span:first-child { color: #a1a1a6; }
  .stat-val { font-weight: 600; color: #f5f5f7; font-family: monospace; }
  .stat-val.err { color: #ff453a; }

  .stat-grid-2col {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0 .75rem;
  }

  .universe-list {
    display: flex; align-items: center; gap: .4rem; flex-wrap: wrap;
    margin-top: .5rem; padding-top: .5rem;
    border-top: 1px solid rgba(255,255,255,.06);
  }
  .uni-label { font-size: .72rem; color: #a1a1a6; }
  .uni-badge {
    background: rgba(0,212,255,.12); color: #00d4ff;
    font-size: .72rem; font-weight: 600; padding: .15rem .4rem;
    border-radius: 4px; font-family: monospace;
  }

  /* Logs */
  .logs-panel {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1rem;
    max-height: 70vh;
    overflow: auto;
  }
  .log-viewer {
    margin: 0;
    font-family: 'SF Mono', 'Fira Code', monospace;
    font-size: .75rem;
    line-height: 1.5;
    color: #a1a1a6;
    white-space: pre-wrap;
    word-break: break-all;
  }

  /* Audit */
  .audit-panel {
    background: rgba(29,29,31,.88);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 14px;
    padding: 1rem;
    overflow: auto;
  }
  .empty-msg { color: #a1a1a6; text-align: center; padding: 2rem; }
  .audit-table {
    width: 100%;
    border-collapse: collapse;
    font-size: .82rem;
  }
  .audit-table th {
    text-align: left;
    padding: .5rem .75rem;
    font-size: .72rem;
    font-weight: 600;
    color: #a1a1a6;
    text-transform: uppercase;
    letter-spacing: .04em;
    border-bottom: 1px solid rgba(255,255,255,.08);
  }
  .audit-table td {
    padding: .5rem .75rem;
    border-bottom: 1px solid rgba(255,255,255,.04);
  }
  .audit-time { font-family: monospace; color: #a1a1a6; white-space: nowrap; }
  .audit-action { font-weight: 600; color: #f5f5f7; }
  .audit-detail { color: #a1a1a6; max-width: 300px; overflow: hidden; text-overflow: ellipsis; }
  .role-badge {
    font-size: .68rem; font-weight: 600; padding: .15rem .4rem;
    border-radius: 4px; background: rgba(255,255,255,.06); color: #a1a1a6;
  }

  @media (max-width: 768px) {
    .stat-card.wide { grid-column: span 1; }
    .stat-grid-2col { grid-template-columns: 1fr; }
  }
</style>
