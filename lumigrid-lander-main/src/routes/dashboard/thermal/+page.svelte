<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getInfo } from '$lib/api/node';
  import type { NodeInfo } from '$lib/api/node';
  import { readString, writeString } from '$lib/auth/storage';
  import { NODE_IP_KEY } from '$lib/auth/storage';

  // ── Feature 2: Thermal Dashboard ─────────────────────────────────────────────

  let nodeIp    = $state('');
  let info      = $state<NodeInfo | null>(null);
  let error     = $state('');
  let history   = $state<number[]>([]);
  let alertTemp = $state(65);
  let alertSent = $state(false);
  let polling: ReturnType<typeof setInterval> | null = null;

  function getTemp(i: NodeInfo | null): number | null {
    const arr = i?.u?.Lumigrid_PCA9685?.temp;
    if (!Array.isArray(arr) || !arr.length) return null;
    return Number(arr[0]);
  }

  async function poll() {
    error = '';
    try {
      info = await getInfo(nodeIp);
      const t = getTemp(info);
      if (t !== null) {
        history = [...history.slice(-59), t];
        if (t >= alertTemp && !alertSent) {
          alertSent = true;
          if ('Notification' in window && Notification.permission === 'granted') {
            new Notification('LumiGrid Thermal Alert', {
              body: `Node ${nodeIp} reached ${t.toFixed(1)} °C`,
              icon: '/favicon.ico'
            });
          }
        } else if (t < alertTemp - 5) {
          alertSent = false;
        }
      }
    } catch (e) {
      error = String(e);
    }
  }

  onMount(async () => {
    nodeIp = readString(NODE_IP_KEY);
    alertTemp = Number(readString('lg_alert_temp', '65'));
    if (nodeIp) { await poll(); polling = setInterval(poll, 5000); }
  });
  onDestroy(() => { if (polling) clearInterval(polling); });

  function requestNotifPerm() {
    if ('Notification' in window) Notification.requestPermission();
  }

  function tempToColor(t: number): string {
    if (t < 40) return '#34c759';
    if (t < 55) return '#ff9500';
    if (t < 65) return '#ff6b30';
    return '#ff453a';
  }

  function arcPath(pct: number, r = 54): string {
    const startAngle = -220 * (Math.PI / 180);
    const sweep = 260 * (Math.PI / 180);
    const endAngle = startAngle + sweep * Math.min(pct, 1);
    const cx = 64, cy = 64;
    const x1 = cx + r * Math.cos(startAngle);
    const y1 = cy + r * Math.sin(startAngle);
    const x2 = cx + r * Math.cos(endAngle);
    const y2 = cy + r * Math.sin(endAngle);
    const large = (sweep * Math.min(pct, 1)) > Math.PI ? 1 : 0;
    return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
  }

  function sparkPath(vals: number[]): string {
    if (!vals.length) return '';
    const W = 400, H = 64, pad = 4;
    const min = Math.min(...vals) - 2;
    const max = Math.max(...vals, min + 10) + 2;
    return vals.map((v, i) => {
      const x = pad + (i / Math.max(vals.length - 1, 1)) * (W - pad * 2);
      const y = H - pad - ((v - min) / (max - min)) * (H - pad * 2);
      return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
    }).join(' ');
  }

  $effect(() => { writeString('lg_alert_temp', String(alertTemp)); });

  const MAX_TEMP = 85;
  let currentTemp = $derived(getTemp(info));
  let tempPct = $derived(currentTemp !== null ? currentTemp / MAX_TEMP : 0);
  let col = $derived(currentTemp !== null ? tempToColor(currentTemp) : '#a1a1a6');
</script>

