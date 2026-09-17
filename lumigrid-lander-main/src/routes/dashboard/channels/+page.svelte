<script lang="ts">
  import { onMount } from 'svelte';
  import { getDeviceConfig } from '$lib/api/config';
  import { postFactoryPwm } from '$lib/api/factory';
  import type { DeviceConfig, FixtureConfig, PhysicalOutput } from '$lib/api/config';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  // Profile constants mirroring firmware FixtureProfile enum
  const PROFILE_NAMES: Record<number, string> = {
    0: 'WS2812',
    1: 'SK6812',
    2: 'PWM Single',
    3: 'PWM RGB',
    4: 'PWM RGBW',
  };
  const PWM_PROFILES = new Set([2, 3, 4]);

  interface PwmChannel {
    fixtureIdx: number;
    fixtureLabel: string;
    profile: number;
    channelLabel: string;
    pca9685Channel: number;
    duty: number; // 0–4095
  }

  let nodeIp = $state('');
  let config = $state<DeviceConfig | null>(null);
  let loading = $state(true);
  let error = $state('');
  let toast = $state('');
  let sending = $state(false);
  let loadGen = 0;

  // Derived list of all controllable PWM channels from the firmware topology
  const pwmChannels = $derived.by<PwmChannel[]>(() => {
    if (!config?.fixtures || !config?.physicalOutputs) return [];
    const channels: PwmChannel[] = [];
    config.fixtures.forEach((fix, fIdx) => {
      if (!PWM_PROFILES.has(fix.profile)) return;
      const numCh = fix.profile === 2 ? 1 : fix.profile === 3 ? 3 : 4;
      const chLabels = fix.profile === 2 ? ['W'] : fix.profile === 3 ? ['R', 'G', 'B'] : ['R', 'G', 'B', 'W'];
      for (let ch = 0; ch < numCh; ch++) {
        const pIdx = fix.physicalOutputIndex[ch];
        const phys: PhysicalOutput | undefined = config!.physicalOutputs![pIdx];
        if (!phys || !phys.enabled) continue;
        channels.push({
          fixtureIdx: fIdx,
          fixtureLabel: `Fixture ${fIdx}`,
          profile: fix.profile,
          channelLabel: chLabels[ch],
          pca9685Channel: phys.hardwareId,
          duty: 0,
        });
      }
    });
    return channels;
  });

  const addressableFixtures = $derived.by(() => {
    if (!config?.fixtures) return [];
    return config.fixtures
      .map((f, i) => ({ ...f, index: i }))
      .filter(f => !PWM_PROFILES.has(f.profile) && f.numPixels > 0);
  });

  async function loadConfig(targetIp = nodeIp, gen = loadGen) {
    if (!targetIp) {
      config = null;
      loading = false;
      return;
    }
    loading = true;
    error = '';
    try {
      const cfg = await getDeviceConfig(targetIp);
      if (gen !== loadGen) return;
      config = cfg;
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) loading = false;
    }
  }

  async function sendPwm(ch: PwmChannel) {
    if (sending || !nodeIp) return;
    sending = true;
    try {
      await postFactoryPwm(nodeIp, ch.pca9685Channel, ch.duty);
    } catch (e) {
      error = String(e);
    } finally {
      sending = false;
    }
  }

  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => { if (toast === msg) toast = ''; }, 2500);
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        config = null;
        loading = !!nodeIp;
        error = '';
        if (nodeIp) loadConfig(nodeIp, loadGen);
      }
    });
    return unsub;
  });
</script>

<svelte:head><title>Channels — LumiGrid</title></svelte:head>

