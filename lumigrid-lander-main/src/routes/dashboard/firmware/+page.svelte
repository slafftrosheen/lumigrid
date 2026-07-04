<script lang="ts">
  import { onMount } from 'svelte';
  import { getInfo, uploadFirmware } from '$lib/api/wled';
  import type { WledInfo } from '$lib/api/wled';

  // ── Feature 8: OTA Group Firmware Update ─────────────────────────────────────

  type NodeOtaState = {
    ip:       string;
    name:     string;
    version:  string;
    status:   'idle' | 'uploading' | 'verifying' | 'done' | 'failed' | 'rollback';
    progress: number;
    error:    string;
  };

  let nodeList = $state<string[]>([]);
  let nodes    = $state<NodeOtaState[]>([]);
  let selected = $state<Set<string>>(new Set());
  let binFile  = $state<File | null>(null);
  let binName  = $state('');
  let prevBin  = $state<File | null>(null); // for rollback
  let running  = $state(false);

  onMount(async () => {
    nodeList = JSON.parse(localStorage.getItem('lg_nodes') ?? '[]');
    selected = new Set(nodeList);
    await refreshVersions();
  });

  async function refreshVersions() {
    const infos = await Promise.allSettled(nodeList.map(async ip => {
      const info = await getInfo(ip);
      return { ip, info };
    }));
    nodes = nodeList.map((ip, i) => {
      const r = infos[i];
      const info: WledInfo | null = r.status === 'fulfilled' ? r.value.info : null;
      return {
        ip,
        name:     info?.name ?? ip,
        version:  info?.ver  ?? '?',
        status:   'idle',
        progress: 0,
        error:    r.status === 'rejected' ? String((r as PromiseRejectedResult).reason) : ''
      };
    });
  }

  function onFile(e: Event) {
    const f = (e.target as HTMLInputElement).files?.[0];
    if (!f) return;
    binFile = f;
    binName = f.name;
  }

  function onDrop(e: DragEvent) {
    e.preventDefault();
    const f = e.dataTransfer?.files[0];
    if (!f || !f.name.endsWith('.bin')) return;
    binFile = f;
    binName = f.name;
  }

  function toggleNode(ip: string) {
    const s = new Set(selected);
    s.has(ip) ? s.delete(ip) : s.add(ip);
    selected = s;
  }

  async function startOta() {
    if (!binFile || !selected.size) return;
    running = true;
    prevBin = binFile; // store for potential rollback

    const targets = nodes.filter(n => selected.has(n.ip));
    // Fan-out: all nodes in parallel
    await Promise.allSettled(targets.map(node => flashNode(node)));
    running = false;
  }

  async function flashNode(node: NodeOtaState) {
    const idx = nodes.findIndex(n => n.ip === node.ip);
    if (idx < 0 || !binFile) return;

    // Upload
    setNodeStatus(idx, 'uploading', 0, '');
    try {
      await uploadFirmware(node.ip, binFile, (pct) => {
        setNodeStatus(idx, 'uploading', pct, '');
      });
    } catch (e) {
      setNodeStatus(idx, 'failed', 0, String(e));
      return;
    }

    // Verify: poll /json/info until version changes or timeout (60 s)
    setNodeStatus(idx, 'verifying', 100, '');
    const oldVer = node.version;
    const t0 = Date.now();
    while (Date.now() - t0 < 60_000) {
      await sleep(3000);
      try {
        const info = await getInfo(node.ip);
        if (info.ver !== oldVer) {
          nodes = nodes.map((n, i) => i === idx ? { ...n, version: info.ver, status: 'done', progress: 100, error: '' } : n);
          return;
        }
      } catch { /* node rebooting — retry */ }
    }
    // Timeout = treat as failed
    setNodeStatus(idx, 'failed', 0, 'Verification timeout — node may need manual check');
  }

  function setNodeStatus(idx: number, status: NodeOtaState['status'], progress: number, error: string) {
    nodes = nodes.map((n, i) => i === idx ? { ...n, status, progress, error } : n);
  }

  async function rollback(node: NodeOtaState) {
    if (!prevBin) return;
    const idx = nodes.findIndex(n => n.ip === node.ip);
    setNodeStatus(idx, 'rollback', 0, '');
    try {
      await uploadFirmware(node.ip, prevBin, (pct) => setNodeStatus(idx, 'rollback', pct, ''));
      setNodeStatus(idx, 'idle', 0, '');
    } catch (e) {
      setNodeStatus(idx, 'failed', 0, `Rollback failed: ${e}`);
    }
  }

  function sleep(ms: number) { return new Promise(r => setTimeout(r, ms)); }

  const STATUS_LABEL: Record<string, string> = {
    idle: 'Ready', uploading: 'Uploading…', verifying: 'Rebooting…', done: '✓ Done', failed: '✗ Failed', rollback: 'Rolling back…'
  };
  const STATUS_COLOR: Record<string, string> = {
    idle: '#a1a1a6', uploading: '#00d4ff', verifying: '#ff9500', done: '#34c759', failed: '#ff453a', rollback: '#ff9500'
  };
