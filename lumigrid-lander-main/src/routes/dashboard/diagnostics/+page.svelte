<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getHealth, getLogs, getAuditLog } from '$lib/api/node';
  import type { HealthSnapshot, AuditEvent } from '$lib/api/node';
  import { readString, NODE_IP_KEY } from '$lib/auth/storage';

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
    nodeIp = readString(NODE_IP_KEY);
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

    <div class="tab-bar">
      <button class="tab" class:active={activeTab === 'health'} onclick={() => activeTab = 'health'}>Health</button>
      <button class="tab" class:active={activeTab === 'logs'} onclick={() => activeTab = 'logs'}>Logs</button>
      <button class="tab" class:active={activeTab === 'audit'} onclick={() => activeTab = 'audit'}>Audit</button>
    </div>

    {#if activeTab === 'health' && health}
      <div class="confidence-card">
        <div class="confidence-header">
          <span class="confidence-label">System Confidence</span>
          <span class="confidence-state" style:color={statusColor(health.startup.state === 'Running')}>
            {health.startup.state}
          </span>
        </div>
        <div class="confidence-grid">
          <div class="conf-item"><span class="conf-label">Startup</span><span class="conf-val" style:color={statusColor(health.startup.state === 'Running')}>{health.startup.state}</span></div>
          <div class="conf-item"><span class="conf-label">Config</span><span class="conf-val" style:color={statusColor(health.config.valid)}>{health.config.valid ? 'Valid' : 'Error'}</span></div>
          <div class="conf-item"><span class="conf-label">Output</span><span class="conf-val" style:color={statusColor(!health.output.fault)}>{health.output.state}</span></div>
          <div class="conf-item"><span class="conf-label">E-Stop</span><span class="conf-val" style:color={statusColor(!health.output.estop_latched)}>{health.output.estop_latched ? 'Latched' : 'Clear'}</span></div>
        </div>
      </div>

      <div class="stats-grid">
        <div class="stat-card">
          <h3>System</h3>
          <div class="stat-row"><span>Uptime</span><span class="stat-val">{formatUptime(health.system.uptime_s)}</span></div>
          <div class="stat-row"><span>Free Heap</span><span class="stat-val">{formatBytes(health.system.free_heap)}</span></div>
          <div class="stat-row"><span>Min Free Heap</span><span class="stat-val">{formatBytes(health.system.min_free_heap)}</span></div>
          <div class="stat-row"><span>FPS</span><span class="stat-val">{health.system.fps.toFixed(1)}</span></div>
          <div class="stat-row"><span>Reset Reason</span><span class="stat-val">{health.system.reset_reason}</span></div>
        </div>

        <div class="stat-card">
          <h3>Network</h3>
          <div class="stat-row"><span>WiFi RSSI</span><span class="stat-val" style:color={rssiColor(health.network.wifi_rssi)}>{health.network.wifi_rssi} dBm</span></div>
          <div class="stat-row"><span>Signal</span><span class="stat-val" style:color={rssiColor(health.network.wifi_rssi)}>{rssiLabel(health.network.wifi_rssi)}</span></div>
        </div>

        <div class="stat-card">
          <h3>Storage (NVS)</h3>
          <div class="stat-row"><span>Used</span><span class="stat-val">{health.nvs.used}</span></div>
          <div class="stat-row"><span>Free</span><span class="stat-val">{health.nvs.free}</span></div>
          <div class="stat-row"><span>Usage</span><span class="stat-val">{health.nvs.used + health.nvs.free > 0 ? ((health.nvs.used / (health.nvs.used + health.nvs.free)) * 100).toFixed(0) : 0}%</span></div>
        </div>

        <div class="stat-card">
          <h3>Configuration</h3>
          <div class="stat-row"><span>Valid</span><span class="stat-val" style:color={statusColor(health.config.valid)}>{health.config.valid ? 'Yes' : 'No'}</span></div>
          <div class="stat-row"><span>Errors</span><span class="stat-val">{health.config.error_count}</span></div>
          <div class="stat-row"><span>Persistence</span><span class="stat-val">{health.config.persistence_state}</span></div>
          <div class="stat-row"><span>Primary</span><span class="stat-val" style:color={statusColor(health.config.primary_valid)}>{health.config.primary_valid ? 'OK' : 'Invalid'}</span></div>
          <div class="stat-row"><span>Backup</span><span class="stat-val" style:color={statusColor(health.config.backup_valid)}>{health.config.backup_valid ? 'OK' : 'Invalid'}</span></div>
          {#if health.config.first_error}<div class="stat-row"><span>First Error</span><span class="stat-val err">{health.config.first_error}</span></div>{/if}
        </div>

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
            <div class="universe-list"><span class="uni-label">Universes:</span>{#each health.dmx.configured_universes as uid}<span class="uni-badge">{uid}</span>{/each}</div>
          {/if}
        </div>

        <div class="stat-card"><h3>Sync</h3><div class="stat-row"><span>Packet Loss</span><span class="stat-val">{health.sync.packet_loss}</span></div></div>

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
      <div class="logs-panel"><pre class="log-viewer">{logs || 'No logs available'}</pre></div>
    {:else if activeTab === 'audit'}
      <div class="audit-panel">
        {#if audit.length === 0}
          <p class="empty-msg">No audit events recorded</p>
        {:else}
          <table class="audit-table">
            <thead><tr><th>Time</th><th>Role</th><th>Action</th><th>Detail</th></tr></thead>
            <tbody>{#each audit as evt}<tr><td class="audit-time">{auditTime(evt.timestamp_s)}</td><td><span class="role-badge">{auditRoleName(evt.role)}</span></td><td>{evt.action}</td><td>{evt.detail}</td></tr>{/each}</tbody>
          </table>
        {/if}
      </div>
    {/if}
  </div>
{/if}

<style>
  .diag-root { display: flex; flex-direction: column; gap: 18px; }
  .diag-header { display:flex; justify-content:space-between; align-items:center; gap:16px; }
  .diag-header h2 { margin:0; }
  .diag-sub { margin:4px 0 0; opacity:.65; font-size:.9rem; }
  .header-actions { display:flex; align-items:center; gap:12px; }
  .auto-refresh-toggle { display:flex; gap:8px; align-items:center; font-size:.9rem; }
  .btn-refresh { cursor:pointer; }
  .error-bar-inline { padding:10px 12px; border-radius:8px; background:rgba(255,69,58,.12); }
  .tab-bar { display:flex; gap:4px; border-bottom:1px solid rgba(127,127,127,.2); }
  .tab { border:0; background:transparent; padding:10px 14px; cursor:pointer; opacity:.65; }
  .tab.active { opacity:1; border-bottom:2px solid currentColor; }
  .confidence-card,.stat-card,.logs-panel,.audit-panel { border:1px solid rgba(127,127,127,.2); border-radius:12px; padding:16px; background:rgba(127,127,127,.04); }
  .confidence-header { display:flex; justify-content:space-between; margin-bottom:14px; }
  .confidence-label,.conf-label { opacity:.65; font-size:.8rem; text-transform:uppercase; letter-spacing:.06em; }
  .confidence-state,.conf-val,.stat-val { font-weight:600; }
  .confidence-grid { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; }
  .conf-item { display:flex; flex-direction:column; gap:4px; }
  .stats-grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:14px; }
  .stat-card.wide { grid-column:1/-1; }
  .stat-card h3 { margin:0 0 12px; }
  .stat-row { display:flex; justify-content:space-between; gap:16px; padding:7px 0; }
  .stat-val { text-align:right; }
  .stat-grid-2col { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); column-gap:24px; }
  .err { color:#ff453a; }
  .universe-list { display:flex; gap:6px; align-items:center; flex-wrap:wrap; margin-top:10px; }
  .uni-label { opacity:.65; }
  .uni-badge,.role-badge { border:1px solid rgba(127,127,127,.25); border-radius:999px; padding:3px 8px; font-size:.8rem; }
  .log-viewer { margin:0; max-height:60vh; overflow:auto; white-space:pre-wrap; font:12px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace; }
  .audit-table { width:100%; border-collapse:collapse; }
  .audit-table th,.audit-table td { text-align:left; padding:9px 8px; border-bottom:1px solid rgba(127,127,127,.15); }
  .empty-msg,.loading { opacity:.65; }
  @media (max-width:800px) { .diag-header { align-items:flex-start; flex-direction:column; } .confidence-grid,.stats-grid,.stat-grid-2col { grid-template-columns:1fr; } .stat-card.wide { grid-column:auto; } .audit-panel { overflow:auto; } }
</style>
