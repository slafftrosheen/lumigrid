<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import {
    BUF_SIZE,
    LANES,
    PIXELS_PER_LANE,
    connect,
    getNodeLiveSnapshot,
    getNodeLiveState,
    reconnect as reconnectNode,
  } from '$lib/state/nodeStore';

  type ViewMode = 'lanes' | 'zones' | 'composite' | 'dmx';

  let nodeIp = $state('');
  let connected = $state(false);
  let viewMode = $state<ViewMode>('lanes');

  let pixelBuf: Uint8Array = new Uint8Array(BUF_SIZE);
  let dirty = $state(false);
  let canvasEl: HTMLCanvasElement | null = null;
  let rafId = 0;
  let unsubscribe: (() => void) | null = null;

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
      case 'zones': drawZones(ctx, w, h); break;
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

  function drawZones(ctx: CanvasRenderingContext2D, w: number, h: number) {
    const zoneCount = 8;
    const zoneW = Math.floor(w / zoneCount);
    const pixelsPerZone = Math.floor(PIXELS_PER_LANE / zoneCount);

    for (let z = 0; z < zoneCount; z++) {
      const x = z * zoneW + 1;
      for (let px = 0; px < pixelsPerZone; px++) {
        const off = (z * PIXELS_PER_LANE + px) * 3;
        const r = pixelBuf[off] ?? 0;
        const g = pixelBuf[off + 1] ?? 0;
        const b = pixelBuf[off + 2] ?? 0;
        if (r === 0 && g === 0 && b === 0) continue;
        ctx.fillStyle = `rgb(${r},${g},${b})`;
        const py = px * Math.floor(h / pixelsPerZone);
        ctx.fillRect(x, py, zoneW - 2, Math.floor(h / pixelsPerZone) - 1);
      }
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
    const liveState = getNodeLiveState();
    unsubscribe = liveState.subscribe(value => {
      nodeIp = value.nodeIp;
      connected = value.connection === 'connected';
      pixelBuf = value.pixels;
      if (value.status) statusInfo = value.status;
      dirty = true;
    });

    const snapshot = getNodeLiveSnapshot();
    lastFrameCount = snapshot.frameCount;
    lastFpsTime = performance.now();
    rafId = requestAnimationFrame(render);
    connect();
  });

  onDestroy(() => {
    if (rafId) cancelAnimationFrame(rafId);
    unsubscribe?.();
  });

  function reconnect() {
    reconnectNode();
  }

  function setView(mode: ViewMode) {
    viewMode = mode;
    dirty = true;
  }
</script>

<div class="viz-root">
  <div class="viz-toolbar">
    <div class="viz-left">
      <h2>Live Visualizer</h2>
      <span class="conn-badge" class:connected>{connected ? 'Connected' : 'Disconnected'}</span>
      <span class="fps-badge">{wsFps} fps</span>
    </div>
    <div class="viz-views">
      <button class="view-btn" class:active={viewMode === 'lanes'} onclick={() => setView('lanes')}>Lanes</button>
      <button class="view-btn" class:active={viewMode === 'zones'} onclick={() => setView('zones')}>Zones</button>
      <button class="view-btn" class:active={viewMode === 'composite'} onclick={() => setView('composite')}>Composite</button>
      <button class="view-btn" class:active={viewMode === 'dmx'} onclick={() => setView('dmx')}>DMX</button>
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
  .viz-root { display: flex; flex-direction: column; gap: 1rem; }
  .viz-toolbar { display: flex; align-items: center; justify-content: space-between; gap: 1rem; flex-wrap: wrap; }
  .viz-left, .viz-right { display: flex; align-items: center; gap: .6rem; }
  .viz-left h2 { margin: 0; font-size: 1.15rem; font-weight: 600; color: #f5f5f7; }
  .conn-badge { font-size: .72rem; padding: .2rem .5rem; border-radius: 999px; background: rgba(255,69,58,.15); color: #ff453a; border: 1px solid rgba(255,69,58,.25); }
  .conn-badge.connected { background: rgba(48,209,88,.12); color: #30d158; border-color: rgba(48,209,88,.25); }
  .fps-badge { font-size: .72rem; padding: .2rem .5rem; border-radius: 999px; background: rgba(255,255,255,.06); color: #a1a1a6; font-family: monospace; }
  .viz-views { display: flex; gap: .25rem; }
  .view-btn { background: rgba(255,255,255,.05); border: 1px solid rgba(255,255,255,.08); border-radius: 8px; color: #a1a1a6; font-size: .78rem; padding: .35rem .65rem; cursor: pointer; transition: background .15s, color .15s; }
  .view-btn:hover { background: rgba(255,255,255,.08); color: #f5f5f7; }
  .view-btn.active { background: rgba(255,45,85,.18); color: #f5f5f7; border-color: rgba(255,45,85,.35); }
  .btn-reconnect { background: rgba(255,45,85,.8); color: #fff; border: none; border-radius: 8px; padding: .35rem .7rem; font-size: .78rem; font-weight: 600; cursor: pointer; }
  .btn-reconnect:hover { background: #ff3b30; }
  .viz-canvas-wrap { background: rgba(255,255,255,.03); border: 1px solid rgba(255,255,255,.08); border-radius: 12px; overflow: hidden; line-height: 0; }
  .viz-canvas-wrap canvas { width: 100%; height: auto; display: block; image-rendering: pixelated; }
  .viz-status { display: flex; gap: 1.5rem; padding: .6rem 1rem; background: rgba(255,255,255,.04); border: 1px solid rgba(255,255,255,.08); border-radius: 10px; flex-wrap: wrap; }
  .stat { display: flex; align-items: center; gap: .4rem; }
  .stat-label { font-size: .72rem; color: #666; text-transform: uppercase; letter-spacing: .04em; }
  .stat-val { font-size: .82rem; color: #f5f5f7; font-family: monospace; }
  .stat-val.on { color: #30d158; }
</style>
