<script lang="ts">
  import { onMount } from 'svelte';
  import { getPresets, savePreset, activatePreset, getEffects } from '$lib/api/node';
  import type { ScenePreset, SceneZone, SceneLayer, EffectMeta, RgbColor } from '$lib/api/node';
  import { readString, NODE_IP_KEY } from '$lib/auth/storage';

  let nodeIp = $state('');
  let presets = $state<ScenePreset[]>([]);
  let effects = $state<EffectMeta[]>([]);
  let selectedIdx = $state<number | null>(null);
  let loading = $state(true);
  let saving = $state(false);
  let error = $state('');
  let toast = $state('');
  let activePreset = $state(-1);

  const BLEND_MODES = ['normal', 'add', 'multiply', 'screen'];
  const ZONE_COUNT = 8;
  const LAYER_COUNT = 3;
  const MAX_PRESETS = 32;

  function defaultLayer(): SceneLayer {
    return { effect: 'solid', blendMode: 'normal', primaryColor: { r: 255, g: 255, b: 255 }, secondaryColor: { r: 0, g: 0, b: 0 }, speedMs: 2000, fxParam: 0, paletteId: 0, opacity: 255, enabled: false };
  }
  function defaultZone(): SceneZone { return { layers: [defaultLayer(), defaultLayer(), defaultLayer()] }; }
  function defaultPreset(idx: number, name?: string): ScenePreset {
    return { index: idx, name: name ?? `Scene ${idx}`, globalBrightness: 128, transitionMs: 1000, zones: Array.from({ length: ZONE_COUNT }, () => defaultZone()) };
  }
  let editScene = $state<ScenePreset | null>(null);
  function selectPreset(idx: number) { const found = presets.find(p => p.index === idx); editScene = found ? structuredClone(found) : defaultPreset(idx); selectedIdx = idx; }
  function findFreeSlot(): number { const used = new Set(presets.map(p => p.index)); for (let i = 0; i < MAX_PRESETS; i++) if (!used.has(i)) return i; return -1; }
  function createScene() { const slot = findFreeSlot(); if (slot < 0) { toast = 'All 32 slots occupied'; return; } selectPreset(slot); }
  function duplicateScene() { if (!editScene) return; const slot = findFreeSlot(); if (slot < 0) { toast = 'All 32 slots occupied'; return; } const dup = structuredClone(editScene); dup.index = slot; dup.name = `${editScene.name} copy`; editScene = dup; selectedIdx = slot; }
  async function saveScene() {
    if (!editScene || saving) return; saving = true; error = '';
    try { await savePreset(nodeIp, editScene); const idx = editScene.index; const name = editScene.name; const existing = presets.findIndex(p => p.index === idx); const saved = structuredClone(editScene); if (existing >= 0) presets[existing] = saved; else presets = [...presets, saved].sort((a, b) => a.index - b.index); presets = presets; toast = `Saved "${name}"`; }
    catch (e) { error = String(e); }
    saving = false;
  }
  async function doActivate(idx: number) { try { await activatePreset(nodeIp, idx); activePreset = idx; toast = `Activated scene ${idx}`; } catch (e) { error = String(e); } }
  function deleteScene(idx: number) { presets = presets.filter(p => p.index !== idx); if (selectedIdx === idx) { editScene = null; selectedIdx = null; } toast = `Cleared slot ${idx}`; }
  function renameScene() { if (!editScene) return; const name = prompt('Scene name:', editScene.name); if (name !== null && name.trim()) editScene.name = name.trim().slice(0, 23); }
  async function loadData() { loading = true; error = ''; try { const [p, e] = await Promise.all([getPresets(nodeIp), getEffects(nodeIp)]); presets = p; effects = e; } catch (e) { error = String(e); } loading = false; }
  onMount(() => { nodeIp = readString(NODE_IP_KEY) ?? ''; if (nodeIp) loadData(); else loading = false; });
  function toastMsg(msg: string) { toast = msg; setTimeout(() => { if (toast === msg) toast = ''; }, 3000); }
  function rgbToHex(c: RgbColor): string { return `#${((1 << 24) + (c.r << 16) + (c.g << 8) + c.b).toString(16).slice(1)}`; }
  function hexToRgb(hex: string): RgbColor { const n = parseInt(hex.replace('#', ''), 16); return { r: (n >> 16) & 0xff, g: (n >> 8) & 0xff, b: n & 0xff }; }
  function getEffectLabel(id: string): string { return effects.find(e => e.id === id)?.label ?? id; }
  let expandedZone = $state<number | null>(0);