<svelte:head>
  <title>Thermal Monitor — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Thermal Monitor</h1>
      <p class="sub">{nodeIp || 'No node selected'} · polling every 5 s</p>
    </div>
    <button class="action-btn" onclick={requestNotifPerm}>🔔 Enable Alerts</button>
  </header>

  {#if !nodeIp}
    <div class="empty">Select a node in the sidebar to monitor temperature.</div>
  {:else if error}
    <div class="err-box">{error}</div>
  {:else}
    <div class="dash-grid">
      <div class="card gauge-card">
        <h2>Current Temperature</h2>
        <svg viewBox="0 0 128 128" class="gauge-svg" aria-label="Temperature gauge">
          <path d={arcPath(1)} stroke="rgba(255,255,255,.08)" stroke-width="10" fill="none" stroke-linecap="round" />
          <path d={arcPath(tempPct)} stroke={col} stroke-width="10" fill="none" stroke-linecap="round"
                style:filter={`drop-shadow(0 0 6px ${col}88)`} />
          <text x="64" y="68" text-anchor="middle" dominant-baseline="middle"
                font-size="22" font-weight="700" fill={col} font-family="monospace">
            {currentTemp !== null ? currentTemp.toFixed(1) : '—'}
          </text>
          <text x="64" y="84" text-anchor="middle" font-size="9" fill="#a1a1a6">°C</text>
        </svg>
        <div class="temp-status" style:color={col}>
          {currentTemp === null ? 'No sensor' : currentTemp < 45 ? 'Normal' : currentTemp < 65 ? 'Warm' : 'HOT'}
        </div>
      </div>

      <div class="card info-card">
        <h2>Node Info</h2>
        {#if info}
          <dl class="info-dl">
            <dt>Name</dt><dd>{info.name}</dd>
            <dt>Version</dt><dd>{info.ver}</dd>
            <dt>Uptime</dt><dd>{Math.round((info.uptime ?? 0) / 3600)} h {Math.round(((info.uptime ?? 0) % 3600) / 60)} m</dd>
            <dt>Free Heap</dt><dd>{Math.round((info.freeheap ?? 0) / 1024)} KB</dd>
            <dt>MAC</dt><dd class="mono">{info.mac ?? '—'}</dd>
          </dl>
        {/if}
        <div class="alert-section">
          <label class="alert-label" for="alert-range-input">
            Alert threshold
            <span class="alert-val">{alertTemp} °C</span>
          </label>
          <input id="alert-range-input" type="range" min="30" max="80" step="1" bind:value={alertTemp} class="alert-range" />
        </div>
      </div>

      <div class="card spark-card">
        <h2>Temperature History ({history.length} readings)</h2>
        {#if history.length > 1}
          <svg viewBox="0 0 400 64" class="spark-svg" preserveAspectRatio="none">
            {#if history.length}
              {@const min = Math.min(...history) - 2}
              {@const max = Math.max(...history, min + 10) + 2}
              {@const ay = 64 - 4 - ((alertTemp - min) / (max - min)) * (64 - 8)}
              <line x1="4" y1={ay} x2="396" y2={ay} stroke="rgba(255,69,58,.4)" stroke-width="1" stroke-dasharray="4 3" />
            {/if}
            <defs>
              <linearGradient id="sg" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stop-color={col} stop-opacity="0.3" />
                <stop offset="100%" stop-color={col} stop-opacity="0.02" />
              </linearGradient>
            </defs>
            <path d={sparkPath(history) + ` L ${396} 60 L 4 60 Z`} fill="url(#sg)" />
            <path d={sparkPath(history)} stroke={col} stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round" />
          </svg>
          <div class="spark-labels">
            <span>{history[0].toFixed(1)} °C</span>
            <span class="mono" style:color="#a1a1a6">← 5 min ago</span>
            <span>{history[history.length - 1].toFixed(1)} °C</span>
          </div>
        {:else}
          <div class="empty-spark">Collecting data…</div>
        {/if}
      </div>
    </div>
  {/if}
</div>

<style>
  .page { max-width: 900px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .action-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 999px; color: #f5f5f7; padding: .5rem 1rem; cursor: pointer; font-size: .85rem; }
  .empty { color: #a1a1a6; padding: 3rem; text-align: center; }
  .err-box { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3); border-radius: 12px; color: #ff453a; padding: 1rem; font-size: .85rem; font-family: monospace; }
  .dash-grid { display: grid; grid-template-columns: 180px 1fr; grid-template-rows: auto auto; gap: 1rem; }
  @media (max-width: 600px) { .dash-grid { grid-template-columns: 1fr; } }
  .card { background: rgba(29,29,31,.75); backdrop-filter: blur(16px); border: 1px solid rgba(255,255,255,.08); border-radius: 20px; padding: 1.25rem; }
  h2 { margin: 0 0 1rem; font-size: .95rem; font-weight: 600; color: #f5f5f7; }
  .gauge-card { grid-row: 1 / 2; display: flex; flex-direction: column; align-items: center; gap: .5rem; }
  .gauge-svg { width: 100%; max-width: 160px; }
  .temp-status { font-size: .85rem; font-weight: 700; letter-spacing: .05em; }
  .info-card { grid-row: 1 / 2; }
  .info-dl { display: grid; grid-template-columns: auto 1fr; gap: .25rem .75rem; margin: 0 0 1rem; }
  dt { font-size: .72rem; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; align-self: center; }
  dd { margin: 0; font-size: .85rem; color: #f5f5f7; font-weight: 600; }
  .mono { font-family: monospace; }
  .alert-section { border-top: 1px solid rgba(255,255,255,.07); padding-top: .75rem; }
  .alert-label { display: flex; justify-content: space-between; font-size: .82rem; color: #a1a1a6; margin-bottom: .35rem; }
  .alert-val { color: #ff9500; font-weight: 600; }
  .alert-range { width: 100%; accent-color: #ff9500; }
  .spark-card { grid-column: 1 / 3; }
  .spark-svg { width: 100%; height: 80px; }
  .spark-labels { display: flex; justify-content: space-between; font-size: .72rem; color: #a1a1a6; margin-top: .25rem; }
  .empty-spark { color: #a1a1a6; font-size: .85rem; text-align: center; padding: 1.5rem 0; }
  @media (max-width: 600px) { .spark-card { grid-column: 1; } }
</style>
