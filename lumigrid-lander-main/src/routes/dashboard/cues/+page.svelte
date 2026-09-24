<script lang="ts">
  import { onMount } from 'svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import {
    getCueLists,
    getCueList,
    saveCueList,
    deleteCueList,
    controlCue,
    getCueStatus,
    createDefaultCue,
    createDefaultCueList,
    type CueList,
    type CueListSummary,
    type CueEntry,
    type CueRunMode,
    type CueStatus,
  } from '$lib/api/cues';
  import { getPresets, type ScenePreset } from '$lib/api/node';
  import { TRANSITION_CURVES, type TransitionCurve } from '$lib/runtime/curves';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { getSyncStatus, type SyncStatus } from '$lib/api/sync';
  import { nodeStore, commandTracker } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let cueLists = $state<CueListSummary[]>([]);
  let currentList = $state<CueList | null>(null);
  let selectedListIdx = $state<number>(0);
  let presets = $state<ScenePreset[]>([]);
  let loading = $state(true);
  let saving = $state(false);
  let error = $state('');
  let toast = $state('');
  let deleteListDialogOpen = $state(false);
  let deletingList = $state(false);
  let nodeSyncStatus = $state<SyncStatus | null>(null);

  const isFollower = $derived(
    nodeSyncStatus?.activeRole === 'worker' &&
    Boolean(nodeSyncStatus.masterAddress) &&
    (nodeSyncStatus.lastPacketAgeMs === null || nodeSyncStatus.lastPacketAgeMs < 10000)
  );
  const syncMasterAddress = $derived(nodeSyncStatus?.masterAddress || 'Leader');

  // Live telemetry from nodeStore
  let liveStatus = $derived($nodeStore.status);
  let liveCue = $derived(liveStatus?.cue);

  let isRunning = $derived(liveCue?.running ?? false);
  let cueState = $derived(liveCue?.state ?? 'idle');
  let currentCueIdx = $derived(liveCue?.cueIndex ?? 0);
  let nextCueIdx = $derived(liveCue?.nextCueIndex ?? 0);
  let elapsedMs = $derived(liveCue?.elapsedMs ?? 0);
  let remainingMs = $derived(liveCue?.remainingMs ?? 0);
  let manualFadeValue = $state<number>(0);
  let totalMs = $derived(elapsedMs + remainingMs);
  let progressPct = $derived(totalMs > 0 ? Math.min(100, Math.round((elapsedMs / totalMs) * 100)) : 0);

  const RUN_MODES: { id: CueRunMode; label: string }[] = [
    { id: 'single_shot', label: 'Single Shot' },
    { id: 'loop', label: 'Loop' },
    { id: 'ping_pong', label: 'Ping Pong' },
    { id: 'random', label: 'Random' },
    { id: 'manual', label: 'Manual Crossfade' },
  ];

  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => {
      if (toast === msg) toast = '';
    }, 3000);
  }

  async function loadData() {
    if (!nodeIp) {
      loading = false;
      return;
    }
    loading = true;
    error = '';
    try {
      const [lists, pres, syncSt] = await Promise.all([
        getCueLists(nodeIp).catch(() => []),
        getPresets(nodeIp).catch(() => []),
        getSyncStatus(nodeIp).catch(() => null),
      ]);
      cueLists = lists;
      presets = pres;
      nodeSyncStatus = syncSt;

      if (cueLists.length > 0) {
        await selectList(cueLists[0].index);
      } else {
        currentList = createDefaultCueList(0);
        selectedListIdx = 0;
      }
    } catch (e: any) {
      error = e?.message || 'Failed to load cue lists';
    } finally {
      loading = false;
    }
  }

  async function selectList(index: number) {
    selectedListIdx = index;
    if (!nodeIp) return;
    try {
      currentList = await getCueList(nodeIp, index);
    } catch {
      currentList = createDefaultCueList(index);
    }
  }

  function addCue() {
    if (!currentList || currentList.cues.length >= 16) {
      toastMsg('Maximum 16 cues reached');
      return;
    }
    const nextPreset = presets.length > 0 ? presets[currentList.cues.length % presets.length].index : 0;
    currentList.cues = [
      ...currentList.cues,
      createDefaultCue(nextPreset),
    ];
  }

  function removeCue(idx: number) {
    if (!currentList || currentList.cues.length <= 1) {
      toastMsg('Cue list must contain at least 1 cue');
      return;
    }
    currentList.cues = currentList.cues.filter((_, i) => i !== idx);
  }

  function moveCue(idx: number, direction: 'up' | 'down') {
    if (!currentList) return;
    const targetIdx = direction === 'up' ? idx - 1 : idx + 1;
    if (targetIdx < 0 || targetIdx >= currentList.cues.length) return;
    const items = [...currentList.cues];
    const temp = items[idx];
    items[idx] = items[targetIdx];
    items[targetIdx] = temp;
    currentList.cues = items;
  }

  async function saveCurrentList() {
    if (!nodeIp || !currentList) return;
    saving = true;
    try {
      const res = await saveCueList(nodeIp, currentList.index, currentList);
      if (res.commandId) {
        commandTracker.register({
          id: res.commandId,
          domain: 'cue',
          nodeIp,
        });
      }
      toastMsg(`Saved cue list: ${currentList.name}`);
      const lists = await getCueLists(nodeIp);
      cueLists = lists;
    } catch (e: any) {
      toastMsg(`Save error: ${e?.message || e}`);
    } finally {
      saving = false;
    }
  }

  async function createNewList() {
    if (cueLists.length >= 8) {
      toastMsg('Maximum 8 cue lists allowed');
      return;
    }
    const usedIndices = new Set(cueLists.map(l => l.index));
    let freeIdx = 0;
    for (let i = 0; i < 8; i++) {
      if (!usedIndices.has(i)) {
        freeIdx = i;
        break;
      }
    }
    currentList = createDefaultCueList(freeIdx, `Show ${freeIdx + 1}`);
    selectedListIdx = freeIdx;
    await saveCurrentList();
  }

  function deleteCurrentList() {
    if (!nodeIp || !currentList) return;
    deleteListDialogOpen = true;
  }

  async function confirmDeleteCurrentList() {
    if (!nodeIp || !currentList || deletingList) return;
    deletingList = true;
    try {
      await deleteCueList(nodeIp, currentList.index);
      toastMsg('Cue list deleted');
      cueLists = await getCueLists(nodeIp);
      if (cueLists.length > 0) {
        await selectList(cueLists[0].index);
      } else {
        currentList = createDefaultCueList(0);
        selectedListIdx = 0;
      }
      deleteListDialogOpen = false;
    } catch (e: any) {
      toastMsg(`Delete failed: ${e?.message || e}`);
    } finally {
      deletingList = false;
    }
  }

  async function sendTransport(action: 'start' | 'stop' | 'pause' | 'resume' | 'next' | 'previous') {
    if (!nodeIp || !currentList) return;
    try {
      const res = await controlCue(nodeIp, {
        action,
        cueListIndex: currentList.index,
      });
      if (res.commandId) {
        commandTracker.register({
          id: res.commandId,
          domain: 'cue',
          nodeIp,
        });
      }
      toastMsg(`Transport: ${action}`);
    } catch (e: any) {
      toastMsg(`Transport error: ${e?.message || e}`);
    }
  }

  async function jumpToCue(cueIdx: number) {
    if (!nodeIp) return;
    try {
      await controlCue(nodeIp, {
        action: 'jump',
        cueIndex: cueIdx,
      });
      toastMsg(`Jumped to cue #${cueIdx + 1}`);
    } catch (e: any) {
      toastMsg(`Jump error: ${e?.message || e}`);
    }
  }

  async function onManualFadeInput(e: Event) {
    if (!nodeIp) return;
    const target = e.target as HTMLInputElement;
    const val = parseFloat(target.value);
    manualFadeValue = val;
    try {
      await controlCue(nodeIp, {
        action: 'crossfade',
        crossfade: val,
      });
    } catch {}
  }

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    loadData();
  });