</script>

<svelte:head>
  <title>Firmware Update — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>OTA Group Update</h1>
      <p class="sub">Upload firmware once, deploy to all selected nodes simultaneously</p>
    </div>
    <button class="refresh-btn" onclick={refreshVersions} disabled={running}>↺ Refresh versions</button>
  </header>

  <div class="layout">
    <!-- File picker -->
    <div class="upload-card">
      <h2>Firmware Binary</h2>
      <div
        class="drop-zone"
        class:has-file={!!binFile}
        role="button"
        tabindex="0"
        ondrop={onDrop}
        ondragover={(e) => e.preventDefault()}
        onclick={() => document.getElementById('bin-input')?.click()}
        onkeydown={(e) => e.key === 'Enter' && document.getElementById('bin-input')?.click()}
      >
        <div class="drop-icon">{binFile ? '✓' : '⟳'}</div>
        <p class="drop-text">{binName || 'Drop .bin file here or click'}</p>
        {#if binFile}<p class="file-size">{(binFile.size / 1024).toFixed(0)} KB</p>{/if}
      </div>
      <input type="file" accept=".bin" id="bin-input" class="file-input" onchange={onFile} />

      {#if binFile}
        <button
          class="flash-btn"
          onclick={startOta}
          disabled={running || !selected.size}
        >
          {running ? 'Flashing…' : `Flash ${selected.size} Node${selected.size !== 1 ? 's' : ''}`}
        </button>
      {/if}

      <!-- Warning -->
      <div class="warning-box">
        ⚠ Ensure nodes are on stable power during OTA. Failed flashes may require serial recovery.
      </div>
    </div>

    <!-- Node list -->
    <div class="nodes-card">
      <div class="nodes-header">
        <h2>Target Nodes</h2>
        <div class="sel-all-row">
          <button class="sm-btn" onclick={() => { selected = new Set(nodeList); }}>All</button>
          <button class="sm-btn" onclick={() => { selected = new Set(); }}>None</button>
        </div>
      </div>

      {#each nodes as node}
        <div class="node-row" class:selected={selected.has(node.ip)}>
          <input
            type="checkbox"
            checked={selected.has(node.ip)}
            onchange={() => toggleNode(node.ip)}
            id={`n_${node.ip}`}
            disabled={running}
            class="node-check"
          />
          <label for={`n_${node.ip}`} class="node-label">
            <span class="node-name">{node.name}</span>
            <span class="node-ip">{node.ip}</span>
          </label>
          <span class="node-ver">v{node.version}</span>
          <span class="node-status" style:color={STATUS_COLOR[node.status]}>
            {STATUS_LABEL[node.status]}
          </span>

          <!-- Progress bar -->
          {#if node.status === 'uploading' || node.status === 'rollback'}
            <div class="prog-wrap">
              <div class="prog-bar" style:width={`${node.progress}%`} style:background={node.status === 'rollback' ? '#ff9500' : '#00d4ff'}></div>
            </div>
          {/if}

          <!-- Error -->
          {#if node.error}
            <div class="node-err">{node.error}</div>
          {/if}

          <!-- Rollback button (show after fail if prevBin available) -->
          {#if node.status === 'failed' && prevBin}
            <button class="rollback-btn" onclick={() => rollback(node)}>Rollback</button>
          {/if}
        </div>
      {/each}

      {#if !nodes.length}
        <div class="empty">No nodes configured. Add nodes in the sidebar.</div>
      {/if}
    </div>
  </div>
</div>

<style>
  .page { max-width: 960px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .refresh-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.12); border-radius: 999px; color: #f5f5f7; padding: .5rem 1rem; cursor: pointer; font-size: .85rem; }
  .refresh-btn:disabled { opacity: .5; }
  .empty { color: #a1a1a6; padding: 1.5rem; text-align: center; font-size: .85rem; }

  .layout { display: grid; grid-template-columns: 280px 1fr; gap: 1.25rem; align-items: flex-start; }
  @media (max-width: 700px) { .layout { grid-template-columns: 1fr; } }

  .upload-card, .nodes-card {
    background: rgba(29,29,31,.72);
    backdrop-filter: blur(16px);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 18px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: .85rem;
  }
  h2 { margin: 0; font-size: .95rem; font-weight: 600; color: #f5f5f7; }

  .drop-zone { border: 2px dashed rgba(255,255,255,.15); border-radius: 14px; padding: 2rem 1rem; text-align: center; cursor: pointer; transition: border-color .2s, background .2s; }
  .drop-zone:hover, .drop-zone.has-file { border-color: #ff2d55; background: rgba(255,45,85,.06); }
  .drop-icon { font-size: 2rem; margin-bottom: .4rem; color: #a1a1a6; }
  .drop-zone.has-file .drop-icon { color: #34c759; }
  .drop-text { margin: 0; font-size: .82rem; color: #f5f5f7; overflow: hidden; text-overflow: ellipsis; }
  .file-size { margin: .25rem 0 0; font-size: .72rem; color: #a1a1a6; font-family: monospace; }
  .file-input { display: none; }
  .flash-btn { background: rgba(255,45,85,.88); border: none; border-radius: 999px; color: #fff; font-weight: 700; padding: .75rem; cursor: pointer; font-size: .9rem; transition: background .2s, box-shadow .2s; box-shadow: 0 4px 14px rgba(255,45,85,.4); }
  .flash-btn:hover:not(:disabled) { background: #ff3b30; box-shadow: 0 6px 20px rgba(255,45,85,.55); }
  .flash-btn:disabled { opacity: .45; cursor: default; }
  .warning-box { background: rgba(255,149,0,.10); border: 1px solid rgba(255,149,0,.25); border-radius: 10px; color: #ff9500; font-size: .75rem; padding: .65rem .85rem; line-height: 1.4; }

  .nodes-header { display: flex; align-items: center; justify-content: space-between; }
  .sel-all-row { display: flex; gap: .4rem; }
  .sm-btn { background: rgba(255,255,255,.07); border: 1px solid rgba(255,255,255,.10); border-radius: 6px; color: #f5f5f7; font-size: .72rem; padding: .2rem .5rem; cursor: pointer; }

  .node-row { display: flex; align-items: center; flex-wrap: wrap; gap: .5rem .75rem; padding: .7rem .5rem; border-bottom: 1px solid rgba(255,255,255,.05); }
  .node-row:last-child { border-bottom: none; }
  .node-check { accent-color: #ff2d55; width: 16px; height: 16px; flex-shrink: 0; }
  .node-label { display: flex; flex-direction: column; flex: 1; min-width: 100px; cursor: pointer; }
  .node-name { font-size: .85rem; font-weight: 600; color: #f5f5f7; }
  .node-ip { font-size: .72rem; color: #a1a1a6; font-family: monospace; }
  .node-ver { font-size: .75rem; color: #a1a1a6; font-family: monospace; margin-left: auto; }
  .node-status { font-size: .78rem; font-weight: 600; }
  .prog-wrap { width: 100%; height: 4px; background: rgba(255,255,255,.07); border-radius: 999px; overflow: hidden; }
  .prog-bar { height: 100%; border-radius: 999px; transition: width .2s; }
  .node-err { width: 100%; font-size: .72rem; color: #ff453a; font-family: monospace; }
  .rollback-btn { background: rgba(255,149,0,.2); border: 1px solid rgba(255,149,0,.35); border-radius: 7px; color: #ff9500; font-size: .72rem; padding: .2rem .55rem; cursor: pointer; }
  .rollback-btn:hover { background: rgba(255,149,0,.3); }
</style>
