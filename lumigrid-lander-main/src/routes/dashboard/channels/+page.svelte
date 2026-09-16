<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { getChannelConfig, setPixels, getState, setBrightness, setPower } from '$lib/api/node';
  import type { ChannelConfig } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';

  const CHANNEL_TYPES = ['Off', 'PWM (Dumb)', 'CCT (2ch)', 'RGB (3ch)', 'RGBW (4ch)'];
  let nodeIp = $state('');
  let config = $state<ChannelConfig | null>(null);
  let loading = $state(true);
  let error = $state('');
  let channelValues = $state<number[]>(Array(8).fill(128));
  let masterBri = $state(255);
  let masterOn = $state(true);
  let dirty = $state(false);
  let sending = $state(false);
  let lastSent = $state(0);
  let channelRGB = $state<{r:number;g:number;b:number}[]>(Array(8).fill(null).map(() => ({ r: 255, g: 255, b: 255 })));

  async function loadConfig() {
    loading = true; error = '';
    try { config = await getChannelConfig(nodeIp); const st = await getState(nodeIp); masterBri = st.bri; masterOn = st.on; }
    catch (e) { error = String(e); }
    loading = false;
  }
  onMount(() => { nodeIp = getSelectedNodeIp(); if (nodeIp) loadConfig(); else loading = false; });
  async function sendChannels() {
    if (sending || Date.now() - lastSent < 66) return;
    sending = true; lastSent = Date.now(); dirty = false;
    try {
      const pixels: Array<[number, [number, number, number, number]]> = [];
      if (!config) for (let i = 0; i < 8; i++) { const v = channelValues[i]; pixels.push([i, [v, v, v, v]]); }
      else { let pixelIdx = 0; let ch = 0; while (ch < 8) { const type = config.channelType[ch] ?? 1; const v = channelValues[ch]; const rgb = channelRGB[ch]; switch (type) {
        case 0: pixels.push([pixelIdx, [0,0,0,0]]); ch++; pixelIdx++; break;
        case 1: pixels.push([pixelIdx, [v,v,v,v]]); ch++; pixelIdx++; break;
        case 2: { const warm = channelValues[ch] ?? 128; const cool = ch + 1 < 8 ? (channelValues[ch + 1] ?? 128) : 0; pixels.push([pixelIdx, [warm,cool,0,0]]); ch += 2; pixelIdx++; break; }
        case 3: pixels.push([pixelIdx, [rgb.r,rgb.g,rgb.b,0]]); ch += 3; pixelIdx++; break;
        case 4: pixels.push([pixelIdx, [rgb.r,rgb.g,rgb.b,v]]); ch += 4; pixelIdx++; break;
        default: ch++; pixelIdx++; break;
      } } }
      await setPixels(nodeIp, pixels);
    } catch (e) { error = String(e); }
    sending = false; if (dirty) sendChannels();
  }
  function onSliderChange() { dirty = true; sendChannels(); }
  function channelLabel(ch: number) { return config?.channelLabel?.[ch] ?? `LED ${ch}`; }
  function channelType(ch: number) { return config?.channelType?.[ch] ?? 1; }
  function isPrimary(ch: number) { let c = 0; while (c < ch) { const t = channelType(c); c += t === 2 ? 2 : t === 3 ? 3 : t === 4 ? 4 : 1; } return c === ch; }
</script>

