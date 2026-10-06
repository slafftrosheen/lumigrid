<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    BUF_SIZE,
    LANES,
    PIXELS_PER_LANE,
    connect,
    getNodeLiveSnapshot,
    nodePixelStore,
    nodeStore,
    reconnect as reconnectNode,
  } from '$lib/state/nodeStore';

  type ViewMode = 'lanes' | 'channels' | 'composite' | 'dmx';

  let nodeIp = $state('');
  let connected = $state(false);
  let viewMode = $state<ViewMode>('lanes');

  let pixelBuf: Uint8Array = new Uint8Array(BUF_SIZE);
  let dirty = $state(false);
  let canvasEl: HTMLCanvasElement | null = null;
  let rafId = 0;
  let unsubs: (() => void)[] = [];

  let statusInfo = $state({
    effect: '', brightness: 0, outputsEnabled: false,
    testActive: false, autoLoop: false,
  });

  let wsFps = $state(0);
  let lastFrameCount = 0;
  let lastFpsTime = 0;

  function render() {
    if (!canvasEl) { rafId = requestAnimationFrame(render); return; }
    const ctx = canvasEl.getContext('2d');
    if (!ctx) { rafId = requestAnimationFrame(render); return; }

    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
      const currentFrames = getNodeLiveSnapshot().frameCount;
      wsFps = Math.max(0, currentFrames - lastFrameCount);
      lastFrameCount = currentFrames;
      lastFpsTime = now;
    }

    if (dirty) {
      dirty = false;
      drawFrame(ctx, canvasEl.width, canvasEl.height);
    }

    rafId = requestAnimationFrame(render);
  }

  function drawFrame(ctx: CanvasRenderingContext2D, w: number, h: number) {
    ctx.fillStyle = '#0a0a0a';
    ctx.fillRect(0, 0, w, h);

    switch (viewMode) {
      case 'lanes': drawLanes(ctx, w, h); break;
      case 'channels': drawChannels(ctx, w, h); break;
      case 'composite': drawComposite(ctx, w, h); break;
      case 'dmx': drawDmx(ctx, w, h); break;
    }
  }

  function drawLanes(ctx: CanvasRenderingContext2D, w: number, h: number) {
    const laneH = Math.floor(h / LANES);
    const gap = 2;
    const pxW = Math.max(1, Math.floor((w - gap * (PIXELS_PER_LANE - 1)) / PIXELS_PER_LANE));

    for (let lane = 0; lane < LANES; lane++) {
      const y = lane * laneH + 1;
      for (let px = 0; px < PIXELS_PER_LANE; px++) {
        const off = (lane * PIXELS_PER_LANE + px) * 3;
        const r = pixelBuf[off] ?? 0;
        const g = pixelBuf[off + 1] ?? 0;
        const b = pixelBuf[off + 2] ?? 0;
        if (r === 0 && g === 0 && b === 0) continue;
        ctx.fillStyle = `rgb(${r},${g},${b})`;
        const x = px * (pxW + gap);
        ctx.fillRect(x, y, pxW, laneH - 2);
      }
    }
  }

  function drawChannels(ctx: CanvasRenderingContext2D, w: number, h: number) {
    // The pixel preview stream contains the eight physical addressable outputs.
    // Render them as A1..A8 tiles rather than inventing logical "zones".
    const cols = 4;
    const rows = 2;
    const gap = 10;
    const tileW = Math.floor((w - gap * (cols + 1)) / cols);
    const tileH = Math.floor((h - gap * (rows + 1)) / rows);

    ctx.font = '600 18px ui-monospace, SFMono-Regular, Menlo, monospace';
    ctx.textBaseline = 'top';

    for (let channel = 0; channel < LANES; channel++) {
      let sumR = 0;
      let sumG = 0;
      let sumB = 0;
      let lit = 0;

      for (let px = 0; px < PIXELS_PER_LANE; px++) {
        const off = (channel * PIXELS_PER_LANE + px) * 3;
        const r = pixelBuf[off] ?? 0;
        const g = pixelBuf[off + 1] ?? 0;
        const b = pixelBuf[off + 2] ?? 0;
        sumR += r;
        sumG += g;
        sumB += b;
        if (r || g || b) lit++;
      }

      const avgR = Math.round(sumR / PIXELS_PER_LANE);
      const avgG = Math.round(sumG / PIXELS_PER_LANE);
      const avgB = Math.round(sumB / PIXELS_PER_LANE);
      const col = channel % cols;
      const row = Math.floor(channel / cols);
      const x = gap + col * (tileW + gap);
      const y = gap + row * (tileH + gap);

      ctx.fillStyle = 'rgba(255,255,255,0.045)';
      ctx.fillRect(x, y, tileW, tileH);

      const previewH = Math.max(8, Math.floor(tileH * 0.56));
      ctx.fillStyle = `rgb(${avgR},${avgG},${avgB})`;
      ctx.fillRect(x + 8, y + 8, Math.max(1, tileW - 16), previewH);

      ctx.fillStyle = '#f5f5f7';
      ctx.fillText(`A${channel + 1}`, x + 10, y + previewH + 17);

      ctx.font = '12px ui-monospace, SFMono-Regular, Menlo, monospace';
      ctx.fillStyle = '#8e8e93';
      ctx.fillText(`${lit}/${PIXELS_PER_LANE} samples active`, x + 48, y + previewH + 20);
      ctx.font = '600 18px ui-monospace, SFMono-Regular, Menlo, monospace';
    }
  }

  function drawComposite(ctx: CanvasRenderingContext2D, w: number, h: number) {
    const cols = 16;
    const rows = Math.ceil((LANES * PIXELS_PER_LANE) / cols);
    const cellW = Math.max(2, Math.floor(w / cols));
    const cellH = Math.max(2, Math.floor(h / rows));
    let idx = 0;

    for (let lane = 0; lane < LANES; lane++) {
      for (let px = 0; px < PIXELS_PER_LANE; px++) {
        const off = (lane * PIXELS_PER_LANE + px) * 3;
        const r = pixelBuf[off] ?? 0;
        const g = pixelBuf[off + 1] ?? 0;
        const b = pixelBuf[off + 2] ?? 0;
        if (r === 0 && g === 0 && b === 0) { idx++; continue; }
        const col = idx % cols;
        const row = Math.floor(idx / cols);
        ctx.fillStyle = `rgb(${r},${g},${b})`;
        ctx.fillRect(col * cellW, row * cellH, cellW - 1, cellH - 1);
        idx++;
      }
    }
  }

  function drawDmx(ctx: CanvasRenderingContext2D, w: number, h: number) {
    const channels = 512;
    const cols = 32;
    const rows = Math.ceil(channels / cols);
    const cellW = Math.max(2, Math.floor(w / cols));
    const cellH = Math.max(2, Math.floor(h / rows));

    for (let ch = 0; ch < channels; ch++) {
      const srcIdx = ch % (LANES * PIXELS_PER_LANE);
      const off = srcIdx * 3;
      const val = pixelBuf[off] ?? 0;
      if (val === 0) continue;
      const col = ch % cols;
      const row = Math.floor(ch / cols);
      ctx.fillStyle = `rgb(${val},${Math.floor(val * 0.6)},${Math.floor(val * 0.3)})`;
      ctx.fillRect(col * cellW, row * cellH, cellW - 1, cellH - 1);
    }
  }

  onMount(() => {
    unsubs.push(
      nodeStore.subscribe(value => {
        nodeIp = value.nodeIp;
        connected = value.connection === 'connected';
        if (value.status) statusInfo = value.status;
      }),
      nodePixelStore.subscribe(buf => {
        pixelBuf = buf;
        dirty = true;
      }),
    );

    const snapshot = getNodeLiveSnapshot();
    lastFrameCount = snapshot.frameCount;
    lastFpsTime = performance.now();
    rafId = requestAnimationFrame(render);
    connect();
  });

  onDestroy(() => {
    if (rafId) cancelAnimationFrame(rafId);
    unsubs.forEach(fn => fn());
    unsubs = [];
  });

  function reconnect() {
    reconnectNode();
  }

  function setView(mode: ViewMode) {
    viewMode = mode;
    dirty = true;
  }
