<script lang="ts">
  import { onMount } from 'svelte';
  import { getPresets, savePreset, activatePreset, getSignageStatus, getEffects } from '$lib/api/node';
  import type { ScenePreset, EffectMeta, SignageStatus } from '$lib/api/node';
  import {
    createDefaultPreset,
    clonePreset,
    isPresetDirty,
    computeZonePreviewColor,
    rgbToHex,
    hexToRgb,
    deletePreset,
    BLEND_MODES,
    ZONE_COUNT,
    LAYER_COUNT,
    MAX_PRESETS,
  } from '$lib/api/scenes';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let presets = $state<ScenePreset[]>([]);
  let effects = $state<EffectMeta[]>([]);
  let selectedIdx = $state<number | null>(null);
  let loading = $state(true);
  let saving = $state(false);
  let activating = $state(false);
  let error = $state('');
  let toast = $state('');
  let activePreset = $state(-1);
  let sceneApplyPending = $state(false);
  let activationGeneration = $state(0);
  let activationPreset = $state(-1);
  let expandedZone = $state<number | null>(0);

  let editScene = $state<ScenePreset | null>(null);
  let savedSnapshot = $state<ScenePreset | null>(null);

  const isDirty = $derived(isPresetDirty(editScene, savedSnapshot));
  const isActive = $derived(
    editScene !== null &&
    activePreset === editScene.index &&
    !sceneApplyPending
  );

  function selectPreset(idx: number) {
    const found = presets.find(p => p.index === idx);
    if (found) {
      editScene = clonePreset(found);
      savedSnapshot = clonePreset(found);
    } else {
      const def = createDefaultPreset(idx);
      editScene = clonePreset(def);
      savedSnapshot = null;
    }
    selectedIdx = idx;
    error = '';
  }

  function findFreeSlot(): number {
    const used = new Set(presets.map(p => p.index));
    for (let i = 0; i < MAX_PRESETS; i++) {
      if (!used.has(i)) return i;
    }
    return -1;
  }

  function createScene() {
    const slot = findFreeSlot();
    if (slot < 0) {
      toastMsg('All 32 slots are occupied');
      return;
    }
    selectPreset(slot);
  }

  function duplicateScene() {
    if (!editScene) return;
    const slot = findFreeSlot();
    if (slot < 0) {
      toastMsg('All 32 slots are occupied');
      return;
    }
    const dup = clonePreset(editScene);
    dup.index = slot;
    dup.name = `${editScene.name} (copy)`.slice(0, 23);
    editScene = dup;
    savedSnapshot = null;
    selectedIdx = slot;
    toastMsg(`Duplicated to slot ${slot}`);
  }

  function revertEdits() {
    if (!editScene) return;
    if (savedSnapshot) {
      editScene = clonePreset(savedSnapshot);
      toastMsg('Reverted edits to saved preset');
    } else if (selectedIdx !== null) {
      editScene = createDefaultPreset(selectedIdx);
      toastMsg('Reset to default preset');
    }
  }

  async function saveScene() {
    if (!editScene || saving) return;
    saving = true;
    error = '';
    try {
      await savePreset(nodeIp, editScene);
      const idx = editScene.index;
      const name = editScene.name;
      const saved = clonePreset(editScene);
      savedSnapshot = clonePreset(editScene);

      const existing = presets.findIndex(p => p.index === idx);
      if (existing >= 0) {
        presets[existing] = saved;
      } else {
        presets = [...presets, saved].sort((a, b) => a.index - b.index);
      }
      toastMsg(`Saved "${name}" to slot ${idx}`);
    } catch (e) {
      error = String(e);
    } finally {
      saving = false;
    }
  }

  function observeSignageStatus(status: SignageStatus) {
    if (typeof status.activePreset === 'number') {
      activePreset = status.activePreset;
    }
    if (typeof status.sceneApplyPending === 'boolean') {
      sceneApplyPending = status.sceneApplyPending;
    }

    if (!activating || activationGeneration === 0) return;

    if (status.outputFault) {
      activating = false;
      activationGeneration = 0;
      activationPreset = -1;
      error = 'Scene was accepted, but the node reported a hardware output fault.';
      return;
    }

    if (
      typeof status.appliedGeneration === 'number' &&
      status.appliedGeneration >= activationGeneration
    ) {
      const committedPreset = activationPreset;
      activating = false;
      activationGeneration = 0;
      activationPreset = -1;
      sceneApplyPending = false;
      toastMsg(`Scene ${committedPreset} committed to hardware`);
    }
  }

  async function waitForAppliedGeneration(targetGeneration: number) {
    const deadline = Date.now() + 5000;
    let lastPollError = '';

    while (
      activating &&
      activationGeneration === targetGeneration &&
      Date.now() < deadline
    ) {
      try {
        const status = await getSignageStatus(nodeIp);
        lastPollError = '';
        observeSignageStatus(status);
        if (!activating) return;
      } catch (e) {
        lastPollError = String(e);
      }

      await new Promise(resolve => setTimeout(resolve, 150));
    }

    if (activating && activationGeneration === targetGeneration) {
      activating = false;
      activationGeneration = 0;
      activationPreset = -1;
      error = lastPollError
        ? `Activation accepted, but output confirmation failed: ${lastPollError}`
        : 'Activation accepted, but no hardware commit was confirmed within 5 seconds.';
    }
  }

  async function doActivate(idx: number) {
    if (activating) return;
    activating = true;
    error = '';
    try {
      const result = await activatePreset(nodeIp, idx);
      activationGeneration = result.generation;
      activationPreset = result.preset;
      sceneApplyPending = true;
      await waitForAppliedGeneration(result.generation);
    } catch (e) {
      error = String(e);
      activating = false;
      activationGeneration = 0;
      activationPreset = -1;
      sceneApplyPending = false;
    }
  }

  async function removeScene(idx: number) {
    if (!confirm(`Clear preset slot ${idx}? This will reset slot ${idx} on the node.`)) return;
    try {
      if (nodeIp) {
        await deletePreset(nodeIp, idx);
      }
      presets = presets.filter(p => p.index !== idx);
      if (selectedIdx === idx) {
        editScene = null;
        savedSnapshot = null;
        selectedIdx = null;
      }
      toastMsg(`Cleared preset slot ${idx}`);
    } catch (e) {
      error = String(e);
    }
  }

  let loadGen = 0;

  async function loadData(targetIp: string, gen: number) {
    loading = true;
    error = '';
    presets = [];
    effects = [];
    selectedIdx = null;
    editScene = null;
    savedSnapshot = null;
    try {
      const [p, e, status] = await Promise.all([
        getPresets(targetIp),
        getEffects(targetIp),
        getSignageStatus(targetIp),
      ]);
      if (gen !== loadGen) return;
      presets = p;
      effects = e;
      observeSignageStatus(status);
      if (presets.length > 0 && selectedIdx === null) {
        selectPreset(presets[0].index);
      }
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) {
        loading = false;
      }
    }
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      if (state.status?.activePreset !== undefined) {
        activePreset = state.status.activePreset;
      }

      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        if (nodeIp) {
          loadData(nodeIp, loadGen);
        } else {
          presets = [];
          effects = [];
          selectedIdx = null;
          editScene = null;
          savedSnapshot = null;
          loading = false;
          error = '';
        }
      }
    });
    return unsub;
  });

  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => {
      if (toast === msg) toast = '';
    }, 3000);
  }
