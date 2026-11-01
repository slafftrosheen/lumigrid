<script lang="ts">
  /**
   * Cue Studio v2 (LG-034).
   *
   * Transport goes exclusively through /api/v1/show/control (startCueList,
   * stopCue, …). Telemetry comes from the focused nodeStore RuntimeSnapshot.
   * /api/v1/cue-list remains the document persistence API.
   */
  import { onDestroy, onMount } from 'svelte';
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
  import { nodeStore, commandTracker, captureNodeOperation } from '$lib/state/nodeStore';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';

  let loading = $state(true);
  let deleting = $state(false);
  let error = $state('');
  let toast = $state('');
  let cueLists = $state<CueListSummary[]>([]);
  let presets = $state<ScenePreset[]>([]);
  let libraryReady = $state(false);

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

  const unsubscribeEditor = editor.subscribe((value) => {
    editorSnapshot = value;
  });

  let working = $derived(editorSnapshot?.working ?? null);
  let dirty = $derived(editorSnapshot ? isDirty(editorSnapshot) : false);
  let selectedCue = $derived(editorSnapshot?.selectedCue ?? null);
  let saving = $derived(editorSnapshot?.saving ?? false);

  // Focused node subscription (§63) — reactive to node switches like Scenes/Timeline.
  let nodeIp = $state('');
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
    Boolean(working && nodeIp && libraryReady && !saving && !deleting && blockingIssues && !hasBlockingIssues(blockingIssues))
  );

  type NodeOperation = ReturnType<typeof captureNodeOperation>;
  let mounted = false;
  let nodeOwner = $state.raw<NodeOperation | null>(null);
  let nodeSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let selectionGeneration = 0;
  let libraryGeneration = 0;
  let loadingGeneration = 0;
  let toastTimer: ReturnType<typeof setTimeout> | null = null;
  let deleteTarget: { owner: NodeOperation; index: number } | null = null;

  function capturePageOperation(): NodeOperation {
    const owner = captureNodeOperation(nodeIp);
    return { ip: owner.ip, isCurrent: () => mounted && owner.isCurrent() };
  }

  function captureEditorOperation(owner = capturePageOperation()): NodeOperation {
    const selection = selectionGeneration;
    return { ip: owner.ip, isCurrent: () => owner.isCurrent() && selection === selectionGeneration };
  }

  function beginSelection() {
    selectionGeneration++;
    editor.setSaving(false);
    deleting = false;
    deleteDialogOpen = false;
    deleteTarget = null;
  }

  function openDraft(doc: CueListDocument) {
    beginSelection();
    editor.openDraft(doc);
  }

  function toastMsg(msg: string) {
    toast = msg;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      toast = '';
      toastTimer = null;
    }, 3000);
  }

  // ── Data loading ──

  async function loadData(keepSelection = true, owner = capturePageOperation()) {
    if (!owner.isCurrent()) return;
    if (!owner.ip) {
      loading = false;
      return;
    }
    const request = ++libraryGeneration;
    const load = ++loadingGeneration;
    const selection = selectionGeneration;
    const isCurrent = () => owner.isCurrent() && request === libraryGeneration;
    loading = true;
    error = '';
    try {
      const [lists, pres] = await Promise.all([
        getCueLists(owner.ip),
        getPresets(owner.ip),
      ]);
      if (!isCurrent()) return;
      cueLists = lists;
      presets = pres;
      libraryReady = true;

      // A refresh must not replace a draft, or a selection made while loading.
      if (selection !== selectionGeneration || (keepSelection && working)) return;

      const wanted = keepSelection ? editorSnapshot?.listIndex : null;
      const target =
        wanted !== null && lists.some((l) => l.index === wanted)
          ? wanted
          : lists.length > 0
            ? lists[0].index
            : null;

      if (target !== null) {
        await doSelectList(target, owner);
      } else if (!working) {
        // Show an empty draft instead of auto-saving a default list (§45).
        openDraft(createDefaultCueList(freeSlots[0] ?? 0, 'New Cue List', pres[0]?.index ?? 0));
      }
    } catch (e: unknown) {
      if (!isCurrent()) return;
      error = e instanceof Error ? e.message : String(e);
    } finally {
      if (owner.isCurrent() && load === loadingGeneration) loading = false;
    }
  }

  async function selectList(index: number) {
    const node = capturePageOperation();
    const owner = captureEditorOperation(node);
    if (!owner.ip || !owner.isCurrent()) return;
    if (editorSnapshot && isDirty(editorSnapshot)) {
      pendingNav = () => {
        if (owner.isCurrent()) void doSelectList(index, node);
      };
      navGuardOpen = true;
      return;
    }
    await doSelectList(index, node);
  }

  async function doSelectList(index: number, node = capturePageOperation()) {
    if (!node.ip || !node.isCurrent()) return;
    beginSelection();
    const owner = captureEditorOperation(node);
    const draft = get(editor).working;
    error = '';
    try {
      const doc = toDocument(await getCueList(owner.ip, index));
      if (!owner.isCurrent() || get(editor).working !== draft) return;
      // Actions started against the previous visible document while this read
      // was pending must not commit into the newly installed selection.
      beginSelection();
      editor.load(doc);
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      error = e instanceof Error ? e.message : String(e);
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

  async function persistDraft(owner = captureEditorOperation()): Promise<CueListDocument | null> {
    if (!working || !owner.ip || !owner.isCurrent() || saving) return null;
    const submitted = { ...working, cues: working.cues.map((cue) => ({ ...cue })) };
    const expected = editorSnapshot?.persisted?.checksum ?? null;
    editor.setSaving(true);
    editor.setError(null);
    try {
      const res = await saveCueList(owner.ip, submitted, expected);
      if (!owner.isCurrent()) return null;
      if (!res.saved) throw new Error('The node did not save the cue list');
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp: owner.ip });
      }
      // §38: only clear dirty after receiving the authoritative checksum.
      const saved = { ...submitted, checksum: res.checksum ?? submitted.checksum };
      editor.commitSave(saved, submitted);
      const request = ++libraryGeneration;
      try {
        const lists = await getCueLists(owner.ip);
        if (!owner.isCurrent()) return null;
        if (request === libraryGeneration) cueLists = lists;
      } catch (e: unknown) {
        if (!owner.isCurrent()) return null;
        if (request === libraryGeneration) {
          error = `Cue list saved, but the library could not be refreshed: ${e instanceof Error ? e.message : e}`;
        }
      }
      return saved;
    } catch (e: unknown) {
      if (!owner.isCurrent()) return null;
      if (e instanceof CueListConflictError) {
        editor.setConflict(e.currentChecksum);
      } else {
        editor.setError(e instanceof Error ? e.message : String(e));
        toastMsg(`Save failed: ${e instanceof Error ? e.message : e}`);
      }
      return null;
    } finally {
      if (owner.isCurrent()) editor.setSaving(false);
    }
  }

  async function saveDraft() {
    await persistDraft();
  }

  /** Primary action for dirty lists (§40): SAVE & RUN / SAVE & RESTART. */
  async function saveAndRun() {
    const owner = captureEditorOperation();
    if (isFollower) return;
    const saved = await persistDraft(owner);
    if (!saved || !owner.isCurrent() || isFollower) return;
    await startSavedList(owner, saved.index, `Running “${saved.name}”`);
  }

  async function startSavedList(owner: NodeOperation, index: number, label: string) {
    if (!owner.ip || !owner.isCurrent() || isFollower) return;
    try {
      const res = await startCueList(owner.ip, index, 0);
      if (!owner.isCurrent()) return;
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp: owner.ip });
      }
      toastMsg(label);
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      toastMsg(`Run failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  async function runShow() {
    if (!working) return;
    await startSavedList(captureEditorOperation(), working.index, `Running “${working.name}”`);
  }

  /** RUN SAVED VERSION (§40 secondary): never persists the browser draft. */
  async function runSavedVersion() {
    if (!editorSnapshot?.persisted) return;
    await startSavedList(captureEditorOperation(), editorSnapshot.persisted.index, 'Running saved version');
  }

  /** RESTART with the newest saved version (§13/§K). */
  async function restartShow() {
    const owner = capturePageOperation();
    if (!owner.ip || !owner.isCurrent() || isFollower) return;
    const listIdx = runtimeListIndex ?? working?.index ?? 0;
    try {
      const res = await restartCueList(owner.ip, listIdx, 0);
      if (!owner.isCurrent()) return;
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp: owner.ip });
      }
      toastMsg('Restarted with saved version');
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      toastMsg(`Restart failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  // ── Transport (canonical /show/control only) ──

  async function transportAction(fn: (ip: string) => Promise<{ commandId?: number }>, label: string) {
    const owner = capturePageOperation();
    if (!owner.ip || !owner.isCurrent() || isFollower) return;
    try {
      const res = await fn(owner.ip);
      if (!owner.isCurrent()) return;
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp: owner.ip });
      }
      toastMsg(label);
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      // Transport failures surface; follower/authority errors are real errors (§16).
      toastMsg(`${label} failed: ${e instanceof Error ? e.message : e}`);
    }
  }

  async function sendCrossfade(unit: number, owner = capturePageOperation()) {
    if (!owner.ip || !owner.isCurrent() || isFollower) return;
    try {
      const res = await setCueCrossfade(owner.ip, unit);
      if (!owner.isCurrent()) return;
      if (res.commandId) {
        commandTracker.register({ id: res.commandId, domain: 'cue', nodeIp: owner.ip });
      }
    } catch (e: unknown) {
      if (owner.isCurrent()) throw e;
    }
  }

  async function jumpToSelected() {
    if (!nodeIp || selectedCue === null) return;
    const index = selectedCue;
    await transportAction((ip) => jumpCue(ip, index), `Jumped to cue ${index + 1}`);
  }

  // ── New / duplicate / delete / import / export ──

  function openNewSlotChooser() {
    if (!libraryReady) return;
    if (freeSlots.length === 0) {
      toastMsg(`All ${MAX_CUE_LISTS} cue list slots are in use`);
      return;
    }
    newSlotChooserOpen = true;
  }

  function createInSlot(index: number) {
    newSlotChooserOpen = false;
    // Browser draft only — no node write until explicit save (§45).
    openDraft(createDefaultCueList(index, `Cue List ${index + 1}`, presets[0]?.index ?? 0));
  }

  function duplicateCurrentList() {
    if (!working || !editorSnapshot || !libraryReady) return;
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
    openDraft(draft);
    toastMsg('Duplicate created — save to persist');
  }

  function requestDeleteList() {
    if (!working || !editorSnapshot?.persisted) return;
    deleteTarget = { owner: captureEditorOperation(), index: working.index };
    deleteDialogOpen = true;
  }

  async function confirmDeleteList() {
    const target = deleteTarget;
    if (!target || !target.owner.ip || !target.owner.isCurrent() || deleting || saving) return;
    const owner = target.owner;
    const node = capturePageOperation();
    deleting = true;
    try {
      const result = await deleteCueList(owner.ip, target.index);
      if (!owner.isCurrent()) return;
      if (!result.deleted) throw new Error('The node did not delete the cue list');
      toastMsg('Cue list deleted');
      deleteDialogOpen = false;
      cueLists = cueLists.filter((list) => list.index !== target.index);
      // Keep the browser copy recoverable if the following read fails.
      if (working) editor.openDraft({ ...working, checksum: null, cues: working.cues.map((cue) => ({ ...cue })) });
      const draft = get(editor).working;
      const request = ++libraryGeneration;
      const lists = await getCueLists(owner.ip);
      if (!owner.isCurrent() || request !== libraryGeneration) return;
      cueLists = lists;
      if (get(editor).working !== draft) return;
      deleting = false;
      if (lists.length > 0) {
        await doSelectList(lists[0].index, node);
      } else {
        openDraft(createDefaultCueList(0, 'New Cue List', presets[0]?.index ?? 0));
      }
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      // 409 cue_list_in_use surfaces references from the node (§41).
      const message = e instanceof Error ? e.message : String(e);
      if (message.includes('cue_list_in_use')) {
        toastMsg('Cannot delete: list is running or referenced by a schedule');
      } else {
        error = message;
        toastMsg(`${deleteDialogOpen ? 'Delete' : 'Library refresh'} failed: ${message}`);
      }
    } finally {
      if (owner.isCurrent()) deleting = false;
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
    const owner = captureEditorOperation();
    const draft = get(editor).working;
    const slot = freeSlots[0];
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file || !owner.ip || !owner.isCurrent() || !libraryReady) return;
    if (slot === undefined) {
      toastMsg(`All ${MAX_CUE_LISTS} cue list slots are in use`);
      return;
    }
    try {
      const json: unknown = JSON.parse(await file.text());
      if (!owner.isCurrent() || get(editor).working !== draft) return;
      const result: ImportResult = importCueList(json, slot);
      if (result.ok === false) {
        toastMsg(`Import failed: ${result.error}`);
        return;
      }
      openDraft(result.doc);
      toastMsg('Imported as unsaved draft');
    } catch {
      if (!owner.isCurrent()) return;
      toastMsg('Import failed: invalid JSON');
    }
  }

  async function reloadNodeVersion() {
    const owner = captureEditorOperation();
    if (!working || !owner.ip || !owner.isCurrent() || saving) return;
    const index = working.index;
    const draft = get(editor).working;
    editor.setSaving(true);
    try {
      const doc = toDocument(await getCueList(owner.ip, index));
      if (!owner.isCurrent() || get(editor).working !== draft) return;
      beginSelection();
      editor.reloadNodeVersion(doc);
      toastMsg('Loaded node version');
    } catch (e: unknown) {
      if (!owner.isCurrent()) return;
      editor.setError(e instanceof Error ? e.message : String(e));
      toastMsg('Failed to reload node version');
    } finally {
      if (owner.isCurrent()) editor.setSaving(false);
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
      if (nodeIp) editor.bindNode(nodeIp);
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

  // A session can change at the same address. Every connection generation also
  // invalidates in-flight work, while reconnects preserve this node's draft.
  function syncFocusedNode(ip: string) {
    const session = nodeAuthStore.getSession(ip);
    const changedOwner = ip !== nodeIp || session !== nodeSession;
    if (!changedOwner && nodeOwner?.isCurrent()) return;
    nodeIp = ip;
    nodeSession = session;
    nodeOwner = capturePageOperation();
    libraryGeneration++;
    loadingGeneration++;
    beginSelection();
    error = '';
    toast = '';
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = null;
    newSlotChooserOpen = false;
    navGuardOpen = false;
    pendingNav = null;
    if (changedOwner || !ip) {
      cueLists = [];
      presets = [];
      libraryReady = false;
      editor.reset();
      if (ip) editor.bindNode(ip);
    }
    if (ip) void loadData(!changedOwner);
    else loading = false;
  }

  onMount(() => {
    mounted = true;
    return nodeStore.subscribe((state) => syncFocusedNode(state.route ?? ''));
  });

  onDestroy(() => {
    mounted = false;
    selectionGeneration++;
    libraryGeneration++;
    loadingGeneration++;
    unsubscribeEditor();
    if (toastTimer) clearTimeout(toastTimer);
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
    {#if error || editorSnapshot?.error}
      <div class="issue-box" role="alert">
        <p>{error || editorSnapshot?.error}</p>
        {#if error}
          <button class="cbtn" onclick={() => loadData()}>Retry</button>
        {/if}
        <button class="cbtn" onclick={() => { error = ''; editor.setError(null); }}>Dismiss</button>
      </div>
    {/if}
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

    {#if manualModeActive && nodeOwner}
      {#key nodeOwner}
      {@const owner = nodeOwner}
      <div class="cue-surface tbar-surface">
        <ManualCueTBar
          {manualFadeQ15}
          currentLabel={currentCueLabel}
          nextLabel={nextCueLabel}
          currentIndex={currentCueIdx}
          nextIndex={nextCueIdx}
          disabled={isFollower}
          oncrossfade={(unit) => sendCrossfade(unit, owner)}
          onerror={(msg) => { if (owner.isCurrent()) toastMsg(msg); }}
        />
      </div>
      {/key}
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
          {#if cueLists.length === 0 && libraryReady}
            <p class="library-empty">No saved cue lists yet.</p>
          {:else if !libraryReady}
            <p class="library-empty">Cue lists could not be loaded.</p>
          {/if}
        </div>

        <div class="library-actions">
          <button class="cbtn cbtn-primary" onclick={openNewSlotChooser} disabled={!libraryReady}>+ New Cue List</button>
          <button class="cbtn" onclick={duplicateCurrentList} disabled={!working || !libraryReady}>Duplicate</button>
          <button class="cbtn" onclick={onExport} disabled={!working}>Export</button>
          <button class="cbtn" onclick={onImportClick} disabled={!libraryReady}>Import</button>
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
              <button class="cbtn" disabled={isFollower || !editorSnapshot?.persisted} onclick={runSavedVersion} title="Run the node's saved version without saving this draft">
                RUN SAVED VERSION
              </button>
            {:else}
              <button class="cbtn btn-run" disabled={isFollower} onclick={runShow}>▶ RUN</button>
            {/if}
            <div class="toolbar-spacer"></div>
            <button class="cbtn btn-danger" onclick={requestDeleteList} disabled={!editorSnapshot?.persisted || saving}>Delete</button>
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
  onreload={reloadNodeVersion}
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
