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
  import { nodeStore, refreshHealth } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let status = $state<FactoryStatus | null>(null);
  let loading = $state(true);
  let error = $state('');
  let flash = $state('');

  let brightness = $state(32);
  let durationMs = $state(10000);
  let aborting = $state(false);

  // Live runtime state derived directly from authoritative nodeStore snapshot
  const liveRuntime = $derived($nodeStore.snapshot?.runtime);
  const liveOutput = $derived($nodeStore.snapshot?.output);
  const liveJob = $derived($nodeStore.snapshot?.job);

  const isTestActive = $derived(liveRuntime?.testActive ?? status?.testActive ?? false);
  const activeEffect = $derived(liveRuntime?.effect ?? status?.effect ?? 'None (Idle)');
  const isOutputsEnabled = $derived(liveOutput?.enabled ?? status?.outputsEnabled ?? false);
  const isAutoLoop = $derived(liveRuntime?.autoLoop ?? status?.autoLoop ?? false);
  const activeBrightness = $derived(liveRuntime?.brightness ?? status?.brightness ?? 0);

  const ALL_EFFECTS: { name: FactoryEffectName; label: string; desc: string; icon: string; category: 'quick' | 'burnin' | 'pwm' }[] = [
    { name: 'identity',       label: 'Identity',       desc: 'Flashing node identification pulse', icon: '◻', category: 'quick' },
    { name: 'pixel_chase',    label: 'Pixel Chase',    desc: 'Sequential lane walker',             icon: '→', category: 'quick' },
    { name: 'rainbow',        label: 'Rainbow',        desc: 'Continuous spectrum sweep',         icon: '◐', category: 'quick' },
    { name: 'white_walk',     label: 'White Walk',     desc: 'Full spectrum lumen test',          icon: '◈', category: 'quick' },
    { name: 'combined_pulse', label: 'Combined Pulse', desc: 'Synchronized LED and PWM cycle',     icon: '◉', category: 'quick' },
    { name: 'blackout',       label: 'Blackout',       desc: 'Immediately blank all outputs',     icon: '⬛', category: 'quick' },
    { name: 'pwm_walk',       label: 'PWM Walk',       desc: 'Step through auxiliary channels',   icon: '▤', category: 'pwm' },
    { name: 'pwm_fade',       label: 'PWM Fade',       desc: 'Smooth triangle modulation',        icon: '◑', category: 'pwm' },
    { name: 'burn_in',        label: 'Burn-In Stress', desc: 'Full-system thermal stress cycle',  icon: '⬥', category: 'burnin' },
  ];

  const LANES = Array.from({ length: 8 }, (_, i) => i);
  const PWM_CHANNELS = Array.from({ length: 8 }, (_, i) => i);

  let laneColors = $state(Array.from({ length: 8 }, () => ({ r: 255, g: 0, b: 0 })));
  let pwmDuties = $state(Array.from({ length: 8 }, () => 0));

  let refreshGen = 0;

  async function refresh(targetIp = nodeIp, gen = refreshGen) {
    if (!targetIp) {
      status = null;
      loading = false;
      return;
    }
    try {
      const s = await getFactoryStatus(targetIp);
      if (gen !== refreshGen) return;
      status = s;
      error = '';
    } catch (e) {
      if (gen !== refreshGen) return;
      error = String(e);
    } finally {
      if (gen === refreshGen) {
        loading = false;
      }
    }
  }

  async function sendEffect(name: FactoryEffectName) {
    if (!nodeIp) return;
    try {
      await postFactoryEffect(nodeIp, name, brightness, durationMs);
      flash = `Launched test effect: ${name}`;
      setTimeout(() => (flash = ''), 2500);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendAbort() {
    if (!nodeIp || aborting) return;
    aborting = true;
    try {
      await postFactoryAbort(nodeIp);
      flash = 'Emergency Blackout command dispatched';
      setTimeout(() => (flash = ''), 2500);
      await refresh();
      refreshHealth(nodeIp).catch(() => {});
    } catch (e) {
      error = String(e);
    } finally {
      aborting = false;
    }
  }

  async function toggleAutoLoop() {
    if (!nodeIp || !status) return;
    try {
      await postFactoryAutoLoop(nodeIp, !status.autoLoop);
      await refresh();
      flash = `Auto-loop ${!status.autoLoop ? 'enabled' : 'disabled'}`;
      setTimeout(() => (flash = ''), 2000);
    } catch (e) {
      error = String(e);
    }
  }

  async function sendLane(lane: number) {
    if (!nodeIp) return;
    const c = laneColors[lane];
    try {
      await postFactoryLane(nodeIp, lane, c.r, c.g, c.b, brightness, durationMs);
      flash = `Lane ${lane} pattern sent (RGB ${c.r},${c.g},${c.b})`;
      setTimeout(() => (flash = ''), 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  async function sendPwm(channel: number) {
    if (!nodeIp) return;
    try {
      await postFactoryPwm(nodeIp, channel, pwmDuties[channel], durationMs);
      flash = `PWM channel ${channel} set to ${pwmDuties[channel]}`;
      setTimeout(() => (flash = ''), 2000);
      await refresh();
    } catch (e) {
      error = String(e);
    }
  }

  function setLaneColor(lane: number, hex: string) {
    const r = parseInt(hex.slice(1, 3), 16) || 0;
    const g = parseInt(hex.slice(3, 5), 16) || 0;
    const b = parseInt(hex.slice(5, 7), 16) || 0;
    laneColors[lane] = { r, g, b };
    laneColors = [...laneColors];
  }

  function laneHex(lane: number): string {
    const c = laneColors[lane];
    return '#' + [c.r, c.g, c.b].map(v => v.toString(16).padStart(2, '0')).join('');
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        refreshGen++;
        if (nodeIp) {
          status = null;
          loading = true;
          refresh(nodeIp, refreshGen);
        } else {
          status = null;
          loading = false;
          error = '';
        }
      }
    });
    return unsub;
  });
</script>

<svelte:head>
  <title>Factory & Burn-In Console — LumiGrid</title>
</svelte:head>

<div class="factory-root">
  <!-- Page Header -->
  <header class="factory-header">
    <div>
      <h1>Factory & Burn-In Console</h1>
      <p class="sub">Node: <span class="ip-mono">{nodeIp || 'No node selected'}</span></p>
    </div>

    <div class="header-right">
      <button class="ghost-btn" onclick={() => refresh()} type="button">↻ Refresh</button>
    </div>
  </header>

  {#if error}
    <div class="error-banner">{error}</div>
  {/if}
  {#if flash}
    <div class="flash-banner">{flash}</div>
  {/if}

  {#if loading}
    <div class="loading-panel">
      <div class="spinner"></div>
      <p>Querying factory burn-in telemetry...</p>
    </div>
  {:else if !nodeIp}
    <div class="empty-panel">
      <p>No active node selected. Connect to a node in the header to run factory diagnostics.</p>
    </div>
  {:else}
    <!-- SECTION 1: ABORT / BLACKOUT (Top High-Priority Safety Bar) -->
    <div class="abort-banner">
      <div class="abort-info">
        <span class="abort-icon">⚠️</span>
        <div>
          <h3 class="abort-title">Emergency Blackout Authority</h3>
          <p class="abort-desc">Instantly suppresses all pixel and PWM outputs, aborts active tests, and disarms hardware drivers.</p>
        </div>
      </div>
      <button
        class="abort-btn"
        onclick={sendAbort}
        disabled={aborting}
        type="button"
        title="Trigger immediate hardware blackout"
      >
        {aborting ? 'BLACKING OUT...' : '⬛ IMMEDIATE BLACKOUT'}
      </button>
    </div>

    <!-- SECTION 2: FACTORY STATUS -->
    <section class="panel status-panel">
      <div class="panel-head">
        <h2>Factory Status</h2>
        {#if isTestActive}
          <span class="status-live-pill">
            <span class="pulse-dot"></span> TEST RUNNING
          </span>
        {:else}
          <span class="status-idle-pill">IDLE / READY</span>
        {/if}
      </div>

      <div class="status-grid">
        <div class="stat-card">
          <span class="stat-label">Active Effect</span>
          <span class="stat-val {isTestActive ? 'active' : ''}">
            {activeEffect}
          </span>
        </div>

        <div class="stat-card">
          <span class="stat-label">Hardware Outputs</span>
          <span class="stat-val {isOutputsEnabled ? 'ok' : 'muted'}">
            {isOutputsEnabled ? 'Enabled' : 'Disabled'}
          </span>
        </div>

        <div class="stat-card">
          <span class="stat-label">Active Test Brightness</span>
          <span class="stat-val mono">{activeBrightness} / 96</span>
        </div>

        <div class="stat-card">
          <span class="stat-label">Automated Loop</span>
          <span class="stat-val {isAutoLoop ? 'ok' : 'muted'}">
            {isAutoLoop ? 'Active (Looping)' : 'Disabled'}
          </span>
        </div>

        <div class="stat-card">
          <span class="stat-label">Command Rejections</span>
          <span class="stat-val {status?.commandRejections ? 'err' : ''}">
            {status?.commandRejections ?? 0}
          </span>
        </div>
      </div>
    </section>

    <!-- Global Parameters Bar (Before Launching Tests) -->
    <div class="params-bar">
      <div class="param-group">
        <label class="param-label" for="param-brightness">Test Brightness (0–96)</label>
        <div class="param-slider-wrap">
          <input
            id="param-brightness"
            type="range"
            min="0"
            max="96"
            bind:value={brightness}
          />
          <span class="param-val">{brightness}</span>
        </div>
      </div>

      <div class="param-group">
        <label class="param-label" for="param-duration">Duration (Milliseconds)</label>
        <div class="param-input-wrap">
          <input
            id="param-duration"
            type="number"
            min="100"
            max="600000"
            step="500"
            bind:value={durationMs}
          />
          <span class="param-unit">{(durationMs / 1000).toFixed(1)}s</span>
        </div>
      </div>

      <div class="param-actions">
        <button
          class="btn-autoloop"
          class:on={status?.autoLoop}
          onclick={toggleAutoLoop}
          type="button"
          title="Toggle automatic cycling through factory test sequences"
        >
          Auto-Loop: {status?.autoLoop ? 'ON' : 'OFF'}
        </button>
      </div>
    </div>

    <!-- SECTION 3: QUICK TESTS -->
    <section class="panel">
      <div class="panel-head">
        <h2>Quick Tests</h2>
        <span class="head-hint">Standard visual and hardware verification routines</span>
      </div>

      <div class="effects-grid">
        {#each ALL_EFFECTS.filter(e => e.category === 'quick') as fx}
          <button
            class="fx-card"
            class:active={status?.effect === fx.name}
            onclick={() => sendEffect(fx.name)}
            type="button"
          >
            <div class="fx-top">
              <span class="fx-icon">{fx.icon}</span>
              <span class="fx-name">{fx.label}</span>
            </div>
            <p class="fx-desc">{fx.desc}</p>
          </button>
        {/each}
      </div>
    </section>

    <!-- SECTION 4: PIXEL TEST (Lanes 0–7) -->
    <section class="panel">
      <div class="panel-head">
        <h2>Addressable Test (A1–A8)</h2>
        <span class="head-hint">Inspect physical addressable outputs and color channels</span>
      </div>

      <div class="lanes-list">
        {#each LANES as lane}
          <div class="lane-row">
            <span class="lane-tag">A{lane + 1}</span>
            <input
              type="color"
              value={laneHex(lane)}
              onchange={e => setLaneColor(lane, (e.target as HTMLInputElement).value)}
              title="Select lane color"
            />
            <span class="lane-rgb mono">
              RGB({laneColors[lane].r}, {laneColors[lane].g}, {laneColors[lane].b})
            </span>
            <button
              class="btn-send-lane"
              onclick={() => sendLane(lane)}
              type="button"
            >
              Test A{lane + 1}
            </button>
          </div>
        {/each}
      </div>
    </section>

    <!-- SECTION 5: PWM TEST (Channels 0–7) -->
    <section class="panel">
      <div class="panel-head">
        <h2>PWM Test (P1–P8)</h2>
        <div class="pwm-quick-actions">
          <button
            class="ghost-sm"
            onclick={() => sendEffect('pwm_walk')}
            type="button"
          >
            Run PWM Walk
          </button>
          <button
            class="ghost-sm"
            onclick={() => sendEffect('pwm_fade')}
            type="button"
          >
            Run PWM Fade
          </button>
        </div>
      </div>

      <div class="pwm-list">
        {#each PWM_CHANNELS as ch}
          <div class="pwm-row">
            <span class="pwm-tag">P{ch + 1}</span>
            <input
              type="range"
              min="0"
              max="4095"
              bind:value={pwmDuties[ch]}
            />
            <span class="pwm-val mono">{pwmDuties[ch]} / 4095</span>
            <span class="pwm-pct mono">{((pwmDuties[ch] / 4095) * 100).toFixed(0)}%</span>
            <button
              class="btn-send-pwm"
              onclick={() => sendPwm(ch)}
              type="button"
            >
              Set P{ch + 1}
            </button>
          </div>
        {/each}
      </div>
    </section>

    <!-- SECTION 6: AUTOMATED BURN-IN -->
    <section class="panel burnin-panel">
      <div class="panel-head">
        <h2>Automated Burn-In & Stress Testing</h2>
        <span class="caution-badge">High Output & Thermal Load</span>
      </div>

      <div class="burnin-content">
        <p class="burnin-text">
          Continuous endurance test exercising all addressable and PWM outputs at dynamic high load. Ensure device has proper heatsinking and ventilation during extended burn-in runs.
        </p>

        <div class="burnin-actions">
          <button
            class="btn-burnin"
            class:active={status?.effect === 'burn_in'}
            onclick={() => sendEffect('burn_in')}
            type="button"
          >
            <span class="burn-icon">⬥</span>
            <span>{status?.effect === 'burn_in' ? 'Burn-In Active' : 'Start Burn-In Test'}</span>
          </button>

          <button
            class="btn-autoloop large"
            class:on={status?.autoLoop}
            onclick={toggleAutoLoop}
            type="button"
          >
            Toggle Continuous Auto-Loop
          </button>
        </div>
      </div>
    </section>
  {/if}
</div>

<style>
  .factory-root {
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    color: #f5f5f7;
    max-width: 1180px;
    margin: 0 auto;
    width: 100%;
  }

  .factory-header {
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
    -webkit-backdrop-filter: blur(22px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
    padding: 0.85rem 1.25rem;
  }
  .factory-header h1 {
    margin: 0;
    font-size: 1.25rem;
    font-weight: 700;
  }
  .sub {
    margin: 0.25rem 0 0;
    font-size: 0.8rem;
    color: #86868b;
  }
  .ip-mono {
    font-family: monospace;
    color: var(--dash-led);
  }
  .header-right {
    display: flex;
    gap: 0.5rem;
  }
  .ghost-btn {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    padding: 0.4rem 0.85rem;
    font-size: 0.8rem;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .ghost-btn:hover {
    background: rgba(255, 255, 255, 0.1);
  }

  .error-banner {
    background: rgba(255, 69, 58, 0.2);
    border: 1px solid rgba(255, 69, 58, 0.4);
    color: #ff453a;
    padding: 0.6rem 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }
  .flash-banner {
    background: rgba(48, 209, 88, 0.18);
    border: 1px solid rgba(48, 209, 88, 0.4);
    color: #30d158;
    padding: 0.6rem 1rem;
    border-radius: 8px;
    font-size: 0.85rem;
  }

  .loading-panel, .empty-panel {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    min-height: 300px;
    color: #86868b;
  }
  .spinner {
    width: 32px;
    height: 32px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }

  /* Emergency Abort Banner */
  .abort-banner {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: rgba(255, 59, 48, 0.12);
    border: 1px solid rgba(255, 59, 48, 0.35);
    border-radius: 14px;
    padding: 1rem 1.25rem;
    gap: 1.5rem;
    flex-wrap: wrap;
  }
  .abort-info {
    display: flex;
    align-items: center;
    gap: 1rem;
  }
  .abort-icon {
    font-size: 1.8rem;
  }
  .abort-title {
    margin: 0;
    font-size: 1rem;
    font-weight: 700;
    color: #ff453a;
  }
  .abort-desc {
    margin: 0.2rem 0 0;
    font-size: 0.8rem;
    color: #ddd;
  }
  .abort-btn {
    background: #ff3b30;
    color: #fff;
    border: none;
    border-radius: 10px;
    padding: 0.65rem 1.4rem;
    font-size: 0.9rem;
    font-weight: 700;
    cursor: pointer;
    box-shadow: 0 4px 16px rgba(255, 59, 48, 0.4);
    transition: all 0.15s ease;
    white-space: nowrap;
  }
  .abort-btn:hover:not(:disabled) {
    background: #ff453a;
    transform: scale(1.02);
    box-shadow: 0 6px 20px rgba(255, 59, 48, 0.6);
  }
  .abort-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  /* Panels */
  .panel {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .panel-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    padding-bottom: 0.6rem;
  }
  .panel-head h2 {
    margin: 0;
    font-size: 1.05rem;
    font-weight: 600;
  }
  .head-hint {
    font-size: 0.75rem;
    color: #86868b;
  }

  /* Status Grid */
  .status-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 0.75rem;
  }
  .stat-card {
    background: linear-gradient(180deg, rgba(255,255,255,.035), rgba(255,255,255,.018));
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 10px;
    padding: 0.75rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .stat-label {
    font-size: 0.7rem;
    color: #86868b;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    font-weight: 600;
  }
  .stat-val {
    font-size: 1.05rem;
    font-weight: 700;
  }
  .stat-val.active { color: #ff9f0a; text-transform: capitalize; }
  .stat-val.ok { color: #30d158; }
  .stat-val.muted { color: #86868b; }
  .stat-val.err { color: #ff453a; }
  .mono { font-family: monospace; }

  .status-live-pill {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    background: rgba(255, 159, 10, 0.15);
    border: 1px solid rgba(255, 159, 10, 0.4);
    color: #ff9f0a;
    padding: 0.2rem 0.6rem;
    border-radius: 6px;
    font-size: 0.72rem;
    font-weight: 700;
  }
  .status-idle-pill {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #86868b;
    padding: 0.2rem 0.6rem;
    border-radius: 6px;
    font-size: 0.72rem;
    font-weight: 700;
  }
  .pulse-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #ff9f0a;
    animation: pulse 1.2s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.3; transform: scale(0.8); }
  }

  /* Params Bar */
  .params-bar {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    padding: 0.85rem 1.25rem;
    flex-wrap: wrap;
  }
  .param-group {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    min-width: 180px;
  }
  .param-label {
    font-size: 0.72rem;
    color: #86868b;
    font-weight: 600;
    text-transform: uppercase;
  }
  .param-slider-wrap, .param-input-wrap {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .param-slider-wrap input {
    accent-color: var(--dash-accent);
    flex: 1;
  }
  .param-val {
    font-family: monospace;
    font-size: 0.85rem;
    font-weight: 700;
    min-width: 32px;
  }
  .param-input-wrap input {
    width: 100px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #f5f5f7;
    padding: 0.3rem 0.5rem;
    font-family: monospace;
    font-size: 0.85rem;
  }
  .param-unit {
    font-size: 0.75rem;
    color: #86868b;
  }
  .param-actions {
    margin-left: auto;
  }
  .btn-autoloop {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 8px;
    color: #f5f5f7;
    padding: 0.45rem 0.9rem;
    font-size: 0.8rem;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-autoloop.on {
    background: rgba(48, 209, 88, 0.2);
    border-color: rgba(48, 209, 88, 0.4);
    color: #30d158;
    font-weight: 700;
  }

  /* Quick Effects Grid */
  .effects-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 0.75rem;
  }
  .fx-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 10px;
    padding: 0.85rem 1rem;
    cursor: pointer;
    text-align: left;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    transition: all 0.15s ease;
  }
  .fx-card:hover {
    background: rgba(255, 255, 255, 0.07);
    border-color: rgba(var(--dash-accent-rgb), 0.4);
    transform: translateY(-2px);
  }
  .fx-card.active {
    border-color: #ff9f0a;
    background: rgba(255, 159, 10, 0.12);
    box-shadow: 0 0 12px rgba(255, 159, 10, 0.3);
  }
  .fx-top {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .fx-icon {
    font-size: 1.1rem;
  }
  .fx-name {
    font-size: 0.85rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .fx-desc {
    margin: 0;
    font-size: 0.72rem;
    color: #86868b;
    line-height: 1.3;
  }

  /* Pixel Lanes */
  .lanes-list {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .lane-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.05);
    border-radius: 8px;
    padding: 0.4rem 0.75rem;
  }
  .lane-tag {
    font-size: 0.8rem;
    font-weight: 800;
    color: var(--dash-accent);
    min-width: 60px;
  }
  .lane-row input[type='color'] {
    width: 32px;
    height: 26px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    border-radius: 4px;
    padding: 0;
    cursor: pointer;
    background: transparent;
  }
  .lane-rgb {
    font-size: 0.75rem;
    color: #a1a1a6;
    flex: 1;
  }
  .btn-send-lane, .btn-send-pwm {
    border-radius: 7px;
    font-weight: 700;
    font-size: 0.75rem;
    padding: 0.3rem 0.7rem;
    cursor: pointer;
    transition: background .12s ease, border-color .12s ease, color .12s ease;
  }
  .btn-send-lane {
    background: rgba(var(--dash-accent-rgb),.14);
    border: 1px solid rgba(var(--dash-accent-rgb),.32);
    color: var(--dash-accent);
  }
  .btn-send-pwm {
    background: color-mix(in oklab, var(--dash-warm) 14%, transparent);
    border: 1px solid color-mix(in oklab, var(--dash-warm) 32%, transparent);
    color: var(--dash-warm);
  }
  .btn-send-lane:hover { background: rgba(var(--dash-accent-rgb),.24); }
  .btn-send-pwm:hover { background: color-mix(in oklab, var(--dash-warm) 24%, transparent); }

  /* PWM Channels */
  .pwm-quick-actions {
    display: flex;
    gap: 0.4rem;
  }
  .ghost-sm {
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.72rem;
    padding: 0.25rem 0.55rem;
    cursor: pointer;
  }
  .pwm-list {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .pwm-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.05);
    border-radius: 8px;
    padding: 0.4rem 0.75rem;
  }
  .pwm-tag {
    font-size: 0.8rem;
    font-weight: 800;
    color: var(--dash-warm);
    min-width: 50px;
  }
  .pwm-row input[type='range'] {
    flex: 1;
    accent-color: #ff9f0a;
  }
  .pwm-val {
    font-size: 0.75rem;
    color: #ddd;
    min-width: 80px;
    text-align: right;
  }
  .pwm-pct {
    font-size: 0.75rem;
    color: #ff9f0a;
    min-width: 40px;
    text-align: right;
  }

  /* Burn-In Panel */
  .burnin-panel {
    background: rgba(255, 159, 10, 0.03);
    border-color: rgba(255, 159, 10, 0.2);
  }
  .caution-badge {
    background: rgba(255, 159, 10, 0.15);
    border: 1px solid rgba(255, 159, 10, 0.35);
    color: #ff9f0a;
    padding: 0.2rem 0.6rem;
    border-radius: 6px;
    font-size: 0.7rem;
    font-weight: 700;
  }
  .burnin-content {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .burnin-text {
    margin: 0;
    font-size: 0.82rem;
    color: #a1a1a6;
    line-height: 1.45;
  }
  .burnin-actions {
    display: flex;
    gap: 0.75rem;
    flex-wrap: wrap;
  }
  .btn-burnin {
    background: #ff9f0a;
    color: #000;
    border: none;
    border-radius: 8px;
    padding: 0.55rem 1.25rem;
    font-size: 0.85rem;
    font-weight: 700;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    cursor: pointer;
    box-shadow: 0 4px 14px rgba(255, 159, 10, 0.35);
    transition: all 0.15s ease;
  }
  .btn-burnin:hover {
    background: #ffaa22;
    transform: translateY(-1px);
  }
  .btn-burnin.active {
    background: #ff3b30;
    color: #fff;
    box-shadow: 0 0 18px rgba(255, 59, 48, 0.6);
  }
  .btn-autoloop.large {
    padding: 0.55rem 1.25rem;
    font-size: 0.85rem;
  }

  @media (max-width: 768px) {
    .factory-header { position: static; align-items: flex-start; }
    .header-right { width: 100%; }
    .header-right button { flex: 1; }
    .abort-banner { align-items: stretch; }
    .abort-btn { width: 100%; }
    .params-bar { gap: 1rem; }
    .param-group { min-width: 100%; }
    .param-actions { margin-left: 0; width: 100%; }
    .param-actions button { width: 100%; }
    .effects-grid { grid-template-columns: 1fr; }
  }

</style>
