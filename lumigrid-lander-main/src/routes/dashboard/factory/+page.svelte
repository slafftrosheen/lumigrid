<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    getFactoryStatus,
    postFactoryEffect,
    postFactoryLane,
    postFactoryPwm,
    postFactoryAbort,
    postFactoryAutoLoop,
    type FactoryStatus,
    type FactoryEffectName,
  } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';

  let nodeIp = $state('');
  let status = $state<FactoryStatus | null>(null);
  let loading = $state(true);
  let error = $state('');
  let flash = $state('');

  let brightness = $state(32);
  let durationMs = $state(10000);
  let autoRefresh = $state(false);
  let refreshTimer: ReturnType<typeof setInterval> | null = null;

  const EFFECTS: { name: FactoryEffectName; label: string; icon: string; color: string }[] = [
    { name: 'identity',       label: 'Identity',       icon: '◻', color: '#8e8e93' },
    { name: 'pixel_chase',    label: 'Pixel Chase',    icon: '→', color: '#00d4ff' },
    { name: 'rainbow',        label: 'Rainbow',        icon: '◐', color: '#ff2d55' },
    { name: 'white_walk',     label: 'White Walk',     icon: '◈', color: '#f5f5f7' },
    { name: 'pwm_walk',       label: 'PWM Walk',       icon: '▤', color: '#ff9500' },
    { name: 'pwm_fade',       label: 'PWM Fade',       icon: '◑', color: '#5856d6' },
    { name: 'combined_pulse', label: 'Combined Pulse', icon: '◉', color: '#34c759' },
    { name: 'burn_in',        label: 'Burn-In',        icon: '⬥', color: '#ff3b30' },
    { name: 'blackout',       label: 'Blackout',       icon: '⬛', color: '#1c1c1e' },
  ];

  const LANES = Array.from({ length: 8 }, (_, i) => i);
  const PWM_CHANNELS = Array.from({ length: 8 }, (_, i) => i);

  let laneColors = $state(Array.from({ length: 8 }, () => ({ r: 255, g: 0, b: 0 })));
  let pwmDuties = $state(Array.from({ length: 8 }, () => 0));

  onMount(async () => {
    nodeIp = getSelectedNodeIp();
    if (nodeIp) await refresh();
    loading = false;
  });

  onDestroy(() => {
    if (refreshTimer) clearInterval(refreshTimer);
  });

  async function refresh() {
    if (!nodeIp) return;
    try {
      status = await getFactoryStatus(nodeIp);
      error = '';
    } catch (e) {
      error = String(e);
    }
  }

  function toggleAutoRefresh() {
    autoRefresh = !autoRefresh;
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
    if (autoRefresh) {
      refreshTimer = setInterval(refresh, 2000);
    }
  }

  async function sendEffect(name: FactoryEffectName) {
    if (!nodeIp) return;
    try {
      await postFactoryEffect(nodeIp, name, brightness, durationMs);
      flash = `Sent: ${name}`;
      setTimeout(() => flash = '', 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendAbort() {
    if (!nodeIp) return;
    try {
      await postFactoryAbort(nodeIp);
      flash = 'Blackout sent';
      setTimeout(() => flash = '', 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function toggleAutoLoop() {
    if (!nodeIp || !status) return;
    try {
      await postFactoryAutoLoop(nodeIp, !status.autoLoop);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendLane(lane: number) {
    if (!nodeIp) return;
    const c = laneColors[lane];
    try {
      await postFactoryLane(nodeIp, lane, c.r, c.g, c.b, brightness, durationMs);
      flash = `Lane ${lane} sent`;
      setTimeout(() => flash = '', 2000);
    } catch (e) {
      error = String(e);
    }
  }

  async function sendPwm(channel: number) {
    if (!nodeIp) return;
    try {
      await postFactoryPwm(nodeIp, channel, pwmDuties[channel], durationMs);
      flash = `PWM ${channel} sent`;
      setTimeout(() => flash = '', 2000);
    } catch (e) {
      error = String(e);
    }
  }

  function setLaneColor(lane: number, hex: string) {
    const r = parseInt(hex.slice(1, 3), 16);
    const g = parseInt(hex.slice(3, 5), 16);
    const b = parseInt(hex.slice(5, 7), 16);
    laneColors[lane] = { r, g, b };
    laneColors = [...laneColors];
  }

  function laneHex(lane: number): string {
    const c = laneColors[lane];
    return '#' + [c.r, c.g, c.b].map(v => v.toString(16).padStart(2, '0')).join('');
  }
</script>

<svelte:head>
  <title>Factory / Burn-In — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Factory / Burn-In</h1>
      <p class="sub">{nodeIp || 'No node selected'}</p>
    </div>
    <div class="head-btns">
      <button class="ghost-btn" onclick={toggleAutoRefresh}>
        {autoRefresh ? '⏸ Pause' : '▶ Auto-refresh'}
      </button>
      <button class="ghost-btn" onclick={refresh}>↻ Refresh</button>
    </div>
  </header>

  {#if error}<div class="err-box">{error}</div>{/if}
  {#if flash}<div class="flash-box">{flash}</div>{/if}

  {#if loading}
    <div class="loading"><div class="spinner"></div>Loading…</div>
  {:else if !nodeIp}
    <div class="empty">Select a node in the sidebar to begin.</div>
  {:else}
    <!-- Status card -->
    <div class="status-card">
      <div class="status-grid">
        <div class="stat">
          <span class="stat-label">Active Effect</span>
          <span class="stat-value" class:active-effect={status?.testActive}>
            {status?.effect ?? '—'}
          </span>
        </div>
        <div class="stat">
          <span class="stat-label">Brightness</span>
          <span class="stat-value">{status?.brightness ?? 0}</span>
        </div>
        <div class="stat">
          <span class="stat-label">Test Active</span>
          <span class="stat-value" class:ok={status?.testActive} class:off={!status?.testActive}>
            {status?.testActive ? 'Yes' : 'No'}
          </span>
        </div>
        <div class="stat">
          <span class="stat-label">Outputs</span>
          <span class="stat-value" class:ok={status?.outputsEnabled} class:off={!status?.outputsEnabled}>
            {status?.outputsEnabled ? 'Enabled' : 'Disabled'}
          </span>
        </div>
        <div class="stat">
          <span class="stat-label">Auto Loop</span>
          <span class="stat-value" class:ok={status?.autoLoop} class:off={!status?.autoLoop}>
            {status?.autoLoop ? 'On' : 'Off'}
          </span>
        </div>
        <div class="stat">
          <span class="stat-label">Rejections</span>
          <span class="stat-value">{status?.commandRejections ?? 0}</span>
        </div>
      </div>
    </div>

    <!-- Controls row -->
    <div class="controls-row">
      <label class="control-field">
        <span>Brightness</span>
        <input type="range" min="0" max="96" bind:value={brightness} />
        <span class="control-val">{brightness}</span>
      </label>
      <label class="control-field">
        <span>Duration (ms)</span>
        <input type="number" min="1" max="600000" bind:value={durationMs} />
      </label>
      <button class="auto-loop-btn" class:on={status?.autoLoop} onclick={toggleAutoLoop}>
        Auto Loop: {status?.autoLoop ? 'ON' : 'OFF'}
      </button>
      <button class="abort-btn" onclick={sendAbort}>⬛ BLACKOUT</button>
    </div>

    <!-- Effects grid -->
    <div class="section">
      <h2>Test Effects</h2>
      <div class="effects-grid">
        {#each EFFECTS as fx}
          <button
            class="effect-btn"
            class:is-active={status?.effect === fx.name}
            style="--fx-color={fx.color}"
            onclick={() => sendEffect(fx.name)}
          >
            <span class="fx-icon">{fx.icon}</span>
            <span class="fx-label">{fx.label}</span>
          </button>
        {/each}
      </div>
    </div>

    <!-- Pixel lanes -->
    <div class="section">
      <h2>Pixel Lanes (0–7)</h2>
      <div class="lanes-grid">
        {#each LANES as lane}
          <div class="lane-row">
            <span class="lane-num">L{lane}</span>
            <input
              type="color"
              value={laneHex(lane)}
              onchange={(e) => setLaneColor(lane, e.currentTarget.value)}
            />
            <span class="lane-rgb">
              {laneColors[lane].r}, {laneColors[lane].g}, {laneColors[lane].b}
            </span>
            <button class="send-btn" onclick={() => sendLane(lane)}>Send</button>
          </div>
        {/each}
      </div>
    </div>

    <!-- PWM channels -->
    <div class="section">
      <h2>PWM Channels (0–7)</h2>
      <div class="pwm-grid">
        {#each PWM_CHANNELS as ch}
          <div class="pwm-row">
            <span class="pwm-num">CH{ch}</span>
            <input type="range" min="0" max="4095" bind:value={pwmDuties[ch]} />
            <span class="pwm-val">{pwmDuties[ch]}</span>
            <button class="send-btn" onclick={() => sendPwm(ch)}>Set</button>
          </div>
        {/each}
      </div>
    </div>
  {/if}
</div>

<style>
  .page { max-width: 960px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .head-btns { display: flex; gap: .6rem; }
  .ghost-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.14); border-radius: 999px; color: #f5f5f7; padding: .55rem 1.1rem; cursor: pointer; font-size: .88rem; transition: background .2s; }
  .ghost-btn:hover { background: rgba(255,255,255,.14); }
  .err-box { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3); border-radius: 12px; color: #ff453a; padding: .9rem 1rem; font-size: .85rem; margin-bottom: 1rem; }
  .flash-box { background: rgba(52,199,89,.12); border: 1px solid rgba(52,199,89,.3); border-radius: 12px; color: #34c759; padding: .9rem 1rem; font-size: .85rem; margin-bottom: 1rem; }
  .loading { display: flex; align-items: center; gap: .75rem; color: #a1a1a6; padding: 3rem; justify-content: center; }
  .spinner { width: 18px; height: 18px; border: 2px solid rgba(255,255,255,.1); border-top-color: #ff2d55; border-radius: 50%; animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .empty { color: #a1a1a6; padding: 3rem; text-align: center; }

  .status-card { background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 18px; padding: 1.25rem; margin-bottom: 1rem; }
  .status-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); gap: 1rem; }
  .stat { display: flex; flex-direction: column; gap: .2rem; }
  .stat-label { font-size: .72rem; text-transform: uppercase; letter-spacing: .05em; color: #a1a1a6; font-weight: 600; }
  .stat-value { font-size: 1.1rem; font-weight: 700; color: #f5f5f7; }
  .stat-value.ok { color: #34c759; }
  .stat-value.off { color: #a1a1a6; }
  .active-effect { color: #ff9500; text-transform: capitalize; }

  .controls-row { display: flex; align-items: flex-end; gap: 1rem; flex-wrap: wrap; background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 18px; padding: 1rem 1.25rem; margin-bottom: 1rem; }
  .control-field { display: flex; flex-direction: column; gap: .3rem; flex: 1; min-width: 120px; }
  .control-field span { font-size: .72rem; text-transform: uppercase; letter-spacing: .05em; color: #a1a1a6; font-weight: 600; }
  .control-field input[type="range"] { width: 100%; accent-color: #ff2d55; }
  .control-field input[type="number"] { background: rgba(255,255,255,.06); border: 1px solid rgba(255,255,255,.12); border-radius: 8px; color: #f5f5f7; font-size: .9rem; padding: .45rem .6rem; }
  .control-val { font-size: .85rem; color: #f5f5f7; font-weight: 600; text-align: center; }

  .auto-loop-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.14); border-radius: 999px; color: #f5f5f7; padding: .55rem 1.1rem; cursor: pointer; font-size: .85rem; white-space: nowrap; transition: all .2s; }
  .auto-loop-btn.on { background: rgba(52,199,89,.2); border-color: rgba(52,199,89,.4); color: #34c759; }

  .abort-btn { background: rgba(255,59,48,.85); border: none; border-radius: 999px; color: #fff; font-weight: 700; padding: .55rem 1.3rem; cursor: pointer; font-size: .9rem; box-shadow: 0 4px 14px rgba(255,59,48,.4); transition: all .2s; white-space: nowrap; }
  .abort-btn:hover { background: #ff3b30; box-shadow: 0 6px 20px rgba(255,59,48,.55); transform: scale(1.02); }

  .section { background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 18px; padding: 1.25rem; margin-bottom: 1rem; }
  h2 { margin: 0 0 1rem; font-size: .95rem; font-weight: 600; color: #f5f5f7; }

  .effects-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: .75rem; }
  .effect-btn { display: flex; flex-direction: column; align-items: center; gap: .4rem; padding: 1rem .5rem; background: rgba(255,255,255,.04); border: 2px solid rgba(255,255,255,.1); border-radius: 14px; cursor: pointer; transition: all .2s; color: #f5f5f7; }
  .effect-btn:hover { background: rgba(255,255,255,.08); border-color: var(--fx-color); transform: translateY(-2px); }
  .effect-btn.is-active { border-color: var(--fx-color); background: color-mix(in srgb, var(--fx-color) 15%, transparent); box-shadow: 0 0 16px color-mix(in srgb, var(--fx-color) 30%, transparent); }
  .fx-icon { font-size: 1.5rem; }
  .fx-label { font-size: .75rem; font-weight: 600; text-align: center; }

  .lanes-grid { display: flex; flex-direction: column; gap: .5rem; }
  .lane-row { display: flex; align-items: center; gap: .75rem; padding: .5rem .75rem; background: rgba(255,255,255,.03); border-radius: 10px; }
  .lane-num { font-size: .8rem; font-weight: 700; color: #a1a1a6; width: 28px; font-family: monospace; }
  .lane-row input[type="color"] { width: 36px; height: 28px; border: none; border-radius: 6px; cursor: pointer; background: none; padding: 0; }
  .lane-row input[type="color"]::-webkit-color-swatch-wrapper { padding: 0; }
  .lane-row input[type="color"]::-webkit-color-swatch { border: 1px solid rgba(255,255,255,.2); border-radius: 5px; }
  .lane-rgb { font-size: .78rem; color: #a1a1a6; font-family: monospace; min-width: 80px; }

  .pwm-grid { display: flex; flex-direction: column; gap: .5rem; }
  .pwm-row { display: flex; align-items: center; gap: .75rem; padding: .5rem .75rem; background: rgba(255,255,255,.03); border-radius: 10px; }
  .pwm-num { font-size: .8rem; font-weight: 700; color: #a1a1a6; width: 36px; font-family: monospace; }
  .pwm-row input[type="range"] { flex: 1; accent-color: #ff9500; }
  .pwm-val { font-size: .85rem; color: #f5f5f7; font-weight: 600; font-family: monospace; min-width: 45px; text-align: right; }

  .send-btn { background: rgba(255,45,85,.15); border: 1px solid rgba(255,45,85,.3); border-radius: 8px; color: #ff2d55; font-size: .78rem; font-weight: 600; padding: .3rem .75rem; cursor: pointer; transition: all .2s; }
  .send-btn:hover { background: rgba(255,45,85,.25); }
</style>
