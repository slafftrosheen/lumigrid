<script lang="ts">
  /**
   * Cue Studio v2 (LG-034).
   *
   * Transport goes exclusively through /api/v1/show/control (startCueList,
   * stopCue, …). Telemetry comes from the focused nodeStore RuntimeSnapshot.
   * /api/v1/cue-list remains the document persistence API.
   */
  import { onMount } from 'svelte';
  import { beforeNavigate, goto } from '$app/navigation';
  import { get } from 'svelte/store';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import CueListCard from '$lib/components/cues/CueListCard.svelte';
  import CueCard from '$lib/components/cues/CueCard.svelte';
  import CueInspector from '$lib/components/cues/CueInspector.svelte';
  import CueTimelineRibbon from '$lib/components/cues/CueTimelineRibbon.svelte';
  import CueRuntimeMonitor from '$lib/components/cues/CueRuntimeMonitor.svelte';
  import ManualCueTBar from '$lib/components/cues/ManualCueTBar.svelte';
  import CueConflictDialog from '$lib/components/cues/CueConflictDialog.svelte';
  import { cueEditorStore, isDirty } from '$lib/cues/editorStore';
  import {
    MAX_CUES_PER_LIST,
    MAX_CUE_LISTS,
    createDefaultCueList,
    cueListNominalDurationMs,
    validateCueList,
    hasBlockingIssues,
    exportCueList,
    importCueList,
    downloadJson,
    type CueListDocument,
    type CueRunMode,
    type CueEndAction,
    type ImportResult,
  } from '$lib/cues/document';
  import { moveCue, copyCue, pasteAfter } from '$lib/cues/clipboard';
  import { interpolateElapsed } from '$lib/cues/timing';
  import {
    getCueLists,
    getCueList,
    saveCueList,
    deleteCueList,
    toDocument,
    CueListConflictError,
    type CueListSummary,
  } from '$lib/api/cues';
  import {
    startCueList,
    stopCue,
    pauseCue,
    resumeCue,
    nextCue,
    previousCue,
    jumpCue,
    restartCueList,
    setCueCrossfade,
  } from '$lib/api/showTransport';
  import { getPresets, type ScenePreset } from '$lib/api/node';
  import { nodeStore, commandTracker } from '$lib/state/nodeStore';
  import { getSelectedNodeIp } from '$lib/auth/storage';

  let loading = $state(true);
  let saving = $state(false);
  let deleting = $state(false);
  let error = $state('');
  let toast = $state('');
  let cueLists = $state<CueListSummary[]>([]);
  let presets = $state<ScenePreset[]>([]);

  // Dialog state
  let deleteDialogOpen = $state(false);
  let newSlotChooserOpen = $state(false);
  let navGuardOpen = $state(false);
  let pendingNav = $state<(() => void) | null>(null);
  let importInput: HTMLInputElement | null = $state(null);

  const editor = cueEditorStore;

  // Subscribe to editor store
  let editorSnapshot = $state<{
    nodeId: string | null;
    listIndex: number | null;
    persisted: CueListDocument | null;
    working: CueListDocument | null;
    selectedCue: number | null;
    saving: boolean;
    conflict: { currentChecksum: number } | null;
    error: string | null;
  } | null>(null);

  editor.subscribe((value) => {
    editorSnapshot = value;
  });

  let working = $derived(editorSnapshot?.working ?? null);
  let dirty = $derived(editorSnapshot ? isDirty(editorSnapshot) : false);
  let selectedCue = $derived(editorSnapshot?.selectedCue ?? null);

  // Focused node subscription (§63) — reactive to node switches like Scenes/Timeline.
  let nodeIp = $state('');
  let focusedIp = $derived($nodeStore.route ?? '');
  let connection = $derived($nodeStore.connection);
  let connectionOk = $derived(connection === 'online' || connection === 'degraded');

  const snapSync = $derived($nodeStore.snapshot?.sync);
  const isFollower = $derived(
    snapSync?.activeRole === 'worker' &&
      Boolean(snapSync.masterAddress) &&
      (snapSync.lastPacketAgeMs === null || snapSync.lastPacketAgeMs === undefined || snapSync.lastPacketAgeMs < 10000)
  );
  const syncMasterAddress = $derived(snapSync?.masterAddress || 'Leader');

  // ── Runtime telemetry (authoritative, §21/§58) ──
  let transport = $derived($nodeStore.snapshot?.transport);
  let liveCue = $derived(transport?.cue ?? null);
  let running = $derived(liveCue?.running ?? false);
  let runState = $derived(liveCue?.state ?? 'stopped');
  let pausedFrom = $derived(liveCue?.pausedFrom ?? 'stopped');
  let runtimeListIndex = $derived(liveCue?.listIndex ?? null);
  let runtimeMode = $derived((liveCue?.mode ?? null) as CueRunMode | null);
  let currentCueIdx = $derived(liveCue?.cueIndex ?? 0);
  let nextCueIdx = $derived(liveCue?.nextCueIndex ?? 0);
  let contentStale = $derived(liveCue?.contentStale ?? false);
  let remoteDriven = $derived(liveCue?.remoteDriven ?? false);
  let manualFadeQ15 = $derived(liveCue?.manualFade ?? 0);

  // Interpolated display timing (§58)
  let lastSnapAt = $derived($nodeStore.lastSnapshotAt);
  let displayTiming = $derived.by(() => {
    const raw = {
      elapsedMs: liveCue?.elapsedMs ?? 0,
      remainingMs: liveCue?.remainingMs ?? 0,
    };
    const age = lastSnapAt > 0 ? Date.now() - lastSnapAt : 0;
    return interpolateElapsed(raw.elapsedMs, raw.remainingMs, runState, age, connectionOk) ?? raw;
  });
  let phaseTotalMs = $derived(displayTiming.elapsedMs + displayTiming.remainingMs);

  // T-Bar visibility: Manual mode only (§18)
  let manualModeActive = $derived(
    running && runtimeMode === 'manual'
  );

  let workingIsManual = $derived(working?.mode === 'manual');
  let timingVisible = $derived(!workingIsManual);

  let usedIndices = $derived(cueLists.map((l) => l.index));
  let freeSlots = $derived(
    Array.from({ length: MAX_CUE_LISTS }, (_, i) => i).filter((i) => !usedIndices.includes(i))
  );

  let blockingIssues = $derived(
    working ? validateCueList(working, (idx) => presets.some((p) => p.index === idx)) : null
  );
  let canSave = $derived(
    Boolean(working && nodeIp && !saving && blockingIssues && !hasBlockingIssues(blockingIssues))
  );

  function toastMsg(msg: string) {
    toast = msg;
    setTimeout(() => {
      if (toast === msg) toast = '';
    }, 3000);
  }

  // ── Data loading ──

  async function loadData(keepSelection = true) {
    if (!nodeIp) {
      loading = false;
      return;
    }
    loading = true;
    error = '';
    try {
      const [lists, pres] = await Promise.all([
        getCueLists(nodeIp).catch(() => [] as CueListSummary[]),
        getPresets(nodeIp).catch(() => [] as ScenePreset[]),
      ]);
      cueLists = lists;
      presets = pres;

      const wanted = keepSelection ? editorSnapshot?.listIndex : null;
      const target =
        wanted !== null && lists.some((l) => l.index === wanted)
          ? wanted
          : lists.length > 0
            ? lists[0].index
            : null;

      if (target !== null) {
        await selectList(target);
      } else if (!working) {
        // Show an empty draft instead of auto-saving a default list (§45).
        editor.openDraft(createDefaultCueList(freeSlots[0] ?? 0, 'New Cue List', presets[0]?.index ?? 0));
      }
    } catch (e: unknown) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  }

  async function selectList(index: number) {
    if (!nodeIp) return;
    if (editorSnapshot && isDirty(editorSnapshot)) {
      pendingNav = () => void doSelectList(index);
      navGuardOpen = true;
      return;
    }
    await doSelectList(index);
  }

  async function doSelectList(index: number) {
    if (!nodeIp) return;
    try {
      const doc = toDocument(await getCueList(nodeIp, index));
      editor.load(doc);
    } catch {
      editor.openDraft(createDefaultCueList(index, `Cue List ${index + 1}`, presets[0]?.index ?? 0));
    }
  }

  // ── Draft mutations ──

  function mutate(mutator: (doc: CueListDocument) => CueListDocument) {
    editor.updateWorking(mutator);
  }

  function addCue() {
    if (!working) return;
    if (working.cues.length >= MAX_CUES_PER_LIST) {
      toastMsg('Maximum 16 cues reached');
      return;
    }
    const scene = presets[selectedCue ?? 0]?.index ?? presets[0]?.index ?? 0;
    mutate((doc) => {
      const cues = [...doc.cues];
      cues.splice((selectedCue ?? cues.length - 1) + 1, 0, {
        sceneIndex: scene,
        holdMs: 3000,
        fadeMs: 1000,
        curve: 'easeInOut',
        label: '',
      });
      return { ...doc, cues };
    });
    editor.selectCue(Math.min((selectedCue ?? working.cues.length - 1) + 1, MAX_CUES_PER_LIST - 1));
  }

  function duplicateSelectedCue() {
    if (!working || selectedCue === null) return;
    if (working.cues.length >= MAX_CUES_PER_LIST) {
      toastMsg('Maximum 16 cues reached');
      return;
    }
    mutate((doc) => {
      const cues = [...doc.cues];
      cues.splice(selectedCue + 1, 0, { ...cues[selectedCue] });
      return { ...doc, cues };
    });
    editor.selectCue(selectedCue + 1);
  }

  function deleteSelectedCue() {
    if (!working || selectedCue === null) return;
    if (working.cues.length <= 1) {
      toastMsg('Cue list must contain at least 1 cue');
      return;
    }
    mutate((doc) => ({ ...doc, cues: doc.cues.filter((_, i) => i !== selectedCue) }));
    editor.selectCue(null);
  }

  function moveSelectedCue(direction: 'up' | 'down') {
    if (!working || selectedCue === null) return;
    const target = direction === 'up' ? selectedCue - 1 : selectedCue + 1;
    const moved = moveCue(working.cues, selectedCue, target);
    if (moved) {
      mutate((doc) => ({ ...doc, cues: moved }));
      editor.selectCue(target);
    }
  }

  function copySelectedCue() {
    if (!working || selectedCue === null) return;
    copyCue(working.cues[selectedCue]);
    toastMsg('Cue copied');
  }

  function pasteAfterSelected() {
    if (!working || selectedCue === null) return;
    const pasted = pasteAfter(working.cues, selectedCue, MAX_CUES_PER_LIST);
    if (!pasted) {
      toastMsg(working.cues.length >= MAX_CUES_PER_LIST ? 'Maximum 16 cues reached' : 'Clipboard empty');
      return;
    }
    mutate((doc) => ({ ...doc, cues: pasted }));
    editor.selectCue(selectedCue + 1);
  }

  // ── Save / run workflow (§39/§40) ──

  async function persistDraft(): Promise<boolean> {
    if (!working || !nodeIp) return false;
    editor.setSaving(true);
    try {
      const expected = editorSnapshot?.persisted?.checksum ?? null;
      const res = await saveCueList(nodeIp, working, expected);
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp });
      }
      // §38: only clear dirty after receiving the authoritative checksum.
      editor.commitSave({ ...working, checksum: res.checksum ?? working.checksum });
      cueLists = await getCueLists(nodeIp).catch(() => cueLists);
      return true;
    } catch (e: unknown) {
      if (e instanceof CueListConflictError) {
        editor.setConflict(e.currentChecksum);
      } else {
        editor.setError(e instanceof Error ? e.message : String(e));
        toastMsg(`Save failed: ${e instanceof Error ? e.message : e}`);
      }
      return false;
    } finally {
      editor.setSaving(false);
    }
  }

  async function saveDraft() {
    await persistDraft();
  }

  /** Primary action for dirty lists (§40): SAVE & RUN / SAVE & RESTART. */
  async function saveAndRun() {
    if (!await persistDraft()) return;
    await runShow();
  }

  async function runShow() {
    if (!nodeIp || !working) return;
    try {
      const res = await startCueList(nodeIp, working.index, 0);
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp });
      }
      toastMsg(`Running “${working.name}”`);
    } catch (e: unknown) {
      toastMsg(`Run failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  /** RUN SAVED VERSION (§40 secondary): never persists the browser draft. */
  async function runSavedVersion() {
    if (!nodeIp || !working) return;
    try {
      const res = await startCueList(nodeIp, working.index, 0);
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp });
      }
      toastMsg('Running saved version');
    } catch (e: unknown) {
      toastMsg(`Run failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  /** RESTART with the newest saved version (§13/§K). */
  async function restartShow() {
    if (!nodeIp) return;
    const listIdx = runtimeListIndex ?? working?.index ?? 0;
    try {
      const res = await restartCueList(nodeIp, listIdx, 0);
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp });
      }
      toastMsg('Restarted with saved version');
    } catch (e: unknown) {
      toastMsg(`Restart failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Transport (canonical /show/control only) ──

  async function transportAction(fn: (ip: string) => Promise<unknown>, label: string) {
    if (!nodeIp || isFollower) return;
    try {
      await fn(nodeIp);
      toastMsg(label);
    } catch (e: unknown) {
      // Transport failures surface; follower/authority errors are real errors (§16).
      toastMsg(`${label} failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  async function sendCrossfade(unit: number) {
    if (!nodeIp) return;
    const res = await setCueCrossfade(nodeIp, unit);
    if (res.commandId) {
      commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp });
    }
  }

  async function jumpToSelected() {
    if (!nodeIp || selectedCue === null) return;
    await transportAction((ip) => jumpCue(ip, selectedCue), `Jumped to cue ${selectedCue + 1}`);
  }

  // ── New / duplicate / delete / import / export ──

  function openNewSlotChooser() {
    if (freeSlots.length === 0) {
      toastMsg(`All ${MAX_CUE_LISTS} cue list slots are in use`);
      return;
    }
    newSlotChooserOpen = true;
  }

  function createInSlot(index: number) {
    newSlotChooserOpen = false;
    // Browser draft only — no node write until explicit save (§45).
    editor.openDraft(createDefaultCueList(index, `Cue List ${index + 1}`, presets[0]?.index ?? 0));
  }

  function duplicateCurrentList() {
    if (!working || !editorSnapshot) return;
    const dup = (
      editorSnapshot.persisted ?? working
    );
    const used = new Set(usedIndices);
    if (used.size >= MAX_CUE_LISTS) {
      toastMsg(`All ${MAX_CUE_LISTS} slots are in use`);
      return;
    }
    let free = 0;
    while (used.has(free)) free++;
    const draft = {
      ...dup,
      schema: 2 as const,
      index: free,
      checksum: null,
      name:
        `${dup.name || `Cue List ${dup.index + 1}`} copy`.slice(0, 31),
      cues: dup.cues.map((c) => ({ ...c })),
    };
    editor.openDraft(draft);
    toastMsg('Duplicate created — save to persist');
  }

  function requestDeleteList() {
    if (!working) return;
    deleteDialogOpen = true;
  }

  async function confirmDeleteList() {
    if (!nodeIp || !working || deleting) return;
    deleting = true;
    try {
      await deleteCueList(nodeIp, working.index);
      toastMsg('Cue list deleted');
      deleteDialogOpen = false;
      cueLists = await getCueLists(nodeIp).catch(() => cueLists);
      if (cueLists.length > 0) {
        await doSelectList(cueLists[0].index);
      } else {
        editor.openDraft(createDefaultCueList(0, 'New Cue List', presets[0]?.index ?? 0));
      }
    } catch (e: unknown) {
      // 409 cue_list_in_use surfaces references from the node (§41).
      const message = e instanceof Error ? e.message : String(e);
      if (message.includes('cue_list_in_use')) {
        toastMsg('Cannot delete: list is running or referenced by a schedule');
      } else {
        toastMsg(`Delete failed: ${message}`);
      }
    } finally {
      deleting = false;
    }
  }

  function onExport() {
    if (!working) return;
    downloadJson(
      `${(working.name || 'cue-list').replace(/[^a-z0-9_-]+/gi, '_')}.json`,
      exportCueList(working)
    );
  }

  function onImportClick() {
    importInput?.click();
  }

  async function onImportFile(event: Event) {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    try {
      const json: unknown = JSON.parse(await file.text());
      const result: ImportResult = importCueList(json, freeSlots[0] ?? 0);
      if (result.ok === false) {
        toastMsg(`Import failed: ${result.error}`);
        return;
      }
      editor.openDraft(result.doc);
      toastMsg('Imported as unsaved draft');
    } catch {
      toastMsg('Import failed: invalid JSON');
    }
  }

  // ── Unsaved navigation guard (§62) ──

  beforeNavigate((navigation) => {
    const snapshot = get(cueEditorStore);
    if (snapshot && isDirty(snapshot)) {
      navigation.cancel();
      const target = navigation.to?.url?.pathname ?? '/dashboard/cues';
      pendingNav = () => void goto(target);
      navGuardOpen = true;
    }
  });

  async function navigateAfterDiscard(path: string) {
    await goto(path);
  }

  function navStay() {
    navGuardOpen = false;
    pendingNav = null;
  }

  function navDiscard() {
    navGuardOpen = false;
    const nav = pendingNav;
    pendingNav = null;
    if (editorSnapshot?.persisted) {
      editor.load(editorSnapshot.persisted);
    } else {
      editor.reset();
    }
    nav?.();
  }

  void navigateAfterDiscard;

  // ── Derived labels ──

  function sceneLabel(index: number | undefined | null): string {
    if (index === undefined || index === null) return '—';
    return presets.find((p) => p.index === index)?.name ?? `Scene #${index + 1}`;
  }

  let currentCueLabel = $derived(sceneLabel(liveCue?.currentSceneIndex));
  let nextCueLabel = $derived(sceneLabel(liveCue?.nextSceneIndex));
  let runningListName = $derived(
    cueLists.find((l) => l.index === runtimeListIndex)?.name ?? `Cue List ${(runtimeListIndex ?? 0) + 1}`
  );

  let ribbonCues = $derived(
    (working?.cues ?? []).map((c) => ({ fadeMs: c.fadeMs, holdMs: c.holdMs }))
  );

  onMount(() => {
    nodeIp = getSelectedNodeIp();
    editor.bindNode(nodeIp || 'none');
    loadData();
  });

  // React to focused node changes (§63): reload when the dashboard switches node.
  $effect(() => {
    if (focusedIp && nodeIp && focusedIp !== nodeIp) {
      nodeIp = focusedIp;
      editor.bindNode(focusedIp);
      loadData(false);
    }
  });