<svelte:head><title>Channel Control — LumiGrid</title></svelte:head>
<div class="page">
  <header class="page-header"><div><h1>PWM Channel Control</h1><p class="sub">{nodeIp ? `Node: ${nodeIp}` : 'No node selected'} · {config ? `${config.pwmFrequency} Hz` : ''}</p></div><div class="head-actions"><button class="action-btn" onclick={() => setPower(nodeIp, !masterOn).then(loadConfig)}>{masterOn ? '⏻ Off' : '⏻ On'}</button><button class="action-btn" onclick={loadConfig}>↺</button></div></header>
  {#if !nodeIp}<div class="empty">Select a node in the sidebar to control its PWM channels.</div>
  {:else if loading}<div class="loading"><div class="spinner"></div>Loading…</div>
  {:else if error}<div class="err-box">{error}</div>
  {:else}
    <div class="master-card"><div class="master-label"><span>Master Brightness</span><span class="master-val">{masterBri}</span></div><input type="range" min="0" max="255" bind:value={masterBri} class="master-range" oninput={() => setBrightness(nodeIp, masterBri)} /></div>
    <div class="channel-grid">{#each {length: 8} as _, ch}{@const type = channelType(ch)}{@const primary = isPrimary(ch)}
      <div class="channel-card" class:secondary={!primary} class:off-type={type === 0}><div class="ch-header"><span class="ch-index">CH{ch}</span><span class="ch-name">{channelLabel(ch)}</span><span class="ch-type-badge">{CHANNEL_TYPES[type] ?? 'Unknown'}</span></div>
      {#if type === 0}<div class="ch-off">Channel disabled</div>
      {:else if type === 1 || type === 2}<div class="slider-row"><div class="slider-track" style:background={`linear-gradient(to right, #ff2d55 ${channelValues[ch]/255*100}%, rgba(255,255,255,.08) 0)`}><input type="range" min="0" max="255" bind:value={channelValues[ch]} oninput={onSliderChange} class="ch-range" /></div><span class="ch-val">{channelValues[ch]}</span></div>
      {:else if type === 3 || type === 4}<div class="rgb-row">{#each ['r','g','b'] as c}<div class="rgb-col"><label class="rgb-label" for={`rgb-${ch}-${c}`}>{c.toUpperCase()}</label><input id={`rgb-${ch}-${c}`} type="range" min="0" max="255" bind:value={channelRGB[ch][c as 'r'|'g'|'b']} oninput={onSliderChange} class="rgb-range" style:accent-color={c === 'r' ? '#ff453a' : c === 'g' ? '#34c759' : '#0a84ff'} /><span class="rgb-val">{channelRGB[ch][c as 'r'|'g'|'b']}</span></div>{/each}{#if type === 4}<div class="rgb-col"><label class="rgb-label" for={`rgb-${ch}-w`}>W</label><input id={`rgb-${ch}-w`} type="range" min="0" max="255" bind:value={channelValues[ch]} oninput={onSliderChange} class="rgb-range" style:accent-color="#fff" /><span class="rgb-val">{channelValues[ch]}</span></div>{/if}</div><div class="swatch" style:background={`rgb(${channelRGB[ch].r},${channelRGB[ch].g},${channelRGB[ch].b})`}></div>{/if}</div>
    {/each}</div>
  {/if}
</div>

<style>
  .page{max-width:900px}.page-header{display:flex;align-items:flex-start;justify-content:space-between;margin-bottom:1.5rem;gap:1rem;flex-wrap:wrap}h1{margin:0;font-size:1.6rem;font-weight:700;color:#f5f5f7}.sub{margin:.25rem 0 0;color:#a1a1a6;font-size:.85rem}.head-actions{display:flex;gap:.5rem}.action-btn{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.12);border-radius:999px;color:#f5f5f7;padding:.5rem 1rem;cursor:pointer;font-size:.85rem}.action-btn:hover{background:rgba(255,255,255,.14)}.loading{display:flex;align-items:center;gap:.75rem;color:#a1a1a6;padding:3rem;justify-content:center}.spinner{width:18px;height:18px;border:2px solid rgba(255,255,255,.1);border-top-color:#ff2d55;border-radius:50%;animation:spin .8s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}.empty{color:#a1a1a6;padding:3rem;text-align:center}.err-box{background:rgba(255,69,58,.12);border:1px solid rgba(255,69,58,.3);border-radius:12px;color:#ff453a;padding:1rem;font-size:.85rem;font-family:monospace}.master-card{background:rgba(29,29,31,.72);border:1px solid rgba(255,255,255,.08);border-radius:16px;padding:1rem 1.25rem;margin-bottom:1.25rem}.master-label{display:flex;justify-content:space-between;margin-bottom:.5rem;color:#f5f5f7;font-weight:600;font-size:.9rem}.master-val{color:#a1a1a6;font-family:monospace}.master-range{width:100%;accent-color:#ff2d55;height:4px}.channel-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:1rem}.channel-card{background:rgba(29,29,31,.72);border:1px solid rgba(255,255,255,.08);border-radius:16px;padding:1rem;display:flex;flex-direction:column;gap:.6rem}.channel-card.off-type{opacity:.45}.ch-header{display:flex;align-items:center;gap:.4rem;flex-wrap:wrap}.ch-index{font-size:.65rem;font-weight:700;color:#a1a1a6;background:rgba(255,255,255,.06);padding:.1rem .35rem;border-radius:4px;font-family:monospace}.ch-name{flex:1;font-size:.85rem;font-weight:600;color:#f5f5f7;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.ch-type-badge{font-size:.65rem;color:#ff9500;background:rgba(255,149,0,.12);padding:.1rem .4rem;border-radius:999px;white-space:nowrap}.ch-off{color:#a1a1a6;font-size:.78rem;text-align:center;padding:.5rem 0}.slider-row{display:flex;align-items:center;gap:.5rem}.slider-track{flex:1;border-radius:999px;height:6px;position:relative}.ch-range{width:100%;accent-color:#ff2d55}.ch-val{min-width:2.5ch;font-family:monospace;font-size:.8rem;color:#f5f5f7;text-align:right}.rgb-row{display:flex;flex-direction:column;gap:.3rem}.rgb-col{display:flex;align-items:center;gap:.4rem}.rgb-label{width:1.2rem;font-size:.72rem;font-weight:700;color:#a1a1a6;text-align:right}.rgb-range{flex:1;height:4px}.rgb-val{min-width:2.5ch;font-family:monospace;font-size:.72rem;color:#a1a1a6}.swatch{width:100%;height:24px;border-radius:8px;border:1px solid rgba(255,255,255,.1)}
</style>