</script>

<svelte:head><title>Cue Studio — LumiGrid</title></svelte:head>

<div class="cues-container">
  <div class="header-row">
    <div>
      <h1 class="page-title">Cue Studio</h1>
      <p class="page-subtitle">Build timed shows, transitions and repeatable playback across your saved scenes.</p>
    </div>

    {#if toast}
      <div class="toast-pill">{toast}</div>
    {/if}
  </div>

  {#if !nodeIp}
    <div class="empty-state">
      <div class="empty-icon">⚠️</div>
      <p>No device selected. Please select a LumiGrid node on the fleet overview to manage cue lists.</p>
    </div>
  {:else if loading}
    <div class="loading-state">
      <div class="spinner"></div>
      <p>Loading cue workspace…</p>
    </div>
  {:else}
    {#if isFollower}
      <div class="sync-follower-banner" role="alert">
        <span class="banner-icon">ℹ</span>
        <div class="banner-content">
          <strong>Playback Controlled by Sync Leader ({syncMasterAddress})</strong>
          <p>This node is an active follower in a synchronized cluster. Local cue transport and manual crossfade on this follower node are suppressed to preserve cluster frame lock. To run cue lists across the group, use Sync Studio.</p>
        </div>
        <a href="/dashboard/sync" class="banner-action-btn">Open Sync Studio →</a>
      </div>
    {/if}

    <!-- LIVE SHOW MONITOR -->
    <div class="cue-surface monitor-card">
      <div class="monitor-header">
        <div class="status-indicator">
          <span class="status-dot" class:running={isRunning} class:holding={cueState === 'holding'} class:fading={cueState === 'fading'}></span>
          <span class="status-label">{cueState.toUpperCase()}</span>
          {#if isRunning}
            <span class="active-list-badge">List #{(liveCue?.listIndex ?? 0) + 1}</span>
          {/if}
        </div>

        <div class="cue-indices">
          <span class="cue-idx current">CUE #{currentCueIdx + 1}</span>
          <span class="cue-arrow">➔</span>
          <span class="cue-idx next">NEXT #{nextCueIdx + 1}</span>
        </div>
      </div>

      <!-- Live Progress Bar -->
      <div class="progress-container">
        <div class="progress-bar" style="width: {progressPct}%"></div>
        <div class="progress-text">
          <span>{(elapsedMs / 1000).toFixed(1)}s</span>
          <span class="progress-state">{cueState}</span>
          <span>{(remainingMs / 1000).toFixed(1)}s left</span>
        </div>
      </div>

      <!-- Transport Controls -->
      <div class="transport-row">
        <div class="transport-buttons">
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('start')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Start Show"}>
            ▶ START
          </button>
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('pause')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Pause Show"}>
            ⏸ PAUSE
          </button>
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('resume')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Resume Show"}>
            ⏯ RESUME
          </button>
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('stop')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Stop Show"}>
            ⏹ STOP
          </button>
          <div class="btn-divider"></div>
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('previous')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Previous Cue"}>
            ⏮ PREV
          </button>
          <button class="btn-transport" disabled={isFollower} onclick={() => sendTransport('next')} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Next Cue"}>
            ⏭ NEXT
          </button>
        </div>

        <!-- Manual Crossfade T-Bar -->
        <div class="fader-box">
          <label for="manual-fader" class="fader-label">Manual Crossfade (T-Bar)</label>
          <input
            id="manual-fader"
            type="range"
            min="0"
            max="1"
            step="0.01"
            value={manualFadeValue}
            disabled={isFollower}
            oninput={onManualFadeInput}
            class="tbar-slider"
          />
          <div class="fader-scale">
            <span>A</span>
            <span>{Math.round(manualFadeValue * 100)}%</span>
            <span>B</span>
          </div>
        </div>
      </div>
    </div>

    <!-- CUE LIST MANAGEMENT -->
    <div class="studio-grid">
      <!-- Sidebar / List selector -->
      <div class="cue-surface lists-sidebar">
        <div class="sidebar-header">
          <h2>Cue Lists ({cueLists.length}/8)</h2>
          <button class="btn-icon" onclick={createNewList} title="New cue list" aria-label="New cue list">+</button>
        </div>

        <div class="lists-nav">
          {#each cueLists as cl}
            <button
              class="list-nav-item"
              class:selected={cl.index === selectedListIdx}
              aria-pressed={cl.index === selectedListIdx}
              onclick={() => selectList(cl.index)}
            >
              <div class="list-nav-info">
                <span class="list-nav-name">{cl.name || `List ${cl.index + 1}`}</span>
                <span class="list-nav-meta">{cl.mode} • {cl.count} cues</span>
              </div>
              <span class="list-nav-arrow">›</span>
            </button>
          {/each}
        </div>
      </div>

      <!-- Main Cue List Editor -->
      {#if currentList}
        <div class="cue-surface editor-main">
          <div class="editor-header">
            <div class="editor-title-group">
              <input
                type="text"
                bind:value={currentList.name}
                class="title-input"
                placeholder="Show Name"
              />
              <select bind:value={currentList.mode} class="select-mode">
                {#each RUN_MODES as rm}
                  <option value={rm.id}>{rm.label}</option>
                {/each}
              </select>
              <select
                bind:value={currentList.endAction}
                class="select-mode select-end-action"
                title="What happens when the cue list reaches its last cue"
              >
                <option value={0}>End: Hold last scene</option>
                <option value={1}>End: Blackout</option>
                <option value={2}>End: Restore previous scene</option>
              </select>
            </div>

            <div class="editor-actions">
              <button class="btn-danger" onclick={deleteCurrentList}>Delete</button>
              <button class="btn-save" disabled={saving} onclick={saveCurrentList}>
                {saving ? 'Saving...' : 'Save Show'}
              </button>
            </div>
          </div>

          <!-- Cues Table -->
          <div class="cues-table-wrapper">
            <table class="cues-table">
              <thead>
                <tr>
                  <th style="width: 40px">#</th>
                  <th>Label</th>
                  <th>Scene</th>
                  <th>Hold Time</th>
                  <th>Fade Time</th>
                  <th>Curve</th>
                  <th style="width: 120px">Actions</th>
                </tr>
              </thead>
              <tbody>
                {#each currentList.cues as cue, idx}
                  <tr class="cue-row" class:active-row={isRunning && currentCueIdx === idx}>
                    <td class="cue-num" data-label="Cue">
                      <button class="jump-btn" disabled={isFollower} onclick={() => jumpToCue(idx)} title={isFollower ? `Playback controlled by Sync Leader (${syncMasterAddress})` : "Jump directly to this cue"}>
                        {idx + 1}
                      </button>
                    </td>
                    <td data-label="Label">
                      <input type="text" bind:value={cue.label} class="table-input" />
                    </td>
                    <td data-label="Scene">
                      <select bind:value={cue.sceneIndex} class="table-select">
                        {#each presets as p}
                          <option value={p.index}>{p.name} (#{p.index + 1})</option>
                        {/each}
                        {#if presets.length === 0}
                          <option value={cue.sceneIndex}>Scene #{cue.sceneIndex + 1}</option>
                        {/if}
                      </select>
                    </td>
                    <td data-label="Hold">
                      <div class="time-input-group">
                        <input
                          type="number"
                          min="0"
                          max="600000"
                          step="100"
                          bind:value={cue.holdMs}
                          class="table-number"
                        />
                        <span class="unit">ms</span>
                      </div>
                    </td>
                    <td data-label="Fade">
                      <div class="time-input-group">
                        <input
                          type="number"
                          min="0"
                          max="30000"
                          step="50"
                          bind:value={cue.fadeMs}
                          class="table-number"
                        />
                        <span class="unit">ms</span>
                      </div>
                    </td>
                    <td data-label="Curve">
                      <select bind:value={cue.curve} class="table-select">
                        {#each TRANSITION_CURVES as c}
                          <option value={c}>{c}</option>
                        {/each}
                      </select>
                    </td>
                    <td class="cue-actions" data-label="Actions">
                      <button class="icon-btn" onclick={() => moveCue(idx, 'up')} disabled={idx === 0} title="Move cue up" aria-label="Move cue up">▲</button>
                      <button class="icon-btn" onclick={() => moveCue(idx, 'down')} disabled={idx === currentList.cues.length - 1} title="Move cue down" aria-label="Move cue down">▼</button>
                      <button class="icon-btn remove" onclick={() => removeCue(idx)} title="Remove cue" aria-label="Remove cue">✕</button>
                    </td>
                  </tr>
                {/each}
              </tbody>
            </table>
          </div>

          <div class="editor-footer">
            <button class="btn-add-cue" onclick={addCue} disabled={currentList.cues.length >= 16}>
              + Add Cue ({currentList.cues.length}/16)
            </button>
          </div>
        </div>
      {/if}
    </div>
  {/if}
</div>

<OperatorDialog
  open={deleteListDialogOpen}
  title="Delete cue list?"
  description={currentList
    ? `Delete “${currentList.name}” and its ${currentList.cues.length} cue${currentList.cues.length === 1 ? '' : 's'} from the selected node.`
    : ''}
  confirmLabel="Delete Cue List"
  tone="danger"
  busy={deletingList}
  onconfirm={confirmDeleteCurrentList}
  oncancel={() => (deleteListDialogOpen = false)}
/>

<style>
  .cues-container {
    width: 100%;
    max-width: 1320px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    color: var(--dash-text);
  }

  .header-row {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 1rem;
    flex-wrap: wrap;
  }

  .page-title {
    font-size: 1.8rem;
    font-weight: 700;
    color: #ffffff;
    margin: 0;
  }

  .page-subtitle {
    font-size: 0.9rem;
    color: rgba(255, 255, 255, 0.5);
    margin: 4px 0 0 0;
  }

  .toast-pill {
    background: rgba(var(--dash-accent-rgb), 0.15);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.4);
    color: var(--dash-accent);
    padding: 6px 14px;
    border-radius: 20px;
    font-size: 0.85rem;
    font-family: monospace;
    animation: fadeIn 0.2s ease;
  }

  .cue-surface {
    background: var(--dash-surface);
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
    box-shadow: var(--dash-shadow);
  }

  /* Monitor Card */
  .monitor-card {
    position: sticky;
    top: 0;
    z-index: 18;
    background: linear-gradient(180deg, rgba(29,29,31,.92) 0%, rgba(15,15,17,.95) 100%);
    backdrop-filter: blur(22px);
    -webkit-backdrop-filter: blur(22px);
    border: 1px solid var(--dash-border);
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  .monitor-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .status-indicator {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .status-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #636366;
    display: inline-block;
  }

  .status-dot.running {
    background: var(--dash-ok);
    box-shadow: 0 0 10px var(--dash-ok);
  }

  .status-dot.holding {
    background: var(--dash-accent-hover);
    box-shadow: 0 0 10px var(--dash-accent-hover);
  }

  .status-dot.fading {
    background: var(--dash-accent);
    box-shadow: 0 0 10px var(--dash-accent);
  }

  .status-label {
    font-family: monospace;
    font-size: 0.95rem;
    font-weight: 700;
    color: #fff;
    letter-spacing: 0.05em;
  }

  .active-list-badge {
    font-family: monospace;
    font-size: 0.75rem;
    color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.1);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    padding: 2px 6px;
    border-radius: 4px;
  }

  .cue-indices {
    display: flex;
    align-items: center;
    gap: 12px;
    font-family: monospace;
    font-weight: 700;
  }

  .cue-idx.current {
    color: var(--dash-accent);
    font-size: 1.1rem;
  }

  .cue-arrow {
    color: rgba(255, 255, 255, 0.3);
  }

  .cue-idx.next {
    color: rgba(255, 255, 255, 0.6);
    font-size: 0.95rem;
  }

  .progress-container {
    height: 22px;
    background: rgba(0, 0, 0, 0.4);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 6px;
    position: relative;
    overflow: hidden;
  }

  .progress-bar {
    height: 100%;
    background: linear-gradient(90deg, var(--dash-accent) 0%, var(--dash-accent-hover) 100%);
    transition: width 0.1s linear;
  }

  .progress-text {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0 10px;
    font-family: monospace;
    font-size: 0.75rem;
    color: #fff;
    text-shadow: 0 1px 2px rgba(0, 0, 0, 0.8);
  }

  .progress-state {
    font-weight: 600;
    text-transform: uppercase;
  }

  .transport-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 20px;
    flex-wrap: wrap;
  }

  .transport-buttons {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .btn-transport {
    min-height: 36px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    color: #fff;
    padding: 8px 14px;
    border-radius: 6px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .btn-transport:hover {
    background: rgba(var(--dash-accent-rgb), 0.15);
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }

  .btn-divider {
    width: 1px;
    height: 20px;
    background: rgba(255, 255, 255, 0.15);
    margin: 0 4px;
  }

  .fader-box {
    display: flex;
    flex-direction: column;
    gap: 4px;
    min-width: 220px;
  }

  .fader-label {
    font-size: 0.75rem;
    text-transform: uppercase;
    color: rgba(255, 255, 255, 0.5);
    letter-spacing: 0.04em;
  }

  .tbar-slider {
    accent-color: var(--dash-accent);
    cursor: pointer;
  }

  .fader-scale {
    display: flex;
    justify-content: space-between;
    font-family: monospace;
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.4);
  }

  /* Studio Grid */
  .studio-grid {
    display: grid;
    grid-template-columns: minmax(220px, 270px) minmax(0, 1fr);
    gap: 1.25rem;
    align-items: start;
  }

  .lists-sidebar {
    position: sticky;
    top: 148px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .sidebar-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .sidebar-header h2 {
    font-size: 1rem;
    font-weight: 600;
    margin: 0;
  }

  .btn-icon {
    background: rgba(var(--dash-accent-rgb), 0.1);
    border: 1px solid rgba(var(--dash-accent-rgb), 0.3);
    color: var(--dash-accent);
    width: 26px;
    height: 26px;
    border-radius: 4px;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    font-weight: bold;
    line-height: 1;
  }

  .lists-nav {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .list-nav-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 6px;
    padding: 10px 12px;
    color: #fff;
    cursor: pointer;
    text-align: left;
    transition: all 0.15s ease;
  }

  .list-nav-item:hover {
    background: rgba(255, 255, 255, 0.07);
  }

  .list-nav-item.selected {
    background: linear-gradient(90deg, rgba(var(--dash-accent-rgb),.18), rgba(var(--dash-accent-rgb),.07));
    border-color: rgba(var(--dash-accent-rgb), 0.34);
    box-shadow: inset 3px 0 0 var(--dash-accent);
  }

  .list-nav-info {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .list-nav-name {
    font-size: 0.9rem;
    font-weight: 600;
  }

  .list-nav-meta {
    font-size: 0.75rem;
    color: rgba(255, 255, 255, 0.4);
    font-family: monospace;
  }

  .list-nav-arrow {
    color: rgba(255, 255, 255, 0.2);
    font-size: 1.2rem;
  }

  /* Editor Main */
  .editor-main {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  .editor-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 16px;
    flex-wrap: wrap;
  }

  .editor-title-group {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .title-input {
    background: rgba(255, 255, 255, 0.055);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 6px;
    color: #fff;
    font-size: 1.1rem;
    font-weight: 600;
    padding: 8px 12px;
  }

  .title-input:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }

  .select-mode {
    background: rgba(255, 255, 255, 0.055);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 6px;
    color: #fff;
    padding: 8px 12px;
    font-size: 0.9rem;
  }

  .select-mode:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12);
  }

  .select-end-action {
    border-color: rgba(255, 159, 10, 0.3);
    color: rgba(255, 255, 255, 0.75);
    font-size: 0.82rem;
  }

  .select-end-action:focus-visible {
    border-color: #ff9f0a;
    box-shadow: 0 0 0 3px rgba(255, 159, 10, 0.15);
  }

  .editor-actions {
    display: flex;
    gap: 10px;
  }

  .btn-save {
    background: var(--dash-accent);
    color: #fff;
    border: none;
    font-weight: 700;
    padding: 8px 18px;
    border-radius: 6px;
    cursor: pointer;
    transition: opacity 0.15s ease;
  }

  .btn-save:hover:not(:disabled) {
    background: var(--dash-accent-hover);
    transform: translateY(-1px);
    box-shadow: 0 8px 20px rgba(var(--dash-accent-rgb), .24);
  }
  .btn-save:disabled { opacity: .45; cursor: not-allowed; }

  .btn-danger {
    background: rgba(255, 69, 58, 0.1);
    border: 1px solid rgba(255, 69, 58, 0.4);
    color: var(--dash-danger);
    padding: 8px 14px;
    border-radius: 6px;
    cursor: pointer;
  }

  /* Cues Table */
  .cues-table-wrapper {
    overflow-x: auto;
  }

  .cues-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }

  .cues-table th {
    text-align: left;
    padding: 8px 10px;
    color: rgba(255, 255, 255, 0.5);
    font-size: 0.75rem;
    text-transform: uppercase;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  }

  .cues-table td {
    padding: 8px 10px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
  }

  .cue-row.active-row {
    background: rgba(var(--dash-accent-rgb), 0.08);
  }

  .cue-num {
    font-family: monospace;
  }

  .jump-btn {
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 0.15);
    color: #fff;
    border-radius: 4px;
    width: 26px;
    height: 24px;
    cursor: pointer;
    font-family: monospace;
    font-size: 0.8rem;
  }

  .jump-btn:hover {
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }

  .table-input, .table-select {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 4px;
    color: #fff;
    padding: 6px 8px;
    font-size: 0.85rem;
    width: 100%;
    box-sizing: border-box;
  }

  .table-input:focus-visible, .table-select:focus-visible {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.10);
  }

  .time-input-group:focus-within {
    border-color: var(--dash-accent);
    box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.10);
  }

  .time-input-group {
    display: flex;
    align-items: center;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 4px;
    padding: 0 6px;
  }

  .table-number {
    background: transparent;
    border: none;
    color: #fff;
    padding: 6px 0;
    width: 70px;
  }

  .unit {
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.4);
    font-family: monospace;
  }

  .cue-actions {
    display: flex;
    gap: 4px;
  }

  .icon-btn {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #fff;
    width: 26px;
    height: 24px;
    border-radius: 4px;
    cursor: pointer;
    line-height: 1;
  }

  .icon-btn:hover:not(:disabled) {
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }

  .icon-btn.remove:hover {
    border-color: var(--dash-danger);
    color: var(--dash-danger);
  }

  .icon-btn:disabled {
    opacity: 0.3;
    cursor: not-allowed;
  }

  .editor-footer {
    display: flex;
    justify-content: flex-start;
  }

  .btn-add-cue {
    background: rgba(255, 255, 255, 0.05);
    border: 1px dashed rgba(255, 255, 255, 0.2);
    color: var(--dash-accent);
    padding: 8px 16px;
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 600;
    transition: all 0.15s ease;
  }

  .btn-add-cue:hover:not(:disabled) {
    background: rgba(var(--dash-accent-rgb), 0.1);
    border-color: var(--dash-accent);
  }

  .empty-state, .loading-state {
    text-align: center;
    padding: 60px 20px;
    color: rgba(255, 255, 255, 0.5);
  }

  .empty-icon {
    font-size: 2.5rem;
    margin-bottom: 12px;
  }

  .spinner {
    width: 32px;
    height: 32px;
    border: 3px solid rgba(var(--dash-accent-rgb), 0.2);
    border-top-color: var(--dash-accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin: 0 auto 12px auto;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  @keyframes fadeIn {
    from { opacity: 0; }
    to { opacity: 1; }
  }

  @media (max-width: 980px) {
    .monitor-card { position: static; }
    .studio-grid { grid-template-columns: 1fr; }
    .lists-sidebar { position: static; }
    .lists-sidebar { gap: .65rem; }
    .lists-nav {
      flex-direction: row;
      overflow-x: auto;
      padding-bottom: .2rem;
      scroll-snap-type: x proximity;
    }
    .list-nav-item {
      flex: 0 0 min(240px, 76vw);
      scroll-snap-align: start;
    }
    .transport-row { align-items: stretch; }
    .transport-buttons {
      overflow-x: auto;
      padding-bottom: .15rem;
      flex: 1 1 100%;
    }
    .btn-transport { white-space: nowrap; }
    .fader-box { flex: 1 1 260px; }
  }

  @media (max-width: 768px) {
    .page-title { font-size: 1.45rem; }
    .monitor-card { padding: 1rem; }
    .monitor-header {
      align-items: flex-start;
      gap: .75rem;
      flex-direction: column;
    }
    .cue-indices { width: 100%; justify-content: space-between; }
    .progress-text { font-size: .68rem; }
    .editor-header { align-items: stretch; }
    .editor-title-group {
      width: 100%;
      align-items: stretch;
      flex-direction: column;
    }
    .title-input, .select-mode { width: 100%; }
    .editor-actions { width: 100%; }
    .editor-actions button { flex: 1; min-height: 42px; }
    .cues-table-wrapper { overflow: visible; }
    .cues-table,
    .cues-table tbody,
    .cues-table tr,
    .cues-table td {
      display: block;
      width: 100%;
    }
    .cues-table thead { display: none; }
    .cues-table {
      min-width: 0;
      border-collapse: separate;
      border-spacing: 0 10px;
    }
    .cues-table tr {
      padding: .75rem;
      border: 1px solid rgba(255,255,255,.075);
      border-radius: 14px;
      background: linear-gradient(180deg, rgba(255,255,255,.035), rgba(255,255,255,.018));
    }
    .cues-table tr.active-row {
      border-color: rgba(var(--dash-accent-rgb),.28);
      background: linear-gradient(135deg, rgba(var(--dash-accent-rgb),.11), rgba(255,255,255,.018));
    }
    .cues-table td {
      display: grid;
      grid-template-columns: 72px minmax(0, 1fr);
      align-items: center;
      gap: .6rem;
      padding: .42rem .15rem;
      border-bottom: 1px solid rgba(255,255,255,.04);
    }
    .cues-table td:last-child { border-bottom: 0; }
    .cues-table td::before {
      content: attr(data-label);
      color: #737378;
      font-size: .61rem;
      font-weight: 800;
      letter-spacing: .06em;
      text-transform: uppercase;
    }
    .cue-actions {
      display: grid;
      grid-template-columns: 72px repeat(3, 38px);
      justify-content: start;
      gap: .45rem;
    }
    .cue-actions::before {
      content: attr(data-label);
      align-self: center;
      color: #737378;
      font-size: .61rem;
      font-weight: 800;
      letter-spacing: .06em;
      text-transform: uppercase;
    }
    .cue-actions .icon-btn {
      width: 38px;
      height: 36px;
    }
    .table-number { width: 100%; }
    .toast-pill { width: 100%; text-align: center; }
  }

  .sync-follower-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    background: rgba(59, 130, 246, 0.12);
    border: 1px solid rgba(59, 130, 246, 0.35);
    border-radius: 8px;
    padding: 0.85rem 1.25rem;
    margin-bottom: 1.25rem;
    color: #e2e8f0;
  }
  .banner-icon {
    font-size: 1.4rem;
    color: #60a5fa;
  }
  .banner-content strong {
    color: #93c5fd;
    display: block;
    font-size: 0.95rem;
    margin-bottom: 0.2rem;
  }
  .banner-content p {
    margin: 0;
    font-size: 0.82rem;
    color: #94a3b8;
  }
  .banner-action-btn {
    white-space: nowrap;
    padding: 0.45rem 0.9rem;
    background: rgba(59, 130, 246, 0.25);
    border: 1px solid rgba(59, 130, 246, 0.5);
    color: #93c5fd;
    border-radius: 6px;
    text-decoration: none;
    font-size: 0.82rem;
    font-weight: 600;
    transition: all 0.15s ease;
  }
  .banner-action-btn:hover {
    background: rgba(59, 130, 246, 0.45);
    color: #ffffff;
  }
</style>
