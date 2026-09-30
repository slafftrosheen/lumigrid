<script lang="ts">
  import { onMount } from 'svelte';
  import { getNodeInfo, type PublicNodeInfo } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  // ── Feature 2: Thermal Dashboard (LG-031 Canonical Handling) ───────────────
  // Per LG-031 Requirement 25:
  // "There is no clear canonical firmware temperature sensor path in current runtime.
  // Do not keep pretending /json/info provides reliable LumiGrid thermal telemetry.
  // 'Temperature sensor unavailable on this hardware/configuration' is the correct UI."

  let nodeIp = $state('');
  let nodeInfo = $state<PublicNodeInfo | null>(null);

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        if (nodeIp) {
          getNodeInfo(nodeIp).then(info => { nodeInfo = info; }).catch(() => {});
        } else {
          nodeInfo = null;
        }
      }
    });
    return unsub;
  });
</script>

<svelte:head>
  <title>Thermal Monitor — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Thermal Monitor</h1>
      <p class="sub">{nodeIp || 'No node selected'}</p>
    </div>
  </header>

  {#if !nodeIp}
    <div class="empty">Select a node to inspect thermal telemetry.</div>
  {:else}
    <div class="dash-grid">
      <div class="thermal-card gauge-card">
        <h2>Hardware Temperature Sensor</h2>
        <div class="gauge-placeholder">
          <span class="icon">❄️</span>
          <p class="sensor-msg">Temperature sensor unavailable on this hardware/configuration</p>
        </div>
      </div>

      <div class="thermal-card info-card">
        <h2>Hardware & Identity</h2>
        {#if nodeInfo}
          <dl class="info-dl">
            <dt>Device ID</dt><dd class="mono">{nodeInfo.id}</dd>
            <dt>Device Name</dt><dd>{nodeInfo.name}</dd>
            <dt>Model</dt><dd>{nodeInfo.model} ({nodeInfo.family})</dd>
            <dt>Firmware</dt><dd>v{nodeInfo.firmwareVersion}</dd>
            <dt>Hardware Rev</dt><dd>{nodeInfo.hardwareRevision}</dd>
          </dl>
        {:else}
          <div class="empty-spark">Querying node identity…</div>
        {/if}
      </div>
    </div>
  {/if}
</div>

<style>
  .page { width:100%; max-width: 980px; margin:0 auto; }
  .page-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    margin-bottom: 1.25rem;
    gap: 1rem;
    flex-wrap: wrap;
    padding: .85rem 1rem;
    background: rgba(22,22,24,.86);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
  }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .empty { color: #a1a1a6; padding: 3rem; text-align: center; }
  .dash-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 1rem; }
  @media (max-width: 600px) { .dash-grid { grid-template-columns: 1fr; } }
  .thermal-card {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
    box-shadow: var(--dash-shadow);
  }
  h2 { margin: 0 0 1rem; font-size: .95rem; font-weight: 600; color: #f5f5f7; }
  .gauge-placeholder {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 2.5rem 1rem;
    gap: 1rem;
  }
  .icon { font-size: 2.5rem; opacity: 0.6; }
  .sensor-msg {
    color: #a1a1a6;
    font-size: 0.9rem;
    max-width: 320px;
    line-height: 1.4;
    margin: 0;
  }
  .info-dl { display: grid; grid-template-columns: auto 1fr; gap: .4rem .75rem; margin: 0; }
  dt { font-size: .75rem; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; align-self: center; }
  dd { margin: 0; font-size: .85rem; color: #f5f5f7; font-weight: 600; }
  .mono { font-family: monospace; }
  .empty-spark { color: #a1a1a6; font-size: .85rem; text-align: center; padding: 1.5rem 0; }
</style>