</script>

<svelte:head><title>Live Visualizer — LumiGrid</title></svelte:head>

<div class="viz-root">
  <div class="viz-toolbar">
    <div class="viz-left">
      <h2>Live Visualizer</h2>
      <span class="conn-badge" class:connected>{connected ? 'Connected' : 'Disconnected'}</span>
      <span class="fps-badge">{wsFps} fps</span>
    </div>
    <div class="viz-views">
      <button class="view-btn" class:active={viewMode === 'lanes'} aria-pressed={viewMode === 'lanes'} onclick={() => setView('lanes')}>Lanes</button>
      <button class="view-btn" class:active={viewMode === 'channels'} aria-pressed={viewMode === 'channels'} onclick={() => setView('channels')}>A1–A8</button>
      <button class="view-btn" class:active={viewMode === 'composite'} aria-pressed={viewMode === 'composite'} onclick={() => setView('composite')}>Composite</button>
      <button class="view-btn" class:active={viewMode === 'dmx'} aria-pressed={viewMode === 'dmx'} onclick={() => setView('dmx')}>DMX</button>
    </div>
    <div class="viz-right">
      {#if !connected}
        <button class="btn-reconnect" onclick={reconnect}>Reconnect</button>
      {/if}
    </div>
  </div>

  <div class="viz-canvas-wrap">
    <canvas bind:this={canvasEl} width={1152} height={480}></canvas>
  </div>

  <div class="viz-status">
    <div class="stat"><span class="stat-label">Effect</span><span class="stat-val">{statusInfo.effect || '—'}</span></div>
    <div class="stat"><span class="stat-label">Brightness</span><span class="stat-val">{statusInfo.brightness}</span></div>
    <div class="stat"><span class="stat-label">Output</span><span class="stat-val" class:on={statusInfo.outputsEnabled}>{statusInfo.outputsEnabled ? 'ON' : 'OFF'}</span></div>
    <div class="stat"><span class="stat-label">Test</span><span class="stat-val">{statusInfo.testActive ? 'Active' : '—'}</span></div>
    <div class="stat"><span class="stat-label">Loop</span><span class="stat-val">{statusInfo.autoLoop ? 'Auto' : '—'}</span></div>
  </div>
</div>

<style>
  .viz-root {
    width: 100%;
    max-width: 1240px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .viz-toolbar {
    position: sticky;
    top: 0;
    z-index: 18;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    flex-wrap: wrap;
    padding: .8rem 1rem;
    background: rgba(22,22,24,.90);
    backdrop-filter: blur(22px);
    -webkit-backdrop-filter: blur(22px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
  }
  .viz-left, .viz-right { display: flex; align-items: center; gap: .6rem; flex-wrap: wrap; }
  .viz-left h2 { margin: 0; font-size: 1.15rem; font-weight: 600; color: var(--dash-text); }
  .conn-badge { font-size: .72rem; padding: .2rem .5rem; border-radius: 999px; background: rgba(255,69,58,.15); color: #ff453a; border: 1px solid rgba(255,69,58,.25); }
  .conn-badge.connected { background: rgba(48,209,88,.12); color: var(--dash-ok); border-color: rgba(48,209,88,.25); }
  .fps-badge { font-size: .72rem; padding: .2rem .5rem; border-radius: 999px; background: rgba(255,255,255,.06); color: var(--dash-muted); font-family: monospace; }
  .viz-views { display: flex; gap: .25rem; }
  .view-btn { background: rgba(255,255,255,.05); border: 1px solid rgba(255,255,255,.08); border-radius: 9px; color: var(--dash-muted); font-size: .78rem; padding: .38rem .7rem; cursor: pointer; transition: background .15s, color .15s, border-color .15s; }
  .view-btn:hover { background: rgba(255,255,255,.08); color: var(--dash-text); }
  .view-btn.active { background: rgba(var(--dash-accent-rgb),.18); color: var(--dash-text); border-color: rgba(var(--dash-accent-rgb),.35); }
  .btn-reconnect { background: var(--dash-accent); color: #fff; border: none; border-radius: 9px; padding: .4rem .75rem; font-size: .78rem; font-weight: 600; cursor: pointer; }
  .btn-reconnect:hover { background: var(--dash-accent-hover); }
  .viz-canvas-wrap {
    position: relative;
    background: #070708;
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    overflow: hidden;
    line-height: 0;
    box-shadow: var(--dash-shadow);
  }
  .viz-canvas-wrap::before {
    content: "";
    position: absolute;
    inset: 0;
    pointer-events: none;
    box-shadow: inset 0 1px 0 rgba(255,255,255,.055), inset 0 0 80px rgba(0,0,0,.22);
    z-index: 2;
  }
  .viz-canvas-wrap canvas { width: 100%; height: auto; display: block; image-rendering: pixelated; }
  .viz-status {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 1.5rem;
    padding: .7rem 1rem;
    background: var(--dash-surface);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-md);
    flex-wrap: wrap;
  }
  .stat {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: .15rem;
    min-width: 0;
    padding: .25rem .1rem;
  }
  .stat-label { font-size: .72rem; color: #86868b; text-transform: uppercase; letter-spacing: .04em; }
  .stat-val { font-size: .82rem; color: var(--dash-text); font-family: monospace; }
  .stat-val.on { color: var(--dash-ok); }

  @media (max-width: 768px) {
    .viz-toolbar { position: static; align-items: stretch; }
    .viz-left, .viz-right { width: 100%; justify-content: space-between; }
    .viz-views { flex: 1; }
    .view-btn { flex: 1; }
    .viz-status { gap: .75rem 1rem; }
  }
</style>
