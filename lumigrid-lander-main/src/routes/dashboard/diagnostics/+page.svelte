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

    <div class="tabs">
      <button class:active={activeTab === 'health'} onclick={() => activeTab = 'health'}>Health</button>
      <button class:active={activeTab === 'logs'} onclick={() => activeTab = 'logs'}>Logs</button>
      <button class:active={activeTab === 'audit'} onclick={() => activeTab = 'audit'}>Audit</button>
    </div>

    {#if activeTab === 'health'}
      <section class="health-grid">
        <div class="health-card">
          <span class="card-label">System</span>
          <div class="health-row"><span>Uptime</span><strong>{formatUptime(health?.system?.uptime ?? 0)}</strong></div>
          <div class="health-row"><span>Free heap</span><strong>{formatBytes(health?.system?.free_heap ?? 0)}</strong></div>
          <div class="health-row"><span>Min heap</span><strong>{formatBytes(health?.system?.minimum_free_heap ?? 0)}</strong></div>
        </div>
        <div class="health-card">
          <span class="card-label">Network</span>
          <div class="health-row"><span>RSSI</span><strong style:color={rssiColor(health?.network?.rssi ?? -100)}>{health?.network?.rssi ?? '—'} dBm · {rssiLabel(health?.network?.rssi ?? -100)}</strong></div>
          <div class="health-row"><span>IP</span><strong>{health?.network?.ip ?? nodeIp}</strong></div>
          <div class="health-row"><span>Connected</span><strong>{health?.network?.connected ? 'Yes' : 'No'}</strong></div>
        </div>
        <div class="health-card">
          <span class="card-label">Output</span>
          <div class="health-row"><span>Enabled</span><strong style:color={statusColor(health?.output?.enabled ?? false)}>{health?.output?.enabled ? 'Yes' : 'No'}</strong></div>
          <div class="health-row"><span>Control source</span><strong>{controlSourceName(health?.output?.control_source ?? 0)}</strong></div>
          <div class="health-row"><span>Power</span><strong>{formatPower(health?.output?.power_mw ?? 0)}</strong></div>
        </div>
        <div class="health-card">
          <span class="card-label">Persistence</span>
          <div class="health-row"><span>NVS</span><strong>{health?.nvs?.ok ? 'OK' : 'Error'}</strong></div>
          <div class="health-row"><span>Config</span><strong>{health?.config?.valid ? 'Valid' : 'Invalid'}</strong></div>
          <div class="health-row"><span>Startup</span><strong>{health?.startup?.ready ? 'Ready' : 'Not ready'}</strong></div>
        </div>
      </section>
    {:else if activeTab === 'logs'}
      <section class="panel">
        <pre class="log-view">{logs || 'No log data returned.'}</pre>
      </section>
    {:else}
      <section class="panel">
        {#if audit.length}
          <div class="audit-list">
            {#each audit as event}
              <div class="audit-row">
                <span class="audit-time">{auditTime(event.ts)}</span>
                <span class="audit-role">{auditRoleName(event.role)}</span>
                <span class="audit-action">{event.action}</span>
                <span class="audit-result">{event.result ?? ''}</span>
              </div>
            {/each}
          </div>
        {:else}
          <p class="empty">No audit events returned.</p>
        {/if}
      </section>
    {/if}
  </div>
{/if}

<style>
  .diag-root { display: flex; flex-direction: column; gap: 1rem; color: #f5f5f7; }
  .loading { color: #a1a1a6; padding: 2rem; }
  .diag-header { display: flex; justify-content: space-between; align-items: flex-start; gap: 1rem; flex-wrap: wrap; }
  .diag-header h2 { margin: 0; font-size: 1.15rem; }
  .diag-sub { margin: .2rem 0 0; color: #a1a1a6; font-size: .82rem; }
  .header-actions { display: flex; align-items: center; gap: .7rem; }
  .auto-refresh-toggle { display: flex; align-items: center; gap: .4rem; color: #a1a1a6; font-size: .8rem; cursor: pointer; }
  .auto-refresh-toggle input { accent-color: #ff2d55; }
  .btn-refresh { background: rgba(255,255,255,.07); border: 1px solid rgba(255,255,255,.12); border-radius: 9px; color: #f5f5f7; padding: .45rem .8rem; cursor: pointer; }
  .btn-refresh:hover { background: rgba(255,255,255,.12); }
  .error-bar-inline { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.25); color: #ff453a; border-radius: 10px; padding: .7rem .8rem; font-size: .82rem; }
  .tabs { display: flex; gap: .35rem; border-bottom: 1px solid rgba(255,255,255,.08); padding-bottom: .35rem; }
  .tabs button { background: transparent; border: none; color: #8e8e93; padding: .45rem .7rem; border-radius: 7px; cursor: pointer; font-size: .82rem; }
  .tabs button.active { background: rgba(255,45,85,.12); color: #f5f5f7; }
  .health-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: .9rem; }
  .health-card { background: rgba(29,29,31,.78); border: 1px solid rgba(255,255,255,.08); border-radius: 14px; padding: 1rem; }
  .card-label { display: block; color: #8e8e93; text-transform: uppercase; letter-spacing: .05em; font-size: .68rem; font-weight: 700; margin-bottom: .65rem; }
  .health-row { display: flex; justify-content: space-between; gap: .75rem; padding: .35rem 0; font-size: .78rem; color: #a1a1a6; border-top: 1px solid rgba(255,255,255,.04); }
  .health-row:first-of-type { border-top: 0; }
  .health-row strong { color: #f5f5f7; text-align: right; }
  .panel { background: rgba(29,29,31,.78); border: 1px solid rgba(255,255,255,.08); border-radius: 14px; overflow: hidden; }
  .log-view { margin: 0; padding: 1rem; color: #d1d1d6; font: .72rem/1.5 ui-monospace, SFMono-Regular, Menlo, monospace; white-space: pre-wrap; max-height: 65vh; overflow: auto; }
  .audit-list { display: flex; flex-direction: column; }
  .audit-row { display: grid; grid-template-columns: 90px 90px minmax(120px,1fr) minmax(100px,1fr); gap: .75rem; padding: .65rem .9rem; border-bottom: 1px solid rgba(255,255,255,.05); font-size: .75rem; align-items: center; }
  .audit-row:last-child { border-bottom: none; }
  .audit-time, .audit-role { color: #8e8e93; font-family: monospace; }
  .audit-action { color: #f5f5f7; font-weight: 600; }
  .audit-result { color: #a1a1a6; text-align: right; }
  .empty { color: #8e8e93; padding: 2rem; text-align: center; }
  @media (max-width: 700px) { .audit-row { grid-template-columns: 1fr 1fr; } .audit-result { text-align: left; } }
</style>