</script>

{#if loading}
  <div class="scene-root"><div class="loading">Loading scenes...</div></div>
{:else}
<div class="scene-root">
  <aside class="library">
    <div class="lib-header"><h2>Scenes</h2><button class="btn-primary btn-sm" onclick={createScene}>+ New</button></div>
    <div class="lib-list">
      {#each Array(MAX_PRESETS) as _, i}
        {@const preset = presets.find(p => p.index === i)}
        <button class="lib-item" class:active={selectedIdx === i} class:empty={!preset} onclick={() => selectPreset(i)}>
          <span class="lib-idx">{i}</span><span class="lib-name">{preset ? preset.name : '— empty —'}</span>
          {#if preset}<span class="lib-bri">{preset.globalBrightness}</span>{#if activePreset === i}<span class="lib-active-dot" title="Active"></span>{/if}{/if}
        </button>
      {/each}
    </div>
  </aside>
  <section class="editor">
    {#if editScene}
      <div class="ed-toolbar">
        <div class="ed-title-row"><input class="ed-name" bind:value={editScene.name} maxlength={23} placeholder="Scene name" /><span class="ed-slot">Slot {editScene.index}</span></div>
        <div class="ed-actions">
          <button class="btn-primary" onclick={saveScene} disabled={saving}>{saving ? 'Saving...' : 'Save'}</button>
          <button class="btn-secondary" onclick={() => doActivate(editScene.index)}>Activate</button>
          <button class="btn-secondary" onclick={duplicateScene}>Duplicate</button>
          <button class="btn-secondary" onclick={renameScene}>Rename</button>
          {#if presets.find(p => p.index === editScene.index)}<button class="btn-danger" onclick={() => deleteScene(editScene.index)}>Delete</button>{/if}
        </div>
      </div>
      <div class="ed-globals">
        <label class="field"><span>Brightness</span><input type="range" min="0" max="255" bind:value={editScene.globalBrightness} /><span class="field-val">{editScene.globalBrightness}</span></label>
        <label class="field"><span>Transition (ms)</span><input type="range" min="0" max="10000" step="100" bind:value={editScene.transitionMs} /><span class="field-val">{editScene.transitionMs}</span></label>
      </div>
      <div class="zones">
        {#each editScene.zones as zone, zi}
          <div class="zone-card">
            <button class="zone-header" onclick={() => expandedZone = expandedZone === zi ? null : zi}><span class="zone-chevron">{expandedZone === zi ? '▾' : '▸'}</span><span>Zone {zi}</span><span class="zone-layer-count">{zone.layers.filter(l => l.enabled).length}/{LAYER_COUNT} layers</span></button>
            {#if expandedZone === zi}
              <div class="zone-body">
                {#each zone.layers as layer, li}
                  <div class="layer-card" class:disabled={!layer.enabled}>
                    <div class="layer-header">
                      <label class="toggle"><input type="checkbox" bind:checked={layer.enabled} /><span>L{li + 1}</span></label>
                      {#if layer.enabled}
                        <div class="layer-controls">
                          <label class="field-compact"><span>Effect</span><select bind:value={layer.effect}>{#each effects as fx}<option value={fx.id}>{fx.label}</option>{/each}</select></label>
                          <label class="field-compact"><span>Blend</span><select bind:value={layer.blendMode}>{#each BLEND_MODES as bm}<option value={bm}>{bm}</option>{/each}</select></label>
                          <label class="field-compact color-field"><span>Primary</span><input type="color" value={rgbToHex(layer.primaryColor)} onchange={(e) => layer.primaryColor = hexToRgb((e.target as HTMLInputElement).value)} /></label>
                          <label class="field-compact color-field"><span>Secondary</span><input type="color" value={rgbToHex(layer.secondaryColor)} onchange={(e) => layer.secondaryColor = hexToRgb((e.target as HTMLInputElement).value)} /></label>
                          <label class="field-compact"><span>Speed</span><input type="number" min="10" max="60000" bind:value={layer.speedMs} /></label>
                          <label class="field-compact"><span>Opacity</span><input type="range" min="0" max="255" bind:value={layer.opacity} /><span class="field-val-sm">{layer.opacity}</span></label>
                          {#if effects.find(fx => fx.id === layer.effect)?.hasParameter}
                            {@const fx = effects.find(fx => fx.id === layer.effect)}
                            <label class="field-compact"><span>{fx?.parameterLabel ?? 'Param'}</span><input type="number" min={fx?.parameterMin ?? 0} max={fx?.parameterMax ?? 255} bind:value={layer.fxParam} /></label>
                          {/if}
                        </div>
                      {/if}
                    </div>
                  </div>
                {/each}
              </div>
            {/if}
          </div>
        {/each}
      </div>
    {:else}<div class="editor-empty"><p>Select a scene slot to edit, or create a new scene.</p></div>{/if}
  </section>
  {#if toast}<div class="toast">{toast}</div>{/if}
  {#if error}<div class="error-bar">{error}</div>{/if}
</div>
{/if}

<style>
  .scene-root { display:flex; gap:1.5rem; min-height:calc(100dvh - 4rem); position:relative; }
  .loading { color:#a1a1a6; padding:2rem; }
  .library { width:260px; flex-shrink:0; display:flex; flex-direction:column; gap:.5rem; }
  .lib-header { display:flex; align-items:center; justify-content:space-between; padding:0 .25rem .5rem; border-bottom:1px solid rgba(255,255,255,.08); }
  .lib-header h2 { margin:0; font-size:1.1rem; font-weight:600; color:#f5f5f7; }
  .lib-list { display:flex; flex-direction:column; gap:2px; overflow-y:auto; max-height:calc(100dvh - 8rem); }
  .lib-item { display:flex; align-items:center; gap:.5rem; padding:.45rem .6rem; background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.06); border-radius:8px; color:#a1a1a6; font-size:.82rem; cursor:pointer; text-align:left; transition:background .15s,border-color .15s; }
  .lib-item:hover { background:rgba(255,255,255,.06); color:#f5f5f7; }
  .lib-item.active { background:rgba(255,45,85,.15); border-color:rgba(255,45,85,.35); color:#f5f5f7; }
  .lib-item.empty { opacity:.5; }
  .lib-idx { width:22px; text-align:center; font-size:.7rem; font-weight:700; color:#666; font-family:monospace; }
  .lib-name { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .lib-bri { font-size:.7rem; color:#666; font-family:monospace; }
  .lib-active-dot { width:6px; height:6px; border-radius:50%; background:#30d158; box-shadow:0 0 6px rgba(48,209,88,.6); }
  .editor { flex:1; display:flex; flex-direction:column; gap:1rem; min-width:0; }
  .editor-empty { display:flex; align-items:center; justify-content:center; color:#a1a1a6; font-size:.95rem; min-height:300px; }
  .ed-toolbar { display:flex; align-items:flex-start; justify-content:space-between; gap:1rem; flex-wrap:wrap; }
  .ed-title-row { display:flex; align-items:center; gap:.75rem; }
  .ed-name { background:rgba(255,255,255,.06); border:1px solid rgba(255,255,255,.12); border-radius:8px; color:#f5f5f7; font-size:1.1rem; font-weight:600; padding:.4rem .7rem; width:220px; }
  .ed-name:focus { outline:none; border-color:#ff2d55; }
  .ed-slot { font-size:.75rem; color:#666; font-family:monospace; }
  .ed-actions { display:flex; gap:.4rem; flex-wrap:wrap; }
  .ed-globals { display:flex; gap:1.5rem; padding:.75rem 1rem; background:rgba(255,255,255,.04); border:1px solid rgba(255,255,255,.08); border-radius:12px; }
  .zones { display:flex; flex-direction:column; gap:.5rem; }
  .zone-card { background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.08); border-radius:10px; overflow:hidden; }
  .zone-header { display:flex; align-items:center; gap:.5rem; width:100%; padding:.6rem .8rem; background:transparent; border:none; color:#f5f5f7; font-size:.9rem; font-weight:600; cursor:pointer; text-align:left; }
  .zone-header:hover { background:rgba(255,255,255,.03); }
  .zone-chevron { color:#666; font-size:.8rem; width:1rem; }
  .zone-layer-count { margin-left:auto; font-size:.72rem; color:#666; font-weight:400; }
  .zone-body { padding:.25rem .8rem .8rem; display:flex; flex-direction:column; gap:.5rem; }
  .layer-card { padding:.6rem .7rem; background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.06); border-radius:8px; transition:opacity .2s; }
  .layer-card.disabled { opacity:.45; }
  .layer-header { display:flex; flex-direction:column; gap:.4rem; }
  .toggle { display:flex; align-items:center; gap:.4rem; cursor:pointer; font-size:.78rem; font-weight:600; color:#a1a1a6; }
  .toggle input[type="checkbox"] { accent-color:#ff2d55; width:14px; height:14px; }
  .layer-controls { display:flex; flex-wrap:wrap; gap:.6rem; padding-top:.3rem; }
  .field { display:flex; align-items:center; gap:.5rem; font-size:.82rem; color:#a1a1a6; }
  .field input[type="range"] { flex:1; min-width:80px; accent-color:#ff2d55; }
  .field-val { font-family:monospace; font-size:.75rem; min-width:36px; text-align:right; color:#f5f5f7; }
  .field-compact { display:flex; align-items:center; gap:.3rem; font-size:.75rem; color:#888; }
  .field-compact span { white-space:nowrap; }
  .field-compact select,.field-compact input[type="number"] { background:rgba(255,255,255,.06); border:1px solid rgba(255,255,255,.10); border-radius:6px; color:#f5f5f7; font-size:.78rem; padding:.25rem .4rem; width:90px; }
  .field-compact select { width:100px; }
  .field-compact input[type="range"] { width:60px; accent-color:#ff2d55; }
  .field-val-sm { font-family:monospace; font-size:.68rem; min-width:24px; color:#ccc; }
  .color-field input[type="color"] { width:28px; height:24px; border:1px solid rgba(255,255,255,.15); border-radius:4px; padding:0; cursor:pointer; background:transparent; }
  .btn-sm { padding:.3rem .6rem; font-size:.78rem; }
  .btn-primary { background:rgba(255,45,85,.85); color:#fff; border:none; border-radius:8px; padding:.45rem .9rem; font-size:.82rem; font-weight:600; cursor:pointer; transition:background .2s; }
  .btn-primary:hover { background:#ff3b30; }
  .btn-primary:disabled { opacity:.5; cursor:not-allowed; }
  .btn-secondary { background:rgba(255,255,255,.06); color:#a1a1a6; border:1px solid rgba(255,255,255,.10); border-radius:8px; padding:.45rem .7rem; font-size:.82rem; cursor:pointer; transition:background .2s,color .2s; }
  .btn-secondary:hover { background:rgba(255,255,255,.10); color:#f5f5f7; }
  .btn-danger { background:rgba(255,69,58,.15); color:#ff453a; border:1px solid rgba(255,69,58,.25); border-radius:8px; padding:.45rem .7rem; font-size:.82rem; cursor:pointer; transition:background .2s; }
  .btn-danger:hover { background:rgba(255,69,58,.25); }
  .toast { position:fixed; bottom:1.5rem; right:1.5rem; background:rgba(48,209,88,.9); color:#fff; padding:.6rem 1.2rem; border-radius:10px; font-size:.85rem; font-weight:500; box-shadow:0 8px 24px rgba(0,0,0,.4); animation:slideIn .25s ease-out; }
  .error-bar { position:fixed; bottom:1.5rem; left:50%; transform:translateX(-50%); background:rgba(255,69,58,.9); color:#fff; padding:.6rem 1.2rem; border-radius:10px; font-size:.85rem; box-shadow:0 8px 24px rgba(0,0,0,.4); }
  @keyframes slideIn { from { opacity:0; transform:translateY(10px); } to { opacity:1; transform:translateY(0); } }
  @media (max-width:768px) { .scene-root { flex-direction:column; } .library { width:100%; } .lib-list { flex-direction:row; flex-wrap:wrap; max-height:none; } .lib-item { flex:0 0 auto; } }
</style>
