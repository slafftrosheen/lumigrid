<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getStorageDiagnostics, type StorageDiagnostics } from '$lib/api/firmware';
  import { getHealth, getLogs, getAuditLog } from '$lib/api/node';
  import type { HealthSnapshot, AuditEvent } from '$lib/api/node';
  import { evaluateActionableIssues, getOutputDiagnostics, getMemoryDiagnostics } from '$lib/api/diagnostics';
  import type { ActionableIssue, OutputDiagnosticsResponse, MemoryDiagnosticsResponse } from '$lib/api/diagnostics';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let loading = $state(true);
  let error = $state('');
  let health = $state<HealthSnapshot | null>(null);
  let outputDiag = $state<OutputDiagnosticsResponse | null>(null);
  let storageDiag = $state<StorageDiagnostics | null>(null);
  let memoryDiag = $state<MemoryDiagnosticsResponse | null>(null);
  let showAdvancedMemory = $state(false);
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
      const [h, l, a, od, md, sd] = await Promise.all([
        getHealth(targetIp).catch(() => null),
        getLogs(targetIp).catch(() => ''),
        getAuditLog(targetIp).catch(() => [] as AuditEvent[]),
        getOutputDiagnostics(targetIp).catch(() => null),
        getMemoryDiagnostics(targetIp).catch(() => null),
        getStorageDiagnostics(targetIp).catch(() => null),
      ]);
      if (gen !== loadGen) return;
      health = h;
      rawLogs = l;
      audit = a;
      outputDiag = od;
      memoryDiag = md;
      storageDiag = sd;
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

  // Authoritative live indicators from nodeStore snapshot
  const liveOutput = $derived($nodeStore.snapshot?.output);
  const liveNetwork = $derived($nodeStore.snapshot?.network);
  const liveSync = $derived($nodeStore.snapshot?.sync);
  const liveProtocols = $derived($nodeStore.snapshot?.protocols);
  const liveRuntime = $derived($nodeStore.snapshot?.runtime);
  const liveJob = $derived($nodeStore.snapshot?.job);

  const displayOutputState = $derived(liveOutput?.state ?? health?.output.state ?? 'Normal');
  const displayFault = $derived(liveOutput?.fault ?? health?.output.fault ?? false);
  const displayPowerMw = $derived(liveOutput?.powerMw ?? health?.output.current_power_mw ?? 0);
  const displayRssi = $derived(liveNetwork?.rssi ?? health?.network.wifi_rssi ?? 0);
  const displayEstop = $derived(liveOutput?.estopActive ?? health?.output.estop_latched ?? false);
  const actionableIssues = $derived<ActionableIssue[]>(health ? evaluateActionableIssues(health) : []);

  // Output Engine Timing telemetry (LG-038)
  const targetFps = 40.0;
  const committedFps = $derived(liveOutput?.frame?.committedFps ?? health?.system.fps ?? 40.0);
  const frameDurationMs = $derived((((liveOutput?.commitUs ?? outputDiag?.frame?.lastUs ?? 0) / 1000)).toFixed(2));
  const rmtDurationMs = $derived((((liveOutput?.addressableUs ?? outputDiag?.addressable?.lastCommitUs ?? 0) / 1000)).toFixed(2));
  const pwmDurationMs = $derived((((liveOutput?.pwmUs ?? 0) / 1000)).toFixed(2));
  const headroomMs = $derived.by(() => {
    const commitUs = liveOutput?.commitUs ?? outputDiag?.frame?.lastUs ?? 0;
    const targetUs = outputDiag?.frame?.targetPeriodUs ?? 25000;
    const rem = Math.max(0, targetUs - commitUs);
    return (rem / 1000).toFixed(2);
  });
  const deadlineMisses = $derived(liveOutput?.frame?.deadlineMisses ?? outputDiag?.frame?.deadlineMisses ?? 0);
  const activeLanes = $derived(outputDiag?.addressable?.activeLanes ?? 8);

  function statusColor(ok: boolean): string {
    return ok ? '#30d158' : '#ff453a';
  }

  // Runtime Memory card data (LG-038.3 §58): primary metric is free8 (§59);
  // generic internal heap is never presented as "free RAM".
  const memoryFree8 = $derived(memoryDiag?.free8 ?? health?.memory?.free8 ?? health?.system.free_heap ?? 0);
  const memoryLargest8 = $derived(memoryDiag?.largest8 ?? health?.memory?.largest8 ?? 0);
  const memoryMinFree = $derived(memoryDiag?.minimumFree ?? health?.memory?.minimum_free ?? health?.system.min_free_heap ?? 0);
  const memoryAllocationFailures = $derived(
    memoryDiag?.allocationFailures
      ? memoryDiag.allocationFailures.apiAllocationFailures +
          memoryDiag.allocationFailures.previewAllocationFailures +
          memoryDiag.allocationFailures.rmtAllocationFailures +
          memoryDiag.allocationFailures.factoryTaskStartFailures +
          memoryDiag.allocationFailures.networkTaskStartFailures
      : 0
  );
  const memoryHealthName = $derived(memoryDiag?.health ?? health?.memory?.health ?? 'UNMEASURED');
  const memoryHealthy = $derived(memoryHealthName === 'HEALTHY');
  const memoryFragmentedOnly = $derived(memoryHealthName === 'HEAP_FRAGMENTED');
  const memoryStatusLabel = $derived(
    memoryHealthName === 'UNMEASURED'
      ? 'Not measured'
      : memoryHealthName === 'HEALTHY'
      ? 'Healthy'
      : memoryHealthName === 'LOW_RUNTIME_MEMORY'
        ? 'Low Memory'
        : memoryHealthName === 'HEAP_FRAGMENTED'
          ? 'Fragmented'
          : 'Critical'
  );

  function serviceLabel(key: string): string {
    const labels: Record<string, string> = {
      lighting: 'Lighting',
      scheduler: 'Scheduler',
      api: 'API',
      mdns: 'mDNS',
      fleetDiscovery: 'Fleet',
      artnet: 'Art-Net',
      sacn: 'sACN',
      nodeSync: 'Node Sync',
      mqtt: 'MQTT',
      factoryWorker: 'Factory',
      networkApplyWorker: 'Net Apply',
    };
    return labels[key] ?? key;
  }

  function controlSourceName(cs: number): string {
    const names = ['None', 'Schedule', 'DMX', 'Manual', 'Scene', 'Timeline'];
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

<svelte:head><title>Diagnostics — LumiGrid</title></svelte:head>

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
        aria-pressed={activeTab === 'health'}
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
        aria-pressed={activeTab === 'events'}
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
        aria-pressed={activeTab === 'audit'}
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
      <!-- Actionable Issue Summary -->
      {#if actionableIssues.length === 0}
        <div class="actionable-summary-clean">
          <div class="clean-icon">✓</div>
          <div class="clean-text">
            <strong>All Diagnostics Nominal</strong>
            <span>Configuration integrity verified, safety gates active, network and synchronization within limits.</span>
          </div>
        </div>
      {:else}
        <div class="actionable-summary-card">
          <div class="actionable-header">
            <div class="actionable-title-wrap">
              <span class="actionable-icon">⚠️</span>
              <h3>Actionable Issue Summary</h3>
              <span class="actionable-count-badge">{actionableIssues.length} {actionableIssues.length === 1 ? 'issue' : 'issues'}</span>
            </div>
            <span class="actionable-subtitle">Prioritized diagnostics requiring operator attention</span>
          </div>
          <div class="actionable-list">
            {#each actionableIssues as issue}
              <div class="actionable-item {issue.severity}">
                <div class="item-header">
                  <span class="severity-pill {issue.severity}">{issue.severity.toUpperCase()}</span>
                  <span class="category-pill">{issue.category.toUpperCase()}</span>
                  <span class="issue-title">{issue.title}</span>
                </div>
                <p class="issue-desc">{issue.description}</p>
                {#if issue.remedy}
                  <div class="issue-remedy">
                    <span class="remedy-tag">Remedy:</span> {issue.remedy}
                  </div>
                {/if}
              </div>
            {/each}
          </div>
        </div>
      {/if}

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
            <span class="conf-val" style:color={statusColor(!displayFault)}>
              {displayFault ? 'FAULT' : displayOutputState}
            </span>
          </div>
          <div class="conf-item">
            <span class="conf-label">Emergency Stop</span>
            <span class="conf-val" style:color={statusColor(!displayEstop)}>
              {displayEstop ? 'LATCHED' : 'Disarmed / Normal'}
            </span>
          </div>
        </div>
      </div>

      <!-- Health Telemetry Grid -->
      <div class="stats-grid">
        <!-- System & Compute -->
        <div class="stat-card">
          <div class="card-head">
            <h3>System</h3>
            <span class="card-fps">{health.system.fps.toFixed(1)} FPS</span>
          </div>
          <div class="stat-row">
            <span>Uptime</span>
            <span class="stat-val">{formatUptime(health.system.uptime_s)}</span>
          </div>
          <div class="stat-row">
            <span>Reset Reason</span>
            <span class="stat-val">{health.system.reset_reason}</span>
          </div>
        </div>

        {#if storageDiag}
          <section class="diag-card">
            <h3>Storage</h3>
            <p>Flash {(storageDiag.physicalFlashBytes / 1048576).toFixed(0)} MB · Layout v{storageDiag.partitionLayoutVersion}</p>
            <p>Running {storageDiag.running?.label ?? 'Unknown'} · {storageDiag.health}</p>
            <p>Firmware {(storageDiag.imageBytes / 1048576).toFixed(2)} MB / {((storageDiag.running?.size ?? 0) / 1048576).toFixed(2)} MB</p>
            <details><summary>Partition details</summary>
              {#each storageDiag.partitions as partition}
                <p>{partition.label}: {partition.size} bytes at 0x{partition.address.toString(16)}</p>
              {/each}
            </details>
          </section>
        {/if}
        <!-- Runtime Memory (LG-038.3 §58/§59) -->
        <div class="stat-card">
          <div class="card-head">
            <h3>Runtime Memory</h3>
            <span
              class="status-tag"
              style:color={memoryHealthy ? '#30d158' : memoryFragmentedOnly ? '#ff9f0a' : '#ff453a'}
            >
              {memoryStatusLabel}
            </span>
          </div>
          <div class="stat-row">
            <span>Free heap</span>
            <span class="stat-val mono">{formatBytes(memoryFree8)}</span>
          </div>
          <div class="stat-row">
            <span>Largest block</span>
            <span class="stat-val mono">{formatBytes(memoryLargest8)}</span>
          </div>
          <div class="stat-row">
            <span>Minimum free</span>
            <span class="stat-val mono">{formatBytes(memoryMinFree)}</span>
          </div>
          {#if memoryAllocationFailures > 0}
            <div class="stat-row">
              <span>Allocation failures</span>
              <span class="stat-val mono err">{memoryAllocationFailures}</span>
            </div>
          {/if}
          {#if memoryDiag && memoryDiag.tasks.length > 0}
            <button class="advanced-toggle" type="button" onclick={() => (showAdvancedMemory = !showAdvancedMemory)}>
              {showAdvancedMemory ? 'Hide' : 'Show'} task stacks (Advanced)
            </button>
            {#if showAdvancedMemory}
              {#each memoryDiag.tasks as t}
                <div class="stat-row">
                  <span class="mono">{t.name}</span>
                  <span class="stat-val mono">{formatBytes(t.minimumFreeStack)} / {formatBytes(t.stackBytes)}</span>
                </div>
              {/each}
            {/if}
          {/if}
        </div>

        <!-- Active Services (LG-038.3 §60) -->
        <div class="stat-card">
          <div class="card-head">
            <h3>Active Services</h3>
          </div>
          {#if memoryDiag?.services}
            <div class="service-grid">
              {#each Object.entries(memoryDiag.services) as [name, active]}
                <span class="service-pill {active ? 'on' : 'off'}">{serviceLabel(name)}: {active ? 'On' : 'Off'}</span>
              {/each}
            </div>
          {:else}
            <div class="stat-row"><span>Service state unavailable</span><span class="stat-val">—</span></div>
          {/if}
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
            <span class="pwr-tag">{formatPower(displayPowerMw)}</span>
          </div>
          <div class="stat-grid-2col">
            <div class="stat-row">
              <span>Output State</span>
              <span class="stat-val">{displayOutputState}</span>
            </div>
            <div class="stat-row">
              <span>Fault Detected</span>
              <span class="stat-val" style:color={statusColor(!displayFault)}>
                {displayFault ? 'YES' : 'No'}
              </span>
            </div>
            <div class="stat-row">
              <span>Control Source</span>
              <span class="stat-val">{liveRuntime?.controlOwner ?? controlSourceName(health.output.control_source)}</span>
            </div>
            <div class="stat-row">
              <span>Active Scene</span>
              <span class="stat-val mono">Scene {($nodeStore.snapshot?.scene?.activePreset ?? health.output.active_preset) + 1}</span>
            </div>
            <div class="stat-row">
              <span>Live Render Objects</span>
              <span class="stat-val">{health.output.live_objects}</span>
            </div>
            <div class="stat-row">
              <span>Estimated Power</span>
              <span class="stat-val">{formatPower(displayPowerMw)}</span>
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

        <!-- Output Timing & Physical Engine (LG-038) -->
        <div class="stat-card wide">
          <div class="card-head">
            <h3>Output Engine Timing</h3>
            <span class="status-tag" style:color={deadlineMisses > 0 ? '#ff9f0a' : '#30d158'}>
              {deadlineMisses > 0 ? `${deadlineMisses} Deadline Misses` : 'Nominal 40 FPS Engine'}
            </span>
          </div>
          <div class="stat-grid-2col">
            <div class="stat-row">
              <span>Target Framerate</span>
              <span class="stat-val mono">{targetFps.toFixed(1)} FPS (25.00 ms period)</span>
            </div>
            <div class="stat-row">
              <span>Committed Framerate</span>
              <span class="stat-val mono font-bold" style:color={committedFps < 35 ? '#ff453a' : '#30d158'}>
                {committedFps.toFixed(1)} FPS
              </span>
            </div>
            <div class="stat-row">
              <span>Frame Commit Duration</span>
              <span class="stat-val mono">{frameDurationMs} ms</span>
            </div>
            <div class="stat-row">
              <span>RMT Parallel Wire Time</span>
              <span class="stat-val mono">{rmtDurationMs} ms</span>
            </div>
            <div class="stat-row">
              <span>PWM I2C Time</span>
              <span class="stat-val mono">{pwmDurationMs} ms</span>
            </div>
            <div class="stat-row">
              <span>Frame Headroom</span>
              <span class="stat-val mono" style:color={parseFloat(headroomMs) < 2 ? '#ff453a' : '#30d158'}>{headroomMs} ms</span>
            </div>
            <div class="stat-row">
              <span>Deadline Misses</span>
              <span class="stat-val mono" class:err={deadlineMisses > 0}>{deadlineMisses}</span>
            </div>
            <div class="stat-row">
              <span>Active Lanes / Max Limit</span>
              <span class="stat-val mono">{activeLanes} lanes / 256 px</span>
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
              <span>Timeline Drops</span>
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
    position: sticky;
    top: 0;
    z-index: 18;
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
    background: rgba(22,22,24,.90);
    backdrop-filter: blur(22px);
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
    position: sticky;
    top: 76px;
    z-index: 17;
    display: flex;
    background: rgba(12,12,14,.76);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border-radius: 10px;
    padding: .3rem;
    gap: 0.5rem;
    border: 1px solid rgba(255,255,255,.06);
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
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
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
    background: linear-gradient(180deg, rgba(255,255,255,.035), rgba(255,255,255,.018));
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
  .service-grid {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem;
    padding-top: 0.25rem;
  }
  .service-pill {
    font-size: 0.7rem;
    font-family: monospace;
    font-weight: 700;
    padding: 0.18rem 0.45rem;
    border-radius: 6px;
    border: 1px solid rgba(255, 255, 255, 0.12);
  }
  .service-pill.on {
    background: rgba(48, 209, 88, 0.14);
    color: #30d158;
    border-color: rgba(48, 209, 88, 0.35);
  }
  .service-pill.off {
    background: rgba(255, 255, 255, 0.04);
    color: #86868b;
  }
  .advanced-toggle {
    align-self: flex-start;
    background: transparent;
    border: none;
    color: #86868b;
    font-size: 0.72rem;
    cursor: pointer;
    padding: 0.25rem 0;
    text-decoration: underline dotted;
  }
  .advanced-toggle:hover {
    color: #f5f5f7;
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
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
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
  .search-input:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
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
    scrollbar-color: rgba(255,255,255,.16) transparent;
    scrollbar-width: thin;
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
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
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

  @media (max-width: 768px) {
    .diag-header { position: static; align-items: flex-start; }
    .tab-bar { position: static; }
    .header-actions { width: 100%; justify-content: space-between; }
    .tab-bar { overflow-x: auto; padding-bottom: .35rem; }
    .tab { white-space: nowrap; }
    .confidence-grid, .stats-grid, .stat-grid-2col { grid-template-columns: 1fr; }
    .search-input { width: 100%; }
    .events-toolbar { align-items: stretch; }
    .level-filters { overflow-x: auto; }
  }

  /* Actionable Issue Summary Styles */
  .actionable-summary-clean {
    display: flex;
    align-items: center;
    gap: 1rem;
    padding: 1rem 1.25rem;
    border-radius: 12px;
    background: linear-gradient(135deg, rgba(48, 209, 88, 0.12), rgba(48, 209, 88, 0.04));
    border: 1px solid rgba(48, 209, 88, 0.3);
    margin-bottom: 1.25rem;
  }
  .clean-icon {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    border-radius: 50%;
    background: rgba(48, 209, 88, 0.2);
    color: #30d158;
    font-weight: 700;
    font-size: 1.1rem;
  }
  .clean-text {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
  }
  .clean-text strong {
    color: #30d158;
    font-size: 0.95rem;
  }
  .clean-text span {
    color: #a1a1a6;
    font-size: 0.82rem;
  }

  .actionable-summary-card {
    background: linear-gradient(180deg, rgba(30, 20, 20, 0.75), rgba(20, 18, 20, 0.75));
    border: 1px solid rgba(255, 69, 58, 0.35);
    border-radius: 14px;
    padding: 1.25rem;
    margin-bottom: 1.25rem;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.3);
  }
  .actionable-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem;
    padding-bottom: 0.85rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    margin-bottom: 1rem;
  }
  .actionable-title-wrap {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .actionable-title-wrap h3 {
    margin: 0;
    font-size: 1.05rem;
    font-weight: 700;
    color: #f5f5f7;
  }
  .actionable-count-badge {
    background: rgba(255, 69, 58, 0.2);
    border: 1px solid rgba(255, 69, 58, 0.4);
    color: #ff453a;
    font-size: 0.75rem;
    font-weight: 700;
    padding: 0.2rem 0.55rem;
    border-radius: 10px;
  }
  .actionable-subtitle {
    font-size: 0.8rem;
    color: #86868b;
  }
  .actionable-list {
    display: flex;
    flex-direction: column;
    gap: 0.85rem;
  }
  .actionable-item {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-left-width: 4px;
    border-radius: 8px;
    padding: 0.85rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .actionable-item.critical {
    border-left-color: #ff453a;
    background: rgba(255, 69, 58, 0.06);
  }
  .actionable-item.warning {
    border-left-color: #ff9f0a;
    background: rgba(255, 159, 10, 0.06);
  }
  .actionable-item.info {
    border-left-color: var(--dash-led);
    background: rgba(255, 255, 255, 0.04);
  }
  .item-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .severity-pill {
    font-size: 0.68rem;
    font-weight: 800;
    letter-spacing: 0.05em;
    padding: 0.15rem 0.45rem;
    border-radius: 4px;
  }
  .severity-pill.critical {
    background: rgba(255, 69, 58, 0.25);
    color: #ff453a;
  }
  .severity-pill.warning {
    background: rgba(255, 159, 10, 0.25);
    color: #ff9f0a;
  }
  .severity-pill.info {
    background: rgba(255, 255, 255, 0.12);
    color: var(--dash-led);
  }
  .category-pill {
    font-size: 0.68rem;
    font-weight: 700;
    padding: 0.15rem 0.45rem;
    border-radius: 4px;
    background: rgba(255, 255, 255, 0.08);
    color: #a1a1a6;
  }
  .issue-title {
    font-weight: 600;
    font-size: 0.88rem;
    color: #f5f5f7;
  }
  .issue-desc {
    margin: 0;
    font-size: 0.82rem;
    color: #c7c7cc;
    line-height: 1.4;
  }
  .issue-remedy {
    font-size: 0.8rem;
    color: #e5e5ea;
    background: rgba(0, 0, 0, 0.25);
    padding: 0.4rem 0.6rem;
    border-radius: 6px;
    border: 1px solid rgba(255, 255, 255, 0.05);
    margin-top: 0.2rem;
  }
  .remedy-tag {
    color: #ff9f0a;
    font-weight: 700;
  }


</style>