</script>

<svelte:head><title>Cue Studio — LumiGrid</title></svelte:head>

<input
  bind:this={importInput}
  type="file"
  accept="application/json,.json"
  style="display: none"
  onchange={onImportFile}
/>

<div class="cues-container">
  <div class="header-row">
    <div>
      <h1 class="page-title">Cue Studio</h1>
      <p class="page-subtitle">Storyboard timed shows over your saved scenes — millisecond-accurate playback.</p>
    </div>
    {#if toast}
      <div class="toast-pill" role="status">{toast}</div>
    {/if}
  </div>

  {#if !nodeIp}
    <div class="empty-state">
      <div class="empty-icon">⚠️</div>
      <p>No device selected. Select a LumiGrid node on the fleet overview to manage cue lists.</p>
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
          <strong>Playback controlled by Sync Leader ({syncMasterAddress})</strong>
          <p>You may edit and save Cue Lists here, but transport control belongs to the leader. Use Sync Studio to run shows across the cluster.</p>
        </div>
        <a href="/dashboard/sync" class="banner-action-btn">Open Sync Studio →</a>
      </div>
    {/if}

    <!-- LIVE SHOW MONITOR -->
    <CueRuntimeMonitor
      listName={runningListName}
      {running}
      state={runState}
      {pausedFrom}
      currentCueIndex={currentCueIdx}
      cueCount={liveCue ? Math.max(liveCue.cueIndex + 1, cueLists.find((l) => l.index === runtimeListIndex)?.count ?? 0) : 0}
      currentLabel={currentCueLabel}
      nextLabel={nextCueLabel}
      fading={runState === 'fading'}
      elapsedMs={displayTiming.elapsedMs}
      remainingMs={displayTiming.remainingMs}
      {phaseTotalMs}
      {contentStale}
      {remoteDriven}
      {isFollower}
      manualMode={manualModeActive}
      onstart={() => transportAction((ip) => startCueList(ip, working?.index ?? runtimeListIndex ?? 0), 'Show started')}
      onpause={() => transportAction(pauseCue, 'Paused')}
      onresume={() => transportAction(resumeCue, 'Resumed')}
      onstop={() => transportAction(stopCue, 'Stopped')}
      onnext={() => transportAction(nextCue, 'Next cue')}
      onprevious={() => transportAction(previousCue, 'Previous cue')}
      onrestart={restartShow}
    />

    {#if manualModeActive}
      <div class="cue-surface tbar-surface">
        <ManualCueTBar
          {manualFadeQ15}
          currentLabel={currentCueLabel}
          nextLabel={nextCueLabel}
          currentIndex={currentCueIdx}
          nextIndex={nextCueIdx}
          disabled={isFollower}
          oncrossfade={sendCrossfade}
          onerror={(msg) => toastMsg(msg)}
        />
      </div>
    {/if}

    <div class="studio-grid">
      <!-- LIBRARY -->
      <aside class="cue-surface library">
        <div class="library-header">
          <h2>Cue Lists <span class="count">{cueLists.length}/{MAX_CUE_LISTS}</span></h2>
        </div>

        <div class="library-list">
          {#each cueLists as cl (cl.index)}
            <CueListCard
              index={cl.index}
              name={cl.name}
              mode={cl.mode}
              count={cl.count}
              checksum={cl.checksum ?? null}
              cueMs={[]}
              selected={editorSnapshot?.listIndex === cl.index}
              running={running && runtimeListIndex === cl.index}
              contentStale={running && runtimeListIndex === cl.index && contentStale}
              onselect={() => selectList(cl.index)}
            />
          {/each}
          {#if cueLists.length === 0}
            <p class="library-empty">No saved cue lists yet.</p>
          {/if}
        </div>

        <div class="library-actions">
          <button class="cbtn cbtn-primary" onclick={openNewSlotChooser}>+ New Cue List</button>
          <button class="cbtn" onclick={duplicateCurrentList} disabled={!working}>Duplicate</button>
          <button class="cbtn" onclick={onExport} disabled={!working}>Export</button>
          <button class="cbtn" onclick={onImportClick}>Import</button>
        </div>
      </aside>

      <!-- EDITOR -->
      <section class="cue-surface editor">
        {#if working}
          <div class="editor-head">
            <input
              class="title-input"
              type="text"
              value={working.name}
              maxlength={64}
              placeholder="Show name"
              aria-label="Cue list name"
              oninput={(e) => mutate((doc) => ({ ...doc, name: (e.target as HTMLInputElement).value }))}
            />
            <div class="editor-badges">
              {#if dirty}
                <span class="badge badge-dirty">UNSAVED</span>
              {/if}
              {#if running && runtimeListIndex === working.index && contentStale}
                <span class="badge badge-stale">RUNNING PREVIOUS VERSION</span>
              {/if}
            </div>
          </div>

          <div class="editor-toolbar">
            <button class="cbtn cbtn-primary" disabled={!canSave} onclick={saveDraft}>
              {saving ? 'Saving…' : 'Save'}
            </button>
            {#if dirty}
              <button class="cbtn btn-run" disabled={!canSave || isFollower} onclick={saveAndRun}>
                {running && runtimeListIndex === working.index ? 'SAVE & RESTART' : 'SAVE & RUN'}
              </button>
              <button class="cbtn" disabled={isFollower} onclick={runSavedVersion} title="Run the node's saved version without saving this draft">
                RUN SAVED VERSION
              </button>
            {:else}
              <button class="cbtn btn-run" disabled={isFollower} onclick={runShow}>▶ RUN</button>
            {/if}
            <div class="toolbar-spacer"></div>
            <button class="cbtn btn-danger" onclick={requestDeleteList}>Delete</button>
          </div>

          <CueTimelineRibbon cues={ribbonCues} currentCue={running && runtimeListIndex === working.index ? currentCueIdx : null} />

          <div class="storyboard-scroll">
            <div class="storyboard">
              {#each working.cues as cue, idx (idx)}
                <div class="story-cue">
                  <CueCard
                    index={idx}
                    sceneIndex={cue.sceneIndex}
                    sceneName={sceneLabel(cue.sceneIndex)}
                    label={cue.label}
                    holdMs={cue.holdMs}
                    fadeMs={cue.fadeMs}
                    curve={cue.curve}
                    selected={selectedCue === idx}
                    isCurrent={running && runtimeListIndex === working.index && currentCueIdx === idx}
                    showTiming={timingVisible}
                    scenes={presets.map((p) => ({ index: p.index, name: p.name, thumbnail: null }))}
                    onselect={() => editor.selectCue(idx)}
                    onupdate={(patch) => mutate((doc) => ({
                      ...doc,
                      cues: doc.cues.map((c, i) => (i === idx ? { ...c, ...patch } : c)),
                    }))}
                    onmove={(dir) => {
                      editor.selectCue(idx);
                      const target = dir === 'up' ? idx - 1 : idx + 1;
                      const moved = moveCue(working.cues, idx, target);
                      if (moved) mutate((doc) => ({ ...doc, cues: moved }));
                    }}
                    onduplicate={() => { editor.selectCue(idx); duplicateSelectedCue(); }}
                    oncopy={() => { editor.selectCue(idx); copySelectedCue(); }}
                    onpasteafter={() => { editor.selectCue(idx); pasteAfterSelected(); }}
                    ondelete={() => { editor.selectCue(idx); deleteSelectedCue(); }}
                    onjumpto={running && !isFollower ? () => transportAction((ip) => jumpCue(ip, idx), `Jumped to cue ${idx + 1}`) : undefined}
                    canMoveUp={idx > 0}
                    canMoveDown={idx < working.cues.length - 1}
                  />
                  {#if idx < working.cues.length - 1}
                    <div class="story-arrow" aria-hidden="true">↓</div>
                  {/if}
                </div>
              {/each}
            </div>

            <div class="storyboard-side">
              <div class="cue-surface side-panel">
                <h3>Inspector</h3>
                <CueInspector
                  mode={working.mode}
                  endAction={working.endAction}
                  cueCount={working.cues.length}
                  nominalMs={cueListNominalDurationMs(working)}
                  onmodechange={(m) => mutate((doc) => ({ ...doc, mode: m }))}
                  onendactionchange={(a) => mutate((doc) => ({ ...doc, endAction: a }))}
                />
              </div>

              <div class="cue-surface side-panel">
                <h3>Cue</h3>
                <div class="cue-actions-column">
                  <button class="cbtn" onclick={addCue} disabled={working.cues.length >= MAX_CUES_PER_LIST}>
                    + Add Cue ({working.cues.length}/{MAX_CUES_PER_LIST})
                  </button>
                  <button class="cbtn" onclick={duplicateSelectedCue} disabled={selectedCue === null}>Duplicate Cue</button>
                  <button class="cbtn" onclick={deleteSelectedCue} disabled={selectedCue === null}>Delete Cue</button>
                  {#if running && selectedCue !== null && !isFollower}
                    <button class="cbtn btn-run" onclick={jumpToSelected}>[JUMP TO CUE]</button>
                  {/if}
                  <p class="hint">Select a cue card, then use these actions. Jump is always explicit — clicking a card never jumps.</p>
                </div>
                {#if blockingIssues && blockingIssues.missingScenes.length > 0}
                  <div class="issue-box" role="alert">
                    <strong>Missing scenes</strong>
                    {#each blockingIssues.missingScenes as issue (issue.cueIndex)}
                      <div>Cue {issue.cueIndex + 1} · Scene {issue.sceneIndex + 1} missing</div>
                    {/each}
                    <p class="hint">Assign existing scenes before saving.</p>
                  </div>
                {/if}
              </div>
            </div>
          </div>
        {:else}
          <div class="empty-state">
            <p>Select or create a Cue List to start authoring.</p>
          </div>
        {/if}
      </section>
    </div>
  {/if}
</div>

<OperatorDialog
  open={deleteDialogOpen}
  title="Delete cue list?"
  description={working
    ? `Delete “${working.name}” and its ${working.cues.length} cue${working.cues.length === 1 ? '' : 's'} from the selected node.`
    : ''}
  confirmLabel="Delete Cue List"
  tone="danger"
  busy={deleting}
  onconfirm={confirmDeleteList}
  oncancel={() => (deleteDialogOpen = false)}
/>

<OperatorDialog
  open={newSlotChooserOpen}
  title="New Cue List"
  description="Choose a free slot. The list is created as a browser draft — nothing is saved to the node until you save."
  confirmLabel="Close"
  tone="neutral"
  onconfirm={() => {
    newSlotChooserOpen = false;
  }}
  oncancel={() => (newSlotChooserOpen = false)}
>
  <div class="slot-chooser" role="listbox" aria-label="Available slots">
    {#if freeSlots.length === 0}
      <p class="hint">All slots are in use. Delete a list to free a slot.</p>
    {:else}
      {#each freeSlots as slot (slot)}
        <button class="slot-chip" role="option" aria-selected="false" onclick={() => createInSlot(slot)}>
          {String(slot + 1).padStart(2, '0')}
        </button>
      {/each}
    {/if}
  </div>
</OperatorDialog>

<OperatorDialog
  open={navGuardOpen}
  title="Unsaved Cue List changes"
  description="Your draft has changes that are not saved to the node. Stay to keep editing, or discard them."
  confirmLabel="Discard"
  cancelLabel="Stay"
  tone="danger"
  onconfirm={navDiscard}
  oncancel={navStay}
/>

<CueConflictDialog
  open={editorSnapshot?.conflict !== null && editorSnapshot?.conflict !== undefined}
  currentChecksum={editorSnapshot?.conflict?.currentChecksum ?? null}
  busy={saving}
  onreload={async () => {
    editor.clearConflict();
    if (nodeIp && working) {
      try {
        const doc = toDocument(await getCueList(nodeIp, working.index));
        editor.reloadNodeVersion(doc);
        toastMsg('Loaded node version');
      } catch {
        toastMsg('Failed to reload node version');
      }
    }
  }}
  onkeep={() => editor.keepDraft()}
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
    color: #fff;
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
  .tbar-surface {
    display: flex;
    justify-content: center;
  }
  .studio-grid {
    display: grid;
    grid-template-columns: minmax(230px, 280px) minmax(0, 1fr);
    gap: 1.25rem;
    align-items: start;
  }
  .library {
    display: flex;
    flex-direction: column;
    gap: 12px;
    position: sticky;
    top: 148px;
  }
  .library-header h2 {
    font-size: 1rem;
    font-weight: 600;
    margin: 0;
    display: flex;
    justify-content: space-between;
  }
  .count {
    color: rgba(255, 255, 255, 0.4);
    font-family: monospace;
    font-size: 0.85rem;
  }
  .library-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .library-empty {
    color: rgba(255, 255, 255, 0.4);
    font-size: 0.85rem;
  }
  .library-actions {
    display: flex;
    flex-direction: column;
    gap: 6px;
    margin-top: 4px;
  }
  .editor {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .editor-head {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
  }
  .title-input {
    flex: 1;
    min-width: 200px;
    background: rgba(255, 255, 255, 0.055);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 8px;
    color: #fff;
    font-size: 1.15rem;
    font-weight: 700;
    padding: 8px 12px;
  }
  .editor-badges {
    display: flex;
    gap: 6px;
  }
  .badge {
    font-size: 0.62rem;
    font-weight: 800;
    letter-spacing: 0.06em;
    padding: 3px 8px;
    border-radius: 4px;
    white-space: nowrap;
  }
  .badge-dirty {
    color: #ffd60a;
    background: rgba(255, 214, 10, 0.1);
    border: 1px solid rgba(255, 214, 10, 0.35);
  }
  .badge-stale {
    color: #ffd60a;
    background: rgba(255, 214, 10, 0.1);
    border: 1px solid rgba(255, 214, 10, 0.35);
  }
  .editor-toolbar {
    display: flex;
    gap: 8px;
    align-items: center;
    flex-wrap: wrap;
  }
  .toolbar-spacer {
    flex: 1;
  }
  .cbtn {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.15);
    color: #fff;
    padding: 7px 14px;
    border-radius: 6px;
    font-size: 0.84rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .cbtn:hover:not(:disabled) {
    border-color: var(--dash-accent);
    color: var(--dash-accent);
  }
  .cbtn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .cbtn-primary {
    background: rgba(var(--dash-accent-rgb), 0.16);
    border-color: rgba(var(--dash-accent-rgb), 0.4);
    color: var(--dash-accent);
  }
  .storyboard-scroll {
    overflow-x: auto;
  }
  .storyboard {
    display: flex;
    flex-direction: column;
    min-width: min(560px, 100%);
  }
  .story-cue {
    display: flex;
    flex-direction: column;
  }
  .story-arrow {
    text-align: center;
    color: rgba(255, 255, 255, 0.25);
    padding: 2px 0;
    font-size: 0.9rem;
  }
  .storyboard-side {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-top: 4px;
  }
  .side-panel h3 {
    margin: 0 0 8px 0;
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: rgba(255, 255, 255, 0.55);
  }
  .cue-actions-column {
    display: flex;
    flex-direction: column;
    gap: 6px;
    align-items: stretch;
  }
  .hint {
    margin: 4px 0 0 0;
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.4);
  }
  .issue-box {
    margin-top: 10px;
    background: rgba(255, 69, 58, 0.08);
    border: 1px solid rgba(255, 69, 58, 0.35);
    border-radius: 8px;
    padding: 8px 10px;
    font-size: 0.78rem;
    color: #ffb4a9;
  }
  .slot-chooser {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    padding: 6px 0;
  }
  .slot-chip {
    width: 46px;
    height: 38px;
    border-radius: 8px;
    border: 1px solid rgba(var(--dash-accent-rgb), 0.4);
    background: rgba(var(--dash-accent-rgb), 0.08);
    color: var(--dash-accent);
    font-family: monospace;
    font-weight: 700;
    cursor: pointer;
  }
  .slot-chip:hover {
    background: rgba(var(--dash-accent-rgb), 0.22);
  }
  .empty-state,
  .loading-state {
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
    to {
      transform: rotate(360deg);
    }
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
  }
  @media (max-width: 980px) {
    .studio-grid {
      grid-template-columns: 1fr;
    }
    .library {
      position: static;
    }
    .storyboard-side {
      grid-template-columns: 1fr;
    }
  }
</style>
