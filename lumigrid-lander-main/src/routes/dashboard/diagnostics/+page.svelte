<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getHealth, getLogs, getAuditLog } from '$lib/api/node';
  import type { HealthSnapshot, AuditEvent } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let loading = $state(true);
  let error = $state('');
  let health = $state<HealthSnapshot | null>(null);
  let rawLogs = $state('');
  let audit = $state<AuditEvent[]>([]);
  let autoRefresh = $state(false);
  let refreshTimer: ReturnType<typeof setInterval> | null = null;
  let activeTab = $state<'health' | 'events' | 'audit'>('health');
  let logSearch = $state('');
  let logLevelFilter = $state<'all' | 'error' | 'warn' | 'info'>('all');

  interface ParsedLogLine {
    id: number;
    raw: string;
    level: 'error' | 'warn' | 'info' | 'debug';
    tag: string;
    text: string;
  }

  const parsedLogs = $derived.by<ParsedLogLine[]>(() => {
    if (!rawLogs) return [];
    return rawLogs
      .split('\n')
      .map(line => line.trim())
      .filter(Boolean)
      .map((line, idx) => {
        let level: 'error' | 'warn' | 'info' | 'debug' = 'info';
        const lower = line.toLowerCase();
        if (lower.includes('err') || lower.includes('fail') || lower.includes('[e]')) {
          level = 'error';
        } else if (lower.includes('warn') || lower.includes('[w]')) {
          level = 'warn';
        } else if (lower.includes('dbg') || lower.includes('[d]')) {
          level = 'debug';
        }

        let tag = 'SYS';
        const tagMatch = line.match(/\[([A-Za-z0-9_\-]+)\]/);
        if (tagMatch) tag = tagMatch[1];

        return {
          id: idx,
          raw: line,
          level,
          tag,
          text: line,
        };
      });
  });

  const filteredLogs = $derived.by<ParsedLogLine[]>(() => {
    const list = parsedLogs;
    const query = logSearch.toLowerCase().trim();
    return list.filter(l => {
      if (logLevelFilter !== 'all') {
        if (logLevelFilter === 'error' && l.level !== 'error') return false;
        if (logLevelFilter === 'warn' && l.level !== 'warn' && l.level !== 'error') return false;
        if (logLevelFilter === 'info' && l.level === 'debug') return false;
      }
      if (query && !l.raw.toLowerCase().includes(query)) return false;
      return true;
    });
  });

  let loadGen = 0;

  async function loadData(targetIp = nodeIp, gen = loadGen) {
    if (!targetIp) {
      loading = false;
      return;
    }
    error = '';
    try {
      const [h, l, a] = await Promise.all([
        getHealth(targetIp).catch(() => null),
        getLogs(targetIp).catch(() => ''),
        getAuditLog(targetIp).catch(() => [] as AuditEvent[]),
      ]);
      if (gen !== loadGen) return;
      health = h;
      rawLogs = l;
      audit = a;
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) {
        loading = false;
      }
    }
  }

  function toggleAutoRefresh() {
    autoRefresh = !autoRefresh;
    if (autoRefresh) {
      refreshTimer = setInterval(loadData, 3000);
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
    if (rssi >= -55) return 'Excellent';
    if (rssi >= -67) return 'Good';
    if (rssi >= -78) return 'Fair';
    return 'Poor';
  }

  function rssiColor(rssi: number): string {
    if (rssi >= -55) return '#30d158';
    if (rssi >= -67) return '#30d158';
    if (rssi >= -78) return '#ff9f0a';
    return '#ff453a';
  }

  function statusColor(ok: boolean): string {
    return ok ? '#30d158' : '#ff453a';
  }

  function controlSourceName(cs: number): string {
    const names = ['None', 'Scheduler', 'DMX', 'Manual', 'Preset', 'Sequence'];
    return names[cs] ?? `Source(${cs})`;
  }

  function auditRoleName(role: number): string {
    const names = ['Guest', 'Operator', 'Installer', 'Admin'];
    return names[role] ?? `Role(${role})`;
  }

  function auditTime(ts: number): string {
    if (!ts) return '—';
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString();
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        if (nodeIp) {
          health = null;
          rawLogs = '';
          audit = [];
          loading = true;
          loadData(nodeIp, loadGen);
        } else {
          health = null;
          rawLogs = '';
          audit = [];
          loading = false;
          error = '';
        }
      }
    });
    return unsub;
  });

  onDestroy(() => {
    if (refreshTimer) clearInterval(refreshTimer);
  });
