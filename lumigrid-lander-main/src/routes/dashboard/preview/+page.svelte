<script lang="ts">
  // ── Feature 7: GIF Animation Preview (LED Grid Simulator) ────────────────────
  // Pure browser APIs: FileReader, OffscreenCanvas, createImageBitmap, requestAnimationFrame

  let gridW   = $state(16);
  let gridH   = $state(9);
  let gifUrl  = $state('');
  let gifName = $state('');
  let playing = $state(false);
  let error   = $state('');
  let canvasEl = $state<HTMLCanvasElement | null>(null);
  let previewEl = $state<HTMLCanvasElement | null>(null);
  let rafId: number | null = null;
  let imgEl: HTMLImageElement | null = null;

  // We render a GIF by drawing it to an offscreen canvas and sampling pixels
  // at LED grid resolution using requestAnimationFrame to capture each frame.

  function stopPlay() {
    playing = false;
    if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; }
  }

  function renderFrame() {
    if (!imgEl || !canvasEl || !previewEl) return;
    const offCtx = canvasEl.getContext('2d', { willReadFrequently: true });
    const previewCtx = previewEl.getContext('2d');
    if (!offCtx || !previewCtx) return;

    // Draw GIF frame to hidden canvas at native size
    offCtx.clearRect(0, 0, canvasEl.width, canvasEl.height);
    offCtx.drawImage(imgEl, 0, 0, canvasEl.width, canvasEl.height);

    // Sample pixel grid
    const cw = canvasEl.width / gridW;
    const ch = canvasEl.height / gridH;
    const CELL = Math.min(Math.floor(previewEl.width / gridW), Math.floor(previewEl.height / gridH));
    const GUTTER = 2;

    previewCtx.fillStyle = '#050505';
    previewCtx.fillRect(0, 0, previewEl.width, previewEl.height);

    for (let gy = 0; gy < gridH; gy++) {
      for (let gx = 0; gx < gridW; gx++) {
        // Sample centre pixel of each grid cell
        const sx = Math.floor(gx * cw + cw / 2);
        const sy = Math.floor(gy * ch + ch / 2);
        const px = offCtx.getImageData(sx, sy, 1, 1).data;
        const r = px[0], g = px[1], b = px[2];
        const px2 = gx * (CELL + GUTTER);
        const py2 = gy * (CELL + GUTTER);
        // LED glow effect
        previewCtx.save();
        previewCtx.shadowColor = `rgb(${r},${g},${b})`;
        previewCtx.shadowBlur  = CELL * 0.6;
        previewCtx.fillStyle   = `rgb(${r},${g},${b})`;
        previewCtx.beginPath();
        previewCtx.roundRect(px2, py2, CELL, CELL, CELL * 0.2);
        previewCtx.fill();
        previewCtx.restore();
      }
    }

    if (playing) rafId = requestAnimationFrame(renderFrame);
  }

  function startPlay() {
    if (!gifUrl) return;
    playing = true;
    if (rafId !== null) cancelAnimationFrame(rafId);
    rafId = requestAnimationFrame(renderFrame);
  }

  function onFile(e: Event) {
    const files = (e.target as HTMLInputElement).files;
    if (!files?.length) return;
    stopPlay();
    error = '';
    gifName = files[0].name;
    const reader = new FileReader();
    reader.onload = (ev) => {
      const url = ev.target?.result as string;
      gifUrl = url;
      // Create image element for GIF playback
      const img = new Image();
      img.onload = () => {
        imgEl = img;
        // Size hidden canvas to image natural size (or 640×360 max)
        if (canvasEl) {
          canvasEl.width  = Math.min(img.naturalWidth  || 640, 1280);
          canvasEl.height = Math.min(img.naturalHeight || 360, 720);
        }
        // Size preview canvas
        if (previewEl) {
          const CELL = 18, GAP = 3;
          previewEl.width  = gridW * (CELL + GAP);
          previewEl.height = gridH * (CELL + GAP);
        }
        startPlay();
      };
      img.src = url;
    };
    reader.readAsDataURL(files[0]);
  }

  function onDrop(e: DragEvent) {
    e.preventDefault();
    const file = e.dataTransfer?.files[0];
    if (!file || !file.name.toLowerCase().endsWith('.gif')) { error = 'Only GIF files are supported'; return; }
    const input = document.createElement('input');
    input.type = 'file';
    const fakeEvt = new Event('change');
    Object.defineProperty(fakeEvt, 'target', { value: { files: e.dataTransfer?.files } });
    onFile(fakeEvt);
  }

  function onDragOver(e: DragEvent) { e.preventDefault(); }

  function updateGrid() {
    stopPlay();
    if (gifUrl && imgEl) {
      if (previewEl) {
        const CELL = 18, GAP = 3;
        previewEl.width  = gridW * (CELL + GAP);
        previewEl.height = gridH * (CELL + GAP);
      }
      startPlay();
    }
  }
</script>