<div class="page">
  {#if toast}
    <div class="toast">{toast}</div>
  {/if}

  <header class="page-header">
    <div>
      <h1>Output Channels</h1>
      <p class="sub">
        {nodeIp ? `Node: ${nodeIp}` : 'No node selected'}
        {#if config?.physicalOutputs}
          · {config.physicalOutputs.filter(p => p.enabled).length} physical outputs configured
        {/if}
      </p>
    </div>
    <button class="action-btn" onclick={() => loadConfig()} disabled={loading}>↺ Refresh</button>
  </header>

  {#if !nodeIp}
    <div class="empty">Select a node in the sidebar to view its output channels.</div>

  {:else if loading}
    <div class="loading"><div class="spinner"></div>Loading topology…</div>

  {:else if error}
    <div class="err-box">
      <strong>Failed to load config:</strong> {error}
      <br><small>Ensure the node is reachable and at firmware v1.0+</small>
    </div>

  {:else if !config}
    <div class="empty">No configuration returned from node.</div>

  {:else}
    <!-- PWM Channels -->
    <section class="section-card">
      <h2 class="section-title">
        <span class="icon">⚡</span> PWM Channels (PCA9685)
        <span class="count-pill">{pwmChannels.length} channels</span>
      </h2>

      {#if pwmChannels.length === 0}
        <div class="empty-section">
          No PWM fixtures configured.
          {#if !config.physicalOutputs || config.physicalOutputs.length === 0}
            The hardware topology has not been set up — configure fixtures in the device settings first.
          {/if}
        </div>
      {:else}
        <div class="channel-grid">
          {#each pwmChannels as ch, i (ch.pca9685Channel + '_' + ch.channelLabel)}
            <div class="channel-card pwm">
              <div class="ch-header">
                <span class="ch-index">PCA9685 ch{ch.pca9685Channel}</span>
                <span class="ch-name">{ch.fixtureLabel} · {ch.channelLabel}</span>
                <span class="ch-badge">{PROFILE_NAMES[ch.profile]}</span>
              </div>
              <div class="slider-row">
                <div class="slider-track">
                  <input
                    type="range"
                    min="0"
                    max="4095"
                    bind:value={pwmChannels[i].duty}
                    oninput={() => sendPwm(ch)}
                    class="ch-range"
                  />
                </div>
                <span class="ch-val">{ch.duty}</span>
              </div>
              <div class="duty-bar" style:width={`${(ch.duty / 4095) * 100}%`}></div>
            </div>
          {/each}
        </div>
      {/if}
    </section>

    <!-- Addressable Fixtures (read-only info) -->
    <section class="section-card">
      <h2 class="section-title">
        <span class="icon">💡</span> Addressable LED Fixtures (RMT)
        <span class="count-pill">{addressableFixtures.length} fixtures</span>
      </h2>

      {#if addressableFixtures.length === 0}
        <div class="empty-section">No addressable fixtures configured.</div>
      {:else}
        <div class="fixture-list">
          {#each addressableFixtures as fix}
            <div class="fixture-row">
              <span class="fix-idx">Fixture {fix.index}</span>
              <span class="fix-profile">{PROFILE_NAMES[fix.profile] ?? 'Unknown'}</span>
              <span class="fix-pixels">{fix.numPixels} px</span>
              <span class="fix-zone">Zone {fix.zoneId}</span>
              <span class="fix-phys">RMT lane {config!.physicalOutputs?.[fix.physicalOutputIndex[0]]?.hardwareId ?? '?'}</span>
            </div>
          {/each}
        </div>
      {/if}
    </section>
  {/if}
</div>

<style>
  .page { max-width: 920px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .action-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 999px; color: #f5f5f7; padding: .5rem 1rem; cursor: pointer; font-size: .85rem; }
  .action-btn:hover { background: rgba(255,255,255,.14); }
  .action-btn:disabled { opacity: .5; cursor: default; }

  .toast { position: fixed; top: 1.5rem; right: 2rem; background: rgba(29,29,31,.95); border: 1px solid rgba(0,113,227,.4); color: #f5f5f7; padding: .75rem 1.25rem; border-radius: 12px; font-size: .88rem; z-index: 1000; }

  .loading { display: flex; align-items: center; gap: .75rem; color: #a1a1a6; padding: 3rem; justify-content: center; }
  .spinner { width: 18px; height: 18px; border: 2px solid rgba(255,255,255,.1); border-top-color: #ff2d55; border-radius: 50%; animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .empty { color: #a1a1a6; padding: 3rem; text-align: center; }
  .err-box { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3); border-radius: 12px; color: #ff453a; padding: 1rem 1.25rem; font-size: .85rem; line-height: 1.6; }

  .section-card { background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 16px; padding: 1.25rem 1.5rem; margin-bottom: 1.25rem; }
  .section-title { display: flex; align-items: center; gap: .5rem; margin: 0 0 1.25rem; font-size: 1.05rem; font-weight: 600; color: #f5f5f7; }
  .icon { font-size: 1.1rem; }
  .count-pill { margin-left: auto; font-size: .72rem; font-weight: 500; color: #a1a1a6; background: rgba(255,255,255,.07); padding: .15rem .55rem; border-radius: 999px; }
  .empty-section { color: #a1a1a6; font-size: .9rem; padding: 1rem 0; }

  .channel-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 1rem; }
  .channel-card { background: rgba(255,255,255,.03); border: 1px solid rgba(255,255,255,.07); border-radius: 12px; padding: 1rem; display: flex; flex-direction: column; gap: .6rem; position: relative; overflow: hidden; }
  .channel-card.pwm { border-color: rgba(255,149,0,.18); }
  .ch-header { display: flex; align-items: center; gap: .4rem; flex-wrap: wrap; }
  .ch-index { font-size: .65rem; font-weight: 700; color: #a1a1a6; background: rgba(255,255,255,.06); padding: .1rem .35rem; border-radius: 4px; font-family: monospace; }
  .ch-name { flex: 1; font-size: .85rem; font-weight: 600; color: #f5f5f7; }
  .ch-badge { font-size: .65rem; color: #ff9500; background: rgba(255,149,0,.12); padding: .1rem .4rem; border-radius: 999px; white-space: nowrap; }
  .slider-row { display: flex; align-items: center; gap: .5rem; }
  .slider-track { flex: 1; }
  .ch-range { width: 100%; accent-color: #ff9500; }
  .ch-val { min-width: 3ch; font-family: monospace; font-size: .8rem; color: #f5f5f7; text-align: right; }
  .duty-bar { position: absolute; bottom: 0; left: 0; height: 3px; background: linear-gradient(90deg, #ff9500, #ff6b00); border-radius: 0 0 12px 12px; transition: width .1s; }

  .fixture-list { display: flex; flex-direction: column; gap: .5rem; }
  .fixture-row { display: flex; align-items: center; gap: .75rem; padding: .6rem .9rem; background: rgba(255,255,255,.03); border-radius: 8px; font-size: .85rem; flex-wrap: wrap; }
  .fix-idx { font-family: monospace; font-size: .72rem; color: #a1a1a6; min-width: 4.5rem; }
  .fix-profile { color: #64d2ff; font-weight: 600; }
  .fix-pixels { color: #a1a1a6; min-width: 4rem; }
  .fix-zone { color: #a1a1a6; }
  .fix-phys { margin-left: auto; font-family: monospace; font-size: .72rem; color: #a1a1a6; }
</style>