</script>

{#if !nodeIp}
  <div class="diag-empty">
    <p>No node selected. Connect to a node in the header to view diagnostics.</p>
  </div>
{:else if loading}
  <div class="diag-loading">
    <div class="spinner"></div>
    <p>Loading telemetry and system health from {nodeIp}...</p>
  </div>
{:else}
  <div class="diag-root">
    <!-- Header -->
    <header class="diag-header">
      <div class="header-left">
        <h2>Diagnostics & Telemetry</h2>
        <p class="diag-sub">
          Node: <span class="ip-tag">{nodeIp}</span> · Health Schema: v{health?.schema_version ?? '1'}
        </p>
      </div>

      <div class="header-actions">
        <label class="auto-refresh-toggle">
          <input type="checkbox" checked={autoRefresh} onchange={toggleAutoRefresh} />
          <span>Auto-refresh (3s)</span>
        </label>
        <button class="btn-refresh" onclick={() => loadData()} type="button">
          Refresh
        </button>
      </div>
    </header>

    {#if error}
      <div class="error-bar">{error}</div>
    {/if}

    <!-- Tab Bar: HEALTH, EVENTS, AUDIT -->
    <div class="tab-bar">
      <button
        class="tab"
        class:active={activeTab === 'health'}
        onclick={() => (activeTab = 'health')}
        type="button"
      >
        <span>Hardware Health</span>
        {#if health && !health.output.fault && health.config.valid}
          <span class="tab-badge ok">OK</span>
        {:else if health}
          <span class="tab-badge warn">ALERT</span>
        {/if}
      </button>

      <button
        class="tab"
        class:active={activeTab === 'events'}
        onclick={() => (activeTab = 'events')}
        type="button"
      >
        <span>Chronological Events</span>
        {#if parsedLogs.length > 0}
          <span class="tab-badge count">{parsedLogs.length}</span>
        {/if}
      </button>

      <button
        class="tab"
        class:active={activeTab === 'audit'}
        onclick={() => (activeTab = 'audit')}
        type="button"
      >
        <span>Security Audit</span>
        {#if audit.length > 0}
          <span class="tab-badge count">{audit.length}</span>
        {/if}
      </button>
    </div>

    <!-- TAB 1: HEALTH -->
    {#if activeTab === 'health' && health}
      <!-- Confidence Overview Banner -->
      <div class="confidence-card">
        <div class="confidence-header">
          <div class="conf-title-group">
            <span class="confidence-label">Core Runtime Integrity</span>
            <span class="confidence-state" style:color={statusColor(health.startup.state === 'Running')}>
              ● {health.startup.state}
            </span>
          </div>
          <span class="conf-uptime">Uptime: {formatUptime(health.system.uptime_s)}</span>
        </div>

        <div class="confidence-grid">
          <div class="conf-item">
            <span class="conf-label">Startup State</span>
            <span class="conf-val" style:color={statusColor(health.startup.state === 'Running')}>
              {health.startup.state}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Config Integrity</span>
            <span class="conf-val" style:color={statusColor(health.config.valid)}>
              {health.config.valid ? 'Valid & Synced' : 'Config Error'}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Output Stage</span>
            <span class="conf-val" style:color={statusColor(!health.output.fault)}>
              {health.output.fault ? 'FAULT' : health.output.state}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Emergency Stop</span>
            <span class="conf-val" style:color={statusColor(!health.output.estop_latched)}>
              {health.output.estop_latched ? 'LATCHED' : 'Disarmed / Normal'}
            </span>
          </div>
        </div>
      </div>

      <!-- Health Telemetry Grid -->
      <div class="stats-grid">
        <!-- System & Compute -->
        <div class="stat-card">
          <div class="card-head">
            <h3>System & Memory</h3>
            <span class="card-fps">{health.system.fps.toFixed(1)} FPS</span>
          </div>
          <div class="stat-row">
            <span>Uptime</span>
            <span class="stat-val">{formatUptime(health.system.uptime_s)}</span>
          </div>
          <div class="stat-row">
            <span>Free Heap</span>
            <span class="stat-val">{formatBytes(health.system.free_heap)}</span>
          </div>
          <div class="stat-row">
            <span>Min Free Heap</span>
            <span class="stat-val">{formatBytes(health.system.min_free_heap)}</span>
          </div>
          <div class="stat-row">
            <span>Reset Reason</span>
            <span class="stat-val">{health.system.reset_reason}</span>
          </div>
        </div>

        <!-- Network & RF -->
        <div class="stat-card">
          <div class="card-head">
            <h3>Network & RF</h3>
            <span class="signal-tag" style:color={rssiColor(health.network.wifi_rssi)}>
              {rssiLabel(health.network.wifi_rssi)}
            </span>
          </div>
          <div class="stat-row">
            <span>Wi-Fi RSSI</span>
            <span class="stat-val" style:color={rssiColor(health.network.wifi_rssi)}>
              {health.network.wifi_rssi} dBm
            </span>
          </div>
          <div class="stat-row">
            <span>Sync Packet Loss</span>
            <span class="stat-val">{health.sync.packet_loss}</span>
          </div>
          <div class="stat-row">
            <span>Active IP</span>
            <span class="stat-val mono">{nodeIp}</span>
          </div>
        </div>

        <!-- Storage / NVS -->
        <div class="stat-card">
          <div class="card-head">
            <h3>Non-Volatile Storage (NVS)</h3>
            <span class="usage-tag">
              {health.nvs.used + health.nvs.free > 0
                ? ((health.nvs.used / (health.nvs.used + health.nvs.free)) * 100).toFixed(0)
                : 0}% used
            </span>
          </div>
          <div class="stat-row">
            <span>Entries Used</span>
            <span class="stat-val">{health.nvs.used}</span>
          </div>
          <div class="stat-row">
            <span>Entries Free</span>
            <span class="stat-val">{health.nvs.free}</span>
          </div>
          <div class="stat-row">
            <span>Persistence State</span>
            <span class="stat-val">{health.config.persistence_state}</span>
          </div>
        </div>

        <!-- Configuration Store -->
        <div class="stat-card">
          <div class="card-head">
            <h3>Configuration Store</h3>
            <span class="status-tag" style:color={statusColor(health.config.valid)}>
              {health.config.valid ? 'Valid' : 'Invalid'}
            </span>
          </div>
          <div class="stat-row">
            <span>Primary Slot</span>
            <span class="stat-val" style:color={statusColor(health.config.primary_valid)}>
              {health.config.primary_valid ? 'OK' : 'Corrupt'}
            </span>
          </div>
          <div class="stat-row">
            <span>Backup Slot</span>
            <span class="stat-val" style:color={statusColor(health.config.backup_valid)}>
              {health.config.backup_valid ? 'OK' : 'Corrupt'}
            </span>
          </div>
          <div class="stat-row">
            <span>Config Error Count</span>
            <span class="stat-val" class:err={health.config.error_count > 0}>
              {health.config.error_count}
            </span>
          </div>
          {#if health.config.first_error}
            <div class="stat-row">
              <span>Error Detail</span>
              <span class="stat-val err">{health.config.first_error}</span>
            </div>
          {/if}
        </div>

        <!-- Output Stage (Wide) -->
        <div class="stat-card wide">
          <div class="card-head">
            <h3>Output Stage & Power</h3>
            <span class="pwr-tag">{formatPower(health.output.current_power_mw)}</span>
          </div>
          <div class="stat-grid-2col">
            <div class="stat-row">
              <span>Output State</span>
              <span class="stat-val">{health.output.state}</span>
            </div>
            <div class="stat-row">
              <span>Fault Detected</span>
              <span class="stat-val" style:color={statusColor(!health.output.fault)}>
                {health.output.fault ? 'YES' : 'No'}
              </span>
            </div>
            <div class="stat-row">
              <span>Control Source</span>
              <span class="stat-val">{controlSourceName(health.output.control_source)}</span>
            </div>
            <div class="stat-row">
              <span>Active Preset</span>
              <span class="stat-val mono">P{health.output.active_preset}</span>
            </div>
            <div class="stat-row">
              <span>Live Render Objects</span>
              <span class="stat-val">{health.output.live_objects}</span>
            </div>
            <div class="stat-row">
              <span>Estimated Power</span>
              <span class="stat-val">{formatPower(health.output.current_power_mw)}</span>
            </div>
            <div class="stat-row">
              <span>Throttling Events</span>
              <span class="stat-val">{health.output.throttling_events}</span>
            </div>
            <div class="stat-row">
              <span>Watchdog Fallback</span>
              <span class="stat-val" style:color={statusColor(!health.output.watchdog_fallback)}>
                {health.output.watchdog_fallback ? 'YES' : 'No'}
              </span>
            </div>
          </div>
        </div>

        <!-- DMX & Streaming Protocols (Wide) -->
        <div class="stat-card wide">
          <div class="card-head">
            <h3>DMX & sACN Streaming</h3>
            <span class="dmx-tag">{health.dmx.universes_active} Universes Active</span>
          </div>
          <div class="stat-grid-2col">
            <div class="stat-row">
              <span>Active Sources</span>
              <span class="stat-val">{health.dmx.sources_active}</span>
            </div>
            <div class="stat-row">
              <span>Packets Accepted</span>
              <span class="stat-val">{health.dmx.packets_accepted}</span>
            </div>
            <div class="stat-row">
              <span>Sequence Drops</span>
              <span class="stat-val">{health.dmx.packets_rejected_seq}</span>
            </div>
            <div class="stat-row">
              <span>Unpatched Packets</span>
              <span class="stat-val">{health.dmx.packets_ignored_unpatched}</span>
            </div>
            <div class="stat-row">
              <span>Submission Failures</span>
              <span class="stat-val">{health.dmx.submission_failures}</span>
            </div>
            <div class="stat-row">
              <span>Dropped Sources</span>
              <span class="stat-val">{health.dmx.sources_dropped}</span>
            </div>
          </div>
          {#if health.dmx.configured_universes.length > 0}
            <div class="universe-list">
              <span class="uni-label">Configured Universes:</span>
              {#each health.dmx.configured_universes as uid}
                <span class="uni-badge">U{uid}</span>
              {/each}
            </div>
          {/if}
        </div>
      </div>
    {:else if activeTab === 'events'}
      <!-- TAB 2: EVENTS -->
      <div class="events-container">
        <!-- Event Filters -->
        <div class="events-toolbar">
          <div class="search-wrap">
            <input
              type="text"
              placeholder="Search event messages..."
              bind:value={logSearch}
              class="search-input"
            />
          </div>

          <div class="level-filters">
            <button
              class="lvl-btn"
              class:active={logLevelFilter === 'all'}
              onclick={() => (logLevelFilter = 'all')}
              type="button"
            >
              All
            </button>
            <button
              class="lvl-btn info"
              class:active={logLevelFilter === 'info'}
              onclick={() => (logLevelFilter = 'info')}
              type="button"
            >
              Info
            </button>
            <button
              class="lvl-btn warn"
              class:active={logLevelFilter === 'warn'}
              onclick={() => (logLevelFilter = 'warn')}
              type="button"
            >
              Warnings
            </button>
            <button
              class="lvl-btn error"
              class:active={logLevelFilter === 'error'}
              onclick={() => (logLevelFilter = 'error')}
              type="button"
            >
              Errors
            </button>
          </div>
        </div>

        {#if filteredLogs.length === 0}
          <div class="empty-panel">
            <p>No log events matched your filter criteria.</p>
          </div>
        {:else}
          <div class="log-stream">
            {#each filteredLogs as item}
              <div class="log-line" class:line-err={item.level === 'error'} class:line-warn={item.level === 'warn'}>
                <span class="line-level {item.level}">{item.level.toUpperCase()}</span>
                <span class="line-tag">[{item.tag}]</span>
                <span class="line-text">{item.text}</span>
              </div>
            {/each}
          </div>
        {/if}
      </div>
    {:else if activeTab === 'audit'}
      <!-- TAB 3: AUDIT -->
      <div class="audit-container">
        <div class="audit-intro">
          <h3>Security & Configuration Audit Log</h3>
          <p>Chronological record of authenticated operator and administrative actions executed on {nodeIp}.</p>
        </div>

        {#if audit.length === 0}
          <div class="empty-panel">
            <p>No audit events recorded by firmware.</p>
          </div>
        {:else}
          <div class="audit-table-wrap">
            <table class="audit-table">
              <thead>
                <tr>
                  <th>Timestamp</th>
                  <th>Role</th>
                  <th>Action</th>
                  <th>Detail</th>
                </tr>
              </thead>
              <tbody>
                {#each audit as evt}
                  <tr>
                    <td class="audit-time mono">{auditTime(evt.timestamp_s)}</td>
                    <td>
                      <span class="role-badge role-{auditRoleName(evt.role).toLowerCase()}">
                        {auditRoleName(evt.role)}
                      </span>
                    </td>
                    <td class="audit-action mono">{evt.action}</td>
                    <td class="audit-detail">{evt.detail}</td>
                  </tr>
                {/each}
              </tbody>
            </table>
          </div>
        {/if}
      </div>
    {/if}
  </div>
{/if}

<style>
  .diag-root {
    width: 100%;
    max-width: 1240px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    color: var(--dash-text);
  }

  .diag-loading, .diag-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    min-height: 300px;
    color: #a1a1a6;
  }
  .spinner {
    width: 32px;
    height: 32px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  /* Header */
  .diag-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
    background: var(--dash-surface);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    padding: 0.85rem 1.25rem;
  }
  .diag-header h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 600;
  }
  .diag-sub {
    margin: 0.25rem 0 0;
    font-size: 0.8rem;
    color: #86868b;
  }
  .ip-tag {
    color: var(--dash-led);
    font-family: monospace;
  }

  .header-actions {
    display: flex;
    align-items: center;
    gap: 1rem;
  }
  .auto-refresh-toggle {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.8rem;
    color: #86868b;
    cursor: pointer;
  }
  .auto-refresh-toggle input {
    accent-color: var(--dash-accent);
  }
  .btn-refresh {
    background: rgba(255, 255, 255, 0.06);
    color: #f5f5f7;
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    padding: 0.4rem 0.85rem;
    font-size: 0.8rem;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .btn-refresh:hover {
    background: rgba(255, 255, 255, 0.1);
  }

  /* Tab Bar */
  .tab-bar {
    display: flex;
    gap: 0.5rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    padding-bottom: 0.25rem;
  }
  .tab {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    background: transparent;
    border: none;
    padding: 0.5rem 0.9rem;
    color: #86868b;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    border-radius: 8px;
    transition: all 0.15s ease;
  }
  .tab:hover {
    color: #f5f5f7;
    background: rgba(255, 255, 255, 0.03);
  }
  .tab.active {
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.1);
  }
  .tab-badge {
    font-size: 0.65rem;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    font-weight: 700;
  }
  .tab-badge.ok {
    background: rgba(48, 209, 88, 0.2);
    color: #30d158;
  }
  .tab-badge.warn {
    background: rgba(255, 159, 10, 0.2);
    color: #ff9f0a;
  }
  .tab-badge.count {
    background: rgba(255, 255, 255, 0.1);
    color: #f5f5f7;
  }

  /* Confidence Card */
  .confidence-card {
    background: var(--dash-surface);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1rem 1.25rem;
  }
  .confidence-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 0.85rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    padding-bottom: 0.5rem;
  }
  .conf-title-group {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }
  .confidence-label {
    font-size: 0.75rem;
    font-weight: 700;
    color: #86868b;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .confidence-state {
    font-size: 0.85rem;
    font-weight: 700;
  }
  .conf-uptime {
    font-size: 0.8rem;
    color: #86868b;
  }
  .confidence-grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 1rem;
  }
  .conf-item {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .conf-label {
    font-size: 0.72rem;
    color: #86868b;
  }
  .conf-val {
    font-size: 0.85rem;
    font-weight: 600;
  }

  /* Stats Grid */
  .stats-grid {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 1rem;
  }
  .stat-card {
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 12px;
    padding: 1rem 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .stat-card.wide {
    grid-column: 1 / -1;
  }
  .card-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 0.5rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
    padding-bottom: 0.4rem;
  }
  .card-head h3 {
    margin: 0;
    font-size: 0.9rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .card-fps, .signal-tag, .usage-tag, .status-tag, .pwr-tag, .dmx-tag {
    font-size: 0.72rem;
    font-family: monospace;
    font-weight: 700;
  }

  .stat-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.8rem;
    color: #a1a1a6;
    padding: 0.2rem 0;
  }
  .stat-val {
    color: #f5f5f7;
    font-weight: 600;
  }
  .stat-val.mono {
    font-family: monospace;
  }
  .stat-grid-2col {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    column-gap: 2rem;
  }
  .err {
    color: #ff453a !important;
  }

  .universe-list {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin-top: 0.6rem;
    flex-wrap: wrap;
  }
  .uni-label {
    font-size: 0.72rem;
    color: #86868b;
  }
  .uni-badge {
    background: rgba(var(--dash-accent-rgb), 0.15);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    color: var(--dash-led);
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    font-size: 0.72rem;
    font-family: monospace;
  }

  /* Events View */
  .events-container {
    background: var(--dash-surface);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .events-toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .search-input {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: 0.82rem;
    padding: 0.4rem 0.75rem;
    width: 260px;
  }
  .search-input:focus {
    outline: none;
    border-color: var(--dash-accent);
  }
  .level-filters {
    display: flex;
    gap: 0.35rem;
  }
  .lvl-btn {
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: #86868b;
    font-size: 0.75rem;
    padding: 0.3rem 0.6rem;
    cursor: pointer;
  }
  .lvl-btn.active {
    background: rgba(255, 255, 255, 0.12);
    color: #f5f5f7;
  }
  .lvl-btn.info.active {
    color: var(--dash-led);
    border-color: color-mix(in oklab, var(--dash-led) 40%, transparent);
  }
  .lvl-btn.warn.active {
    color: #ff9f0a;
    border-color: rgba(255, 159, 10, 0.4);
  }
  .lvl-btn.error.active {
    color: #ff453a;
    border-color: rgba(255, 69, 58, 0.4);
  }

  .log-stream {
    max-height: 60vh;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 2px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 0.75rem;
    line-height: 1.4;
  }
  .log-line {
    display: flex;
    gap: 0.6rem;
    padding: 0.3rem 0.5rem;
    border-radius: 4px;
    background: rgba(255, 255, 255, 0.015);
  }
  .log-line.line-warn {
    background: rgba(255, 159, 10, 0.08);
  }
  .log-line.line-err {
    background: rgba(255, 69, 58, 0.1);
  }
  .line-level {
    font-weight: 700;
    min-width: 48px;
  }
  .line-level.error { color: #ff453a; }
  .line-level.warn { color: #ff9f0a; }
  .line-level.info { color: var(--dash-led); }
  .line-level.debug { color: #86868b; }
  .line-tag {
    color: #bf5af2;
    min-width: 70px;
  }
  .line-text {
    color: #ddd;
    word-break: break-all;
  }

  /* Audit View */
  .audit-container {
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 14px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .audit-intro h3 {
    margin: 0;
    font-size: 1rem;
  }
  .audit-intro p {
    margin: 0.25rem 0 0;
    font-size: 0.8rem;
    color: #86868b;
  }
  .audit-table-wrap {
    overflow-x: auto;
  }
  .audit-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.8rem;
  }
  .audit-table th {
    text-align: left;
    padding: 0.6rem 0.8rem;
    color: #86868b;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }
  .audit-table td {
    padding: 0.6rem 0.8rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  }
  .role-badge {
    padding: 0.2rem 0.5rem;
    border-radius: 6px;
    font-size: 0.72rem;
    font-weight: 700;
  }
  .role-admin { background: rgba(255, 69, 58, 0.15); color: #ff453a; }
  .role-installer { background: rgba(255, 159, 10, 0.15); color: #ff9f0a; }
  .role-operator { background: rgba(48, 209, 88, 0.15); color: #30d158; }
  .role-guest { background: rgba(255, 255, 255, 0.08); color: #86868b; }

  .empty-panel {
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: 150px;
    color: #86868b;
    font-size: 0.85rem;
  }

  .error-bar {
    background: rgba(255, 69, 58, 0.2);
    border: 1px solid rgba(255, 69, 58, 0.4);
    color: #ff453a;
    padding: 0.6rem 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }

  @media (max-width: 800px) {
    .confidence-grid { grid-template-columns: repeat(2, 1fr); }
    .stats-grid { grid-template-columns: 1fr; }
    .stat-grid-2col { grid-template-columns: 1fr; }
  }

  @media (max-width: 760px) {
    .diag-header { align-items: flex-start; }
    .header-actions { width: 100%; justify-content: space-between; }
    .tab-bar { overflow-x: auto; padding-bottom: .35rem; }
    .tab { white-space: nowrap; }
    .confidence-grid, .stats-grid, .stat-grid-2col { grid-template-columns: 1fr; }
    .search-input { width: 100%; }
    .events-toolbar { align-items: stretch; }
    .level-filters { overflow-x: auto; }
  }

</style>