</script>

{#if loading}
  <div class="scene-root">
    <div class="loading-box">
      <div class="spinner"></div>
      <p>Loading presets and effect metadata from node...</p>
    </div>
  </div>
{:else}
  <div class="scene-root">
    <!-- Scene Library Sidebar -->
    <aside class="library">
      <div class="lib-header">
        <div class="lib-title-row">
          <h2>Scene Library</h2>
          <span class="lib-count">{presets.length}/{MAX_PRESETS}</span>
        </div>
        <button class="btn-primary btn-sm" onclick={createScene} title="Create new preset">+ New</button>
      </div>

      <div class="lib-list">
        {#each Array(MAX_PRESETS) as _, i}
          {@const preset = presets.find(p => p.index === i)}
          <button
            class="lib-item"
            class:active={selectedIdx === i}
            class:empty={!preset}
            onclick={() => selectPreset(i)}
            type="button"
          >
            <span class="lib-idx">{i.toString().padStart(2, '0')}</span>
            <span class="lib-name">{preset ? preset.name : '— empty slot —'}</span>
            {#if preset}
              <span class="lib-bri" title="Global brightness">{preset.globalBrightness}</span>
              {#if activePreset === i}
                <span class="lib-active-dot" title="Active on hardware"></span>
              {/if}
            {/if}
          </button>
        {/each}
      </div>
    </aside>

    <!-- Scene Editor Workspace -->
    <section class="editor">
      {#if editScene}
        <!-- Top Toolbar -->
        <div class="ed-toolbar">
          <div class="ed-title-group">
            <div class="ed-title-row">
              <input
                class="ed-name"
                bind:value={editScene.name}
                maxlength={23}
                placeholder="Scene name"
              />
              <span class="ed-slot">Slot {editScene.index}</span>
            </div>

            <!-- State Badges: EDITED / SAVED / ACTIVE -->
            <div class="status-badges">
              {#if isActive}
                <span class="badge badge-active" title="Preset is currently live on the physical node">
                  <span class="pulse-dot"></span> ACTIVE
                </span>
              {/if}
              {#if isDirty}
                <span class="badge badge-edited" title="Unsaved modifications">
                  EDITED
                </span>
              {:else if savedSnapshot}
                <span class="badge badge-saved" title="Preset matches persisted node storage">
                  SAVED
                </span>
              {:else}
                <span class="badge badge-new" title="New preset not yet written to node">
                  NEW
                </span>
              {/if}
            </div>
          </div>

          <div class="ed-actions">
            <button
              class="btn-primary"
              onclick={saveScene}
              disabled={saving}
              title="Save preset to hardware"
              type="button"
            >
              {saving ? 'Saving...' : 'Save'}
            </button>

            <button
              class="btn-activate"
              class:is-active-btn={isActive}
              onclick={() => doActivate(editScene!.index)}
              disabled={activating}
              title="Activate scene on hardware fixture"
              type="button"
            >
              {activating ? 'Activating...' : isActive ? 'Re-Activate' : 'Activate'}
            </button>

            <button
              class="btn-secondary"
              onclick={revertEdits}
              disabled={!isDirty}
              title="Revert modifications to last saved state"
              type="button"
            >
              Revert
            </button>

            <button
              class="btn-secondary"
              onclick={duplicateScene}
              title="Duplicate this preset into next free slot"
              type="button"
            >
              Duplicate
            </button>

            {#if presets.some(p => p.index === editScene!.index)}
              <button
                class="btn-danger"
                onclick={() => removeScene(editScene!.index)}
                title="Clear this preset slot on hardware"
                type="button"
              >
                Clear Slot
              </button>
            {/if}
          </div>
        </div>

        <!-- Visual Software Preview (8 Zones Strip) -->
        <div class="preview-panel">
          <div class="preview-header">
            <span class="preview-title">Software Fixture Preview</span>
            <span class="preview-subtitle">Zone Color Synthesis (8 Zones)</span>
          </div>

          <div class="preview-strip">
            {#each editScene.zones as zone, zi}
              {@const preview = computeZonePreviewColor(zone, editScene.globalBrightness)}
              {@const activeLayers = zone.layers.filter(l => l.enabled).length}
              <button
                class="zone-preview-box"
                class:selected={expandedZone === zi}
                onclick={() => (expandedZone = zi)}
                style="background: rgb({preview.r}, {preview.g}, {preview.b});"
                type="button"
                title="Zone {zi} (RGB {preview.r},{preview.g},{preview.b}) - Click to edit"
              >
                <div class="zone-preview-overlay">
                  <span class="zp-num">Z{zi}</span>
                  <span class="zp-layers">{activeLayers}/3</span>
                </div>
              </button>
            {/each}
          </div>
        </div>

        <!-- Global Properties -->
        <div class="ed-globals">
          <label class="field">
            <span class="field-label">Global Brightness</span>
            <input
              type="range"
              min="0"
              max="255"
              bind:value={editScene.globalBrightness}
            />
            <span class="field-val">{editScene.globalBrightness}</span>
          </label>

          <label class="field">
            <span class="field-label">Transition (ms)</span>
            <input
              type="range"
              min="0"
              max="10000"
              step="100"
              bind:value={editScene.transitionMs}
            />
            <span class="field-val">{editScene.transitionMs} ms</span>
          </label>
        </div>

        <!-- Zones and Layers Editor -->
        <div class="zones">
          {#each editScene.zones as zone, zi}
            {@const preview = computeZonePreviewColor(zone, editScene.globalBrightness)}
            {@const enabledCount = zone.layers.filter(l => l.enabled).length}
            <div class="zone-card" class:active-zone={expandedZone === zi}>
              <button
                class="zone-header"
                onclick={() => (expandedZone = expandedZone === zi ? null : zi)}
                type="button"
              >
                <div
                  class="zone-swatch"
                  style="background: rgb({preview.r}, {preview.g}, {preview.b});"
                ></div>
                <span class="zone-chevron">{expandedZone === zi ? '▾' : '▸'}</span>
                <span class="zone-title">Zone {zi}</span>
                <span class="zone-layer-count">
                  {enabledCount}/{LAYER_COUNT} layers enabled
                </span>
              </button>

              {#if expandedZone === zi}
                <div class="zone-body">
                  {#each zone.layers as layer, li}
                    <div class="layer-card" class:disabled={!layer.enabled}>
                      <div class="layer-header">
                        <label class="toggle">
                          <input type="checkbox" bind:checked={layer.enabled} />
                          <span class="layer-title">Layer {li + 1}</span>
                        </label>

                        {#if layer.enabled}
                          <div class="layer-controls">
                            <!-- Effect dropdown -->
                            <label class="field-compact">
                              <span>Effect</span>
                              <select bind:value={layer.effect}>
                                {#if effects.length > 0}
                                  {#each effects as fx}
                                    <option value={fx.id}>{fx.label}</option>
                                  {/each}
                                {:else}
                                  <option value="solid">Solid</option>
                                  <option value="breathe">Breathe</option>
                                  <option value="chase">Chase</option>
                                  <option value="rainbow">Rainbow</option>
                                  <option value="sparkle">Sparkle</option>
                                  <option value="gradient">Gradient</option>
                                {/if}
                              </select>
                            </label>

                            <!-- Blend Mode -->
                            <label class="field-compact">
                              <span>Blend</span>
                              <select bind:value={layer.blendMode}>
                                {#each BLEND_MODES as bm}
                                  <option value={bm}>{bm}</option>
                                {/each}
                              </select>
                            </label>

                            <!-- Colors -->
                            <label class="field-compact color-field">
                              <span>Primary</span>
                              <input
                                type="color"
                                value={rgbToHex(layer.primaryColor)}
                                onchange={e => {
                                  const parsed = hexToRgb((e.target as HTMLInputElement).value);
                                  if (parsed) layer.primaryColor = parsed;
                                }}
                              />
                            </label>

                            <label class="field-compact color-field">
                              <span>Secondary</span>
                              <input
                                type="color"
                                value={rgbToHex(layer.secondaryColor)}
                                onchange={e => {
                                  const parsed = hexToRgb((e.target as HTMLInputElement).value);
                                  if (parsed) layer.secondaryColor = parsed;
                                }}
                              />
                            </label>

                            <!-- Speed -->
                            <label class="field-compact">
                              <span>Speed (ms)</span>
                              <input
                                type="number"
                                min="10"
                                max="60000"
                                step="50"
                                bind:value={layer.speedMs}
                              />
                            </label>

                            <!-- Opacity -->
                            <label class="field-compact">
                              <span>Opacity</span>
                              <input
                                type="range"
                                min="0"
                                max="255"
                                bind:value={layer.opacity}
                              />
                              <span class="field-val-sm">{layer.opacity}</span>
                            </label>

                            <!-- Param if effect supports it -->
                            {#if effects.find(fx => fx.id === layer.effect)?.hasParameter}
                              {@const fx = effects.find(fx => fx.id === layer.effect)}
                              <label class="field-compact">
                                <span>{fx?.parameterLabel ?? 'Param'}</span>
                                <input
                                  type="number"
                                  min={fx?.parameterMin ?? 0}
                                  max={fx?.parameterMax ?? 255}
                                  bind:value={layer.fxParam}
                                />
                              </label>
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
      {:else}
        <div class="editor-empty">
          <div class="empty-icon">🎨</div>
          <h3>Scene Studio</h3>
          <p>Select a scene from the library or click <strong>+ New</strong> to build a preset.</p>
        </div>
      {/if}
    </section>

    {#if toast}
      <div class="toast">{toast}</div>
    {/if}
    {#if error}
      <div class="error-bar">
        <span>{error}</span>
        <button class="err-close" onclick={() => (error = '')} type="button">✕</button>
      </div>
    {/if}
  </div>
{/if}

<style>
  .scene-root {
    display: flex;
    gap: 1.5rem;
    min-height: calc(100dvh - 4rem);
    position: relative;
  }

  .loading-box {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    width: 100%;
    color: #a1a1a6;
    min-height: 300px;
  }
  .spinner {
    width: 32px;
    height: 32px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: #0071e3;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  /* Library Sidebar */
  .library {
    width: 270px;
    flex-shrink: 0;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .lib-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 0.25rem 0.5rem;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }
  .lib-title-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .lib-header h2 {
    margin: 0;
    font-size: 1.1rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .lib-count {
    font-size: 0.72rem;
    color: #86868b;
    font-family: monospace;
  }
  .lib-list {
    display: flex;
    flex-direction: column;
    gap: 3px;
    overflow-y: auto;
    max-height: calc(100dvh - 8.5rem);
    padding-right: 0.2rem;
  }
  .lib-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem 0.65rem;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 8px;
    color: #a1a1a6;
    font-size: 0.82rem;
    cursor: pointer;
    text-align: left;
    transition: all 0.15s ease;
  }
  .lib-item:hover {
    background: rgba(255, 255, 255, 0.06);
    color: #f5f5f7;
    border-color: rgba(255, 255, 255, 0.12);
  }
  .lib-item.active {
    background: rgba(0, 113, 227, 0.15);
    border-color: rgba(0, 113, 227, 0.4);
    color: #f5f5f7;
  }
  .lib-item.empty {
    opacity: 0.45;
  }
  .lib-idx {
    width: 22px;
    text-align: center;
    font-size: 0.72rem;
    font-weight: 700;
    color: #86868b;
    font-family: monospace;
  }
  .lib-name {
    flex: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .lib-bri {
    font-size: 0.7rem;
    color: #666;
    font-family: monospace;
  }
  .lib-active-dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: #30d158;
    box-shadow: 0 0 8px rgba(48, 209, 88, 0.8);
  }

  /* Editor Workspace */
  .editor {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    min-width: 0;
  }
  .editor-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 0.75rem;
    color: #a1a1a6;
    min-height: 400px;
    background: rgba(255, 255, 255, 0.02);
    border: 1px dashed rgba(255, 255, 255, 0.1);
    border-radius: 12px;
  }
  .empty-icon {
    font-size: 2.5rem;
  }

  .ed-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    flex-wrap: wrap;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    padding: 0.75rem 1rem;
    border-radius: 12px;
  }
  .ed-title-group {
    display: flex;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .ed-title-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .ed-name {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: 1.1rem;
    font-weight: 600;
    padding: 0.4rem 0.7rem;
    width: 220px;
  }
  .ed-name:focus {
    outline: none;
    border-color: #0071e3;
    box-shadow: 0 0 0 2px rgba(0, 113, 227, 0.3);
  }
  .ed-slot {
    font-size: 0.75rem;
    color: #86868b;
    font-family: monospace;
    background: rgba(255, 255, 255, 0.04);
    padding: 0.25rem 0.5rem;
    border-radius: 6px;
  }

  /* Status Badges */
  .status-badges {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }
  .badge {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    padding: 0.25rem 0.55rem;
    border-radius: 6px;
    text-transform: uppercase;
  }
  .badge-active {
    background: rgba(48, 209, 88, 0.15);
    border: 1px solid rgba(48, 209, 88, 0.4);
    color: #30d158;
  }
  .badge-edited {
    background: rgba(255, 159, 10, 0.15);
    border: 1px solid rgba(255, 159, 10, 0.4);
    color: #ff9f0a;
  }
  .badge-saved {
    background: rgba(10, 132, 255, 0.15);
    border: 1px solid rgba(10, 132, 255, 0.4);
    color: #64d2ff;
  }
  .badge-new {
    background: rgba(191, 90, 242, 0.15);
    border: 1px solid rgba(191, 90, 242, 0.4);
    color: #bf5af2;
  }
  .pulse-dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #30d158;
    animation: pulse 1.5s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.4; transform: scale(0.8); }
  }

  .ed-actions {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }

  /* Preview Panel */
  .preview-panel {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding: 0.75rem 1rem;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 12px;
  }
  .preview-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .preview-title {
    font-size: 0.8rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .preview-subtitle {
    font-size: 0.7rem;
    color: #86868b;
  }
  .preview-strip {
    display: grid;
    grid-template-columns: repeat(8, 1fr);
    gap: 0.5rem;
    min-height: 54px;
  }
  .zone-preview-box {
    position: relative;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    cursor: pointer;
    overflow: hidden;
    padding: 0;
    transition: transform 0.15s ease, border-color 0.15s ease, box-shadow 0.15s ease;
  }
  .zone-preview-box:hover {
    transform: translateY(-2px);
    border-color: rgba(255, 255, 255, 0.4);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  }
  .zone-preview-box.selected {
    border: 2px solid #0071e3;
    box-shadow: 0 0 10px rgba(0, 113, 227, 0.5);
  }
  .zone-preview-overlay {
    position: absolute;
    inset: 0;
    background: linear-gradient(to top, rgba(0, 0, 0, 0.75), transparent 70%);
    display: flex;
    justify-content: space-between;
    align-items: flex-end;
    padding: 0.3rem 0.4rem;
  }
  .zp-num {
    font-size: 0.72rem;
    font-weight: 700;
    color: #fff;
    font-family: monospace;
  }
  .zp-layers {
    font-size: 0.65rem;
    color: #ddd;
    font-family: monospace;
  }

  /* Globals */
  .ed-globals {
    display: flex;
    gap: 1.5rem;
    padding: 0.75rem 1rem;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 12px;
  }

  /* Zones */
  .zones {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .zone-card {
    background: rgba(255, 255, 255, 0.02);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 10px;
    overflow: hidden;
    transition: border-color 0.15s ease;
  }
  .zone-card.active-zone {
    border-color: rgba(0, 113, 227, 0.35);
  }
  .zone-header {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    width: 100%;
    padding: 0.65rem 0.85rem;
    background: transparent;
    border: none;
    color: #f5f5f7;
    font-size: 0.88rem;
    font-weight: 600;
    cursor: pointer;
    text-align: left;
  }
  .zone-header:hover {
    background: rgba(255, 255, 255, 0.03);
  }
  .zone-swatch {
    width: 14px;
    height: 14px;
    border-radius: 4px;
    border: 1px solid rgba(255, 255, 255, 0.2);
  }
  .zone-chevron {
    color: #86868b;
    font-size: 0.8rem;
    width: 1rem;
  }
  .zone-title {
    flex: 1;
  }
  .zone-layer-count {
    font-size: 0.72rem;
    color: #86868b;
    font-weight: 400;
  }
  .zone-body {
    padding: 0.35rem 0.85rem 0.85rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  /* Layers */
  .layer-card {
    padding: 0.65rem 0.75rem;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 8px;
    transition: opacity 0.2s ease;
  }
  .layer-card.disabled {
    opacity: 0.45;
  }
  .layer-header {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
  }
  .toggle {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    cursor: pointer;
    font-size: 0.8rem;
    font-weight: 600;
    color: #f5f5f7;
  }
  .toggle input[type='checkbox'] {
    accent-color: #0071e3;
    width: 15px;
    height: 15px;
  }
  .layer-title {
    font-family: monospace;
  }
  .layer-controls {
    display: flex;
    flex-wrap: wrap;
    gap: 0.65rem;
    padding-top: 0.35rem;
  }

  /* Fields */
  .field {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    font-size: 0.82rem;
    color: #a1a1a6;
  }
  .field-label {
    white-space: nowrap;
    font-size: 0.78rem;
  }
  .field input[type='range'] {
    flex: 1;
    min-width: 90px;
    accent-color: #0071e3;
  }
  .field-val {
    font-family: monospace;
    font-size: 0.75rem;
    min-width: 48px;
    text-align: right;
    color: #f5f5f7;
  }
  .field-compact {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.75rem;
    color: #86868b;
  }
  .field-compact span {
    white-space: nowrap;
  }
  .field-compact select,
  .field-compact input[type='number'] {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #f5f5f7;
    font-size: 0.78rem;
    padding: 0.25rem 0.45rem;
    width: 95px;
  }
  .field-compact select {
    width: 105px;
  }
  .field-compact input[type='range'] {
    width: 65px;
    accent-color: #0071e3;
  }
  .field-val-sm {
    font-family: monospace;
    font-size: 0.68rem;
    min-width: 24px;
    color: #ccc;
  }
  .color-field input[type='color'] {
    width: 28px;
    height: 24px;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 4px;
    padding: 0;
    cursor: pointer;
    background: transparent;
  }

  /* Buttons */
  .btn-sm {
    padding: 0.3rem 0.6rem;
    font-size: 0.75rem;
  }
  .btn-primary {
    background: #0071e3;
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 0.45rem 0.95rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .btn-primary:hover:not(:disabled) {
    background: #0077ed;
  }
  .btn-primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .btn-activate {
    background: rgba(48, 209, 88, 0.18);
    color: #30d158;
    border: 1px solid rgba(48, 209, 88, 0.4);
    border-radius: 8px;
    padding: 0.45rem 0.85rem;
    font-size: 0.82rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-activate:hover:not(:disabled) {
    background: rgba(48, 209, 88, 0.28);
  }
  .btn-activate.is-active-btn {
    background: #30d158;
    color: #000;
  }
  .btn-secondary {
    background: rgba(255, 255, 255, 0.06);
    color: #a1a1a6;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    padding: 0.45rem 0.75rem;
    font-size: 0.82rem;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-secondary:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.1);
    color: #f5f5f7;
  }
  .btn-secondary:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }
  .btn-danger {
    background: rgba(255, 69, 58, 0.15);
    color: #ff453a;
    border: 1px solid rgba(255, 69, 58, 0.3);
    border-radius: 8px;
    padding: 0.45rem 0.75rem;
    font-size: 0.82rem;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn-danger:hover {
    background: rgba(255, 69, 58, 0.25);
  }

  /* Toasts & Errors */
  .toast {
    position: fixed;
    bottom: 1.5rem;
    right: 1.5rem;
    background: rgba(48, 209, 88, 0.92);
    color: #fff;
    padding: 0.6rem 1.2rem;
    border-radius: 10px;
    font-size: 0.85rem;
    font-weight: 500;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
    z-index: 100;
  }
  .error-bar {
    position: fixed;
    bottom: 1.5rem;
    left: 50%;
    transform: translateX(-50%);
    background: rgba(255, 69, 58, 0.95);
    color: #fff;
    padding: 0.6rem 1.2rem;
    border-radius: 10px;
    font-size: 0.85rem;
    display: flex;
    align-items: center;
    gap: 0.75rem;
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
    z-index: 100;
  }
  .err-close {
    background: none;
    border: none;
    color: #fff;
    cursor: pointer;
    font-weight: bold;
  }

  @media (max-width: 900px) {
    .scene-root { flex-direction: column; }
    .library { width: 100%; }
    .lib-list { flex-direction: row; flex-wrap: wrap; max-height: 180px; }
    .lib-item { flex: 0 0 auto; }
    .preview-strip { grid-template-columns: repeat(4, 1fr); }
  }
</style>