<svelte:head>
  <title>Animation Preview — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Animation Preview</h1>
      <p class="sub">Simulate how a GIF looks on your LED grid at configured resolution</p>
    </div>
  </header>

  {#if error}<div class="err-box">{error}</div>{/if}

  <div class="layout">
    <!-- Controls -->
    <div class="controls-card">
      <h2>Grid Settings</h2>
      <label class="field">
        <span>Width (pixels)</span>
        <input type="number" min="1" max="128" bind:value={gridW} onchange={updateGrid} />
      </label>
      <label class="field">
        <span>Height (pixels)</span>
        <input type="number" min="1" max="128" bind:value={gridH} onchange={updateGrid} />
      </label>

      <!-- Drop zone -->
      <div
        class="drop-zone"
        role="button"
        tabindex="0"
        ondrop={onDrop}
        ondragover={onDragOver}
        onclick={() => document.getElementById('gif-input')?.click()}
        onkeydown={(e) => e.key === 'Enter' && document.getElementById('gif-input')?.click()}
      >
        <div class="drop-icon">🎞</div>
        <p class="drop-text">{gifName || 'Drop GIF here or click to browse'}</p>
        <p class="drop-hint">Only animated GIFs</p>
      </div>
      <input type="file" accept=".gif,image/gif" id="gif-input" class="file-input" onchange={onFile} />

      <div class="playback-btns">
        <button class="play-btn" onclick={startPlay} disabled={!gifUrl || playing}>▶ Play</button>
        <button class="stop-btn" onclick={stopPlay} disabled={!playing}>■ Stop</button>
      </div>

      {#if gifUrl}
        <div class="original-label">Original GIF</div>
        <img src={gifUrl} alt="original GIF" class="original-gif" />
      {/if}
    </div>

    <!-- Preview -->
    <div class="preview-card">
      <h2>LED Grid Preview — {gridW}×{gridH}</h2>
      <!-- Hidden canvas for source sampling -->
      <canvas bind:this={canvasEl} class="hidden-canvas" aria-hidden="true"></canvas>
      <!-- Visible LED grid canvas -->
      <div class="preview-wrap">
        {#if !gifUrl}
          <div class="preview-placeholder">
            <div class="ph-icon">◉</div>
            <p>Upload a GIF to see the LED grid simulation</p>
          </div>
        {:else}
          <canvas bind:this={previewEl} class="preview-canvas" aria-label="LED grid preview"></canvas>
        {/if}
      </div>
      <p class="preview-note">Each square = 1 LED pixel · Shadow = approximate glow bleed</p>
    </div>
  </div>
</div>

<style>
  .page { max-width: 1000px; }
  .page-header { margin-bottom: 1.5rem; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .err-box { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3); border-radius: 12px; color: #ff453a; padding: .9rem 1rem; font-size: .85rem; margin-bottom: 1rem; }

  .layout { display: grid; grid-template-columns: 280px 1fr; gap: 1.25rem; align-items: flex-start; }
  @media (max-width: 700px) { .layout { grid-template-columns: 1fr; } }

  .controls-card, .preview-card {
    background: rgba(29,29,31,.72);
    backdrop-filter: blur(16px);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 18px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: .75rem;
  }
  h2 { margin: 0; font-size: .95rem; font-weight: 600; color: #f5f5f7; }

  .field { display: flex; flex-direction: column; gap: .25rem; }
  .field span { font-size: .72rem; font-weight: 600; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; }
  .field input { background: rgba(255,255,255,.06); border: 1px solid rgba(255,255,255,.12); border-radius: 8px; color: #f5f5f7; font-size: .9rem; padding: .4rem .65rem; width: 80px; }
  .field input:focus { outline: none; border-color: #ff2d55; }

  .drop-zone {
    border: 2px dashed rgba(255,255,255,.15);
    border-radius: 14px;
    padding: 1.5rem 1rem;
    text-align: center;
    cursor: pointer;
    transition: border-color .2s, background .2s;
  }
  .drop-zone:hover { border-color: #ff2d55; background: rgba(255,45,85,.06); }
  .drop-icon { font-size: 2rem; margin-bottom: .4rem; }
  .drop-text { margin: 0; font-size: .82rem; color: #f5f5f7; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .drop-hint { margin: .2rem 0 0; font-size: .7rem; color: #a1a1a6; }
  .file-input { display: none; }

  .playback-btns { display: flex; gap: .5rem; }
  .play-btn { flex: 1; background: rgba(255,45,85,.85); border: none; border-radius: 8px; color: #fff; font-weight: 600; font-size: .85rem; padding: .55rem; cursor: pointer; transition: background .2s; }
  .play-btn:hover:not(:disabled) { background: #ff3b30; }
  .play-btn:disabled { opacity: .4; cursor: default; }
  .stop-btn { flex: 1; background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 8px; color: #f5f5f7; font-size: .85rem; padding: .55rem; cursor: pointer; }
  .stop-btn:disabled { opacity: .4; cursor: default; }

  .original-label { font-size: .7rem; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; }
  .original-gif { width: 100%; max-height: 120px; object-fit: contain; border-radius: 8px; border: 1px solid rgba(255,255,255,.08); }

  .hidden-canvas { display: none; }
  .preview-wrap { display: flex; align-items: center; justify-content: center; min-height: 200px; background: #000; border-radius: 12px; overflow: auto; }
  .preview-canvas { image-rendering: pixelated; display: block; }
  .preview-placeholder { display: flex; flex-direction: column; align-items: center; gap: .75rem; color: #a1a1a6; padding: 2rem; }
  .ph-icon { font-size: 2.5rem; background: linear-gradient(135deg, #ff2d55, #00d4ff); -webkit-background-clip: text; background-clip: text; color: transparent; }
  .preview-note { font-size: .72rem; color: #a1a1a6; margin: 0; text-align: center; }
</style>
