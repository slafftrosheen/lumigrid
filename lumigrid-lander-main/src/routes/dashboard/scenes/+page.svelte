<script lang="ts">
  import { onMount } from 'svelte';
  import SceneThumbnail from '$lib/components/dashboard/SceneThumbnail.svelte';
  import EffectGuidance from '$lib/components/dashboard/EffectGuidance.svelte';
  import TransitionEditor from '$lib/components/automation/TransitionEditor.svelte';
  import AutomationTrackEditor from '$lib/components/automation/AutomationTrackEditor.svelte';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';

  import {
    getPresets,
    getPreset,
    saveSceneDocument,
    activatePreset,
    getEffectsCached,
    getPalettesCached,
    getSceneReferences,
    deletePreset,
    createDefaultPreset,
    clonePreset,
    isSceneDocumentDirty,
    normalizeScene,
    exportSceneDocument,
    validateImportedSceneDocument,
    computeZonePreviewColor,
    getPwmIntensity,
    setPwmIntensity,
    rgbToHex,
    hexToRgb,
    isValidSceneName,
    getUtf8ByteLength,
    MAX_SCENE_NAME_BYTES,
    CHANNEL_COUNT,
    LAYER_COUNT,
    MAX_PRESETS,
    BLEND_MODES,
    type ScenePreset,
    type SceneDocument,
    type SceneReferences,
    type EffectMeta,
    type PaletteMeta,
    type SceneChannel,
    type SceneLayer,
    type BlendMode,
    type RgbColor,
  } from '$lib/api/scenes';

  import {
    createDefaultSceneMotion,
    createDefaultTrack,
    getSceneMotion,
    type AutomationTrackConfig,
  } from '$lib/api/sceneMotion';

  import { getChannels, type HardwareChannel } from '$lib/api/channels';
  import { nodeStore, commandTracker, captureNodeOperation } from '$lib/state/nodeStore';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';

  // ── Global Node & State Tracking ──────────────────────────────────────────────
  let nodeIp = $state('');
  let loading = $state(true);
  let saving = $state(false);
  let activating = $state(false);
  let libraryLoaded = $state(false);
  let documentLoading = $state(false);
  let error = $state('');
  let toast = $state('');

  // Node telemetry from RuntimeSnapshot v3 (strict snapshot, no status fallback)
  const snapshot = $derived($nodeStore.snapshot);
  const snapSync = $derived(snapshot?.sync);
  const isFollower = $derived(
    snapSync?.activeRole === 'worker' &&
    Boolean(snapSync.masterAddress) &&
    (snapSync.lastPacketAgeMs === null || snapSync.lastPacketAgeMs === undefined || snapSync.lastPacketAgeMs < 10000)
  );
  const syncMasterAddress = $derived(snapSync?.masterAddress || 'Leader');
  const activePreset = $derived(snapshot?.scene?.activePreset ?? -1);
  const sceneApplyPending = $derived(snapshot?.scene?.applyPending ?? false);

  // Authoritative hardware and effect metadata
  let presets = $state<ScenePreset[]>([]);
  let effects = $state<EffectMeta[]>([]);
  let palettes = $state<PaletteMeta[]>([]);
  let hwChannels = $state<HardwareChannel[]>([]);

  // ── Scene Document Model ──────────────────────────────────────────────────────
  let persistedDocument = $state<SceneDocument | null>(null);
  let workingDocument = $state<SceneDocument | null>(null);
  let selectedIdx = $state<number | null>(null);

  // Dirty state computed over both scene and motion
  const isDirty = $derived(isSceneDocumentDirty(workingDocument, persistedDocument));

  const editScene = $derived(workingDocument?.scene ?? null);
  const currentMotion = $derived(workingDocument?.motion ?? null);
  const activeChannels = $derived<SceneChannel[]>(editScene?.channels ?? editScene?.zones ?? []);

  const isActive = $derived(
    editScene !== null &&
    activePreset === editScene.index &&
    !sceneApplyPending
  );

  // Save Banner for active scene modifications
  let showApplyChangesBanner = $state(false);

  // Conflict state
  let showConflictModal = $state(false);

  // Editor mode tab
  let editorTab = $state<'channels' | 'motion'>('channels');

  // Channel selection model
  let selectedChannelIndices = $state<number[]>([0]);
  let activeInspectorChannel = $derived(
    selectedChannelIndices.length > 0 ? selectedChannelIndices[0] : 0
  );

  // Local Clipboard for copy/paste
  let channelClipboard = $state<SceneChannel | null>(null);
  let layerClipboard = $state<SceneLayer | null>(null);

  // Recent scene colors (for quick picker)
  const recentColors = $derived.by(() => {
    if (!editScene) return ['#ffffff', '#ff9900', '#6366f1', '#10b981'];
    const colors = new Set<string>();
    for (const ch of activeChannels) {
      for (const l of ch.layers) {
        if (l.enabled) {
          colors.add(rgbToHex(l.primaryColor));
        }
      }
    }
    return Array.from(colors).slice(0, 8);
  });

  // ── Generation-Guarded Async Loading ──────────────────────────────────────────
  let loadGen = 0;
  let selectionGen = 0;
  let disposed = false;
  let libraryRequest = 0;
  let workspaceOwner: ReturnType<typeof captureNodeOperation> | null = null;
  let workspaceSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let workspaceBound = false;
  let toastTimer: ReturnType<typeof setTimeout> | null = null;

  function captureWorkspaceOperation() {
    const owner = captureNodeOperation(nodeIp);
    const generation = loadGen;
    return { ip: owner.ip, isCurrent: () => !disposed && generation === loadGen && owner.isCurrent() };
  }

  function captureSceneOperation() {
    const owner = captureWorkspaceOperation();
    const selection = selectionGen;
    return { ip: owner.ip, isCurrent: () => selection === selectionGen && owner.isCurrent() };
  }

  async function loadData(targetIp: string, gen: number) {
    const owner = captureWorkspaceOperation();
    if (!owner.isCurrent() || owner.ip !== targetIp) return;
    const request = ++libraryRequest;
    const selection = selectionGen;
    const isCurrent = () => request === libraryRequest && gen === loadGen && owner.isCurrent();
    loading = true;
    libraryLoaded = false;
    error = '';
    showApplyChangesBanner = false;
    showConflictModal = false;

    try {
      const nodeId = snapshot?.node?.id && snapshot.node.id !== 'unknown' ? snapshot.node.id : targetIp;
      const cacheKey = `${nodeId}`;

      const [p, e, pals, chList] = await Promise.all([
        getPresets(targetIp),
        // Refresh on entry so an upgraded or downgraded node owns its catalogue.
        getEffectsCached(targetIp, cacheKey, true),
        getPalettesCached(targetIp, cacheKey, true),
        getChannels(targetIp),
      ]);

      if (!isCurrent()) return;

      presets = p;
      effects = e;
      palettes = pals;
      hwChannels = chList;
      libraryLoaded = true;

      // Metadata refreshes and reconnects must not replace a local draft.
      if (!workingDocument && selection === selectionGen) {
        const target = selectedIdx ?? presets[0]?.index;
        if (target !== undefined) await selectScene(target);
      }
    } catch (e) {
      if (!isCurrent()) return;
      error = e instanceof Error ? e.message : String(e);
    } finally {
      if (isCurrent()) {
        loading = false;
      }
    }
  }

  // ── Scene Selection & Navigation Protection ───────────────────────────────────
  let pendingAction = $state<(() => void) | null>(null);
  let showUnsavedModal = $state(false);

  function requestNavigation(action: () => void) {
    if (isDirty) {
      pendingAction = action;
      showUnsavedModal = true;
    } else {
      action();
    }
  }

  function confirmDiscard() {
    showUnsavedModal = false;
    if (pendingAction) {
      const act = pendingAction;
      pendingAction = null;
      act();
    }
  }

  function cancelDiscard() {
    showUnsavedModal = false;
    pendingAction = null;
  }

  async function selectScene(idx: number, refresh = false) {
    if (!nodeIp || !libraryLoaded) return;
    ++selectionGen;
    const owner = captureSceneOperation();
    if (!owner.isCurrent()) return;
    const previousDraft = workingDocument ? structuredClone($state.snapshot(workingDocument)) : null;
    documentLoading = true;
    saving = false;
    activating = false;
    error = '';
    showApplyChangesBanner = false;
    showConflictModal = false;

    try {
      const sceneObj = refresh ? await getPreset(owner.ip, idx) :
        presets.find(p => p.index === idx) ?? await getPreset(owner.ip, idx);
      if (!owner.isCurrent()) return;
      const motionObj = await getSceneMotion(owner.ip, idx);
      if (!owner.isCurrent()) return;
      if (isSceneDocumentDirty(workingDocument, previousDraft)) {
        error = 'Scene selection cancelled because the current draft changed while loading.';
        return;
      }
      const doc: SceneDocument = {
        scene: clonePreset($state.snapshot(sceneObj)),
        motion: structuredClone(motionObj),
        sceneChecksum: sceneObj.checksum,
        motionChecksum: motionObj.checksum,
      };
      selectedIdx = idx;
      persistedDocument = structuredClone(doc);
      workingDocument = structuredClone(doc);
      if (refresh) presets = presets.map(preset => preset.index === idx ? clonePreset($state.snapshot(sceneObj)) : preset);
      selectedChannelIndices = [0];
    } catch (e) {
      if (owner.isCurrent()) error = `Could not load scene: ${e instanceof Error ? e.message : String(e)}`;
    } finally {
      if (owner.isCurrent()) documentLoading = false;
    }
  }

  // ── Save & Activate Flows ─────────────────────────────────────────────────────
  async function saveDocument(activateAfter = false) {
    if (!workingDocument || !nodeIp || saving || documentLoading || !libraryLoaded) return;
    const owner = captureSceneOperation();
    if (!owner.isCurrent()) return;
    const submitted = structuredClone($state.snapshot(workingDocument));

    // Validate Scene Name UTF-8 Bytes
    const nameBytes = getUtf8ByteLength(workingDocument.scene.name.trim());
    if (nameBytes > MAX_SCENE_NAME_BYTES) {
      error = `Scene name exceeds ${MAX_SCENE_NAME_BYTES} UTF-8 bytes (currently ${nameBytes} bytes). Please shorten.`;
      return;
    }

    saving = true;
    error = '';

    try {
      const res = await saveSceneDocument(owner.ip, submitted, {
        expectedSceneChecksum: persistedDocument?.sceneChecksum,
        expectedMotionChecksum: persistedDocument?.motionChecksum,
        catalog: { effects, palettes },
      });
      if (!owner.isCurrent() || !workingDocument) return;

      // Update persisted state with server checksums
      const updatedScene = clonePreset(submitted.scene);
      updatedScene.checksum = res.checksum;
      const updatedMotion = structuredClone(submitted.motion);
      updatedMotion.checksum = res.motionChecksum;

      const newPersisted: SceneDocument = {
        scene: updatedScene,
        motion: updatedMotion,
        sceneChecksum: res.checksum,
        motionChecksum: res.motionChecksum,
      };

      persistedDocument = structuredClone(newPersisted);
      workingDocument.sceneChecksum = res.checksum;
      workingDocument.motionChecksum = res.motionChecksum;
      workingDocument.scene.checksum = res.checksum;
      workingDocument.motion.checksum = res.motionChecksum;

      // Update local presets library list
      const existing = presets.findIndex(p => p.index === updatedScene.index);
      if (existing >= 0) {
        presets[existing] = updatedScene;
      } else {
        presets = [...presets, updatedScene].sort((a, b) => a.index - b.index);
      }

      toastMsg(`Saved “${updatedScene.name}” (Slot ${(updatedScene.index + 1).toString().padStart(2, '0')})`);

      // Saving must NEVER silently alter live output
      if (isActive) {
        if (activateAfter) {
          await doActivate(updatedScene.index);
        } else {
          showApplyChangesBanner = true;
        }
      } else if (activateAfter) {
        await doActivate(updatedScene.index);
      }
    } catch (e: any) {
      if (!owner.isCurrent()) return;
      if (e?.status === 409 || e?.message?.includes('conflict') || e?.error === 'scene_conflict') {
        showConflictModal = true;
      } else {
        error = e?.message || String(e);
      }
    } finally {
      if (owner.isCurrent()) saving = false;
    }
  }

  async function doActivate(idx: number) {
    if (isFollower || activating || !nodeIp || !libraryLoaded || documentLoading) return;
    const owner = captureSceneOperation();
    if (!owner.isCurrent()) return;
    activating = true;
    error = '';
    showApplyChangesBanner = false;

    try {
      const result = await activatePreset(owner.ip, idx);
      if (!owner.isCurrent()) return;
      const cmdId = result.commandId ?? Date.now();
      commandTracker.register({
        id: cmdId,
        domain: 'scene',
        nodeIp: owner.ip,
        generation: result.generation,
      });
      await commandTracker.waitFor(cmdId, 5000);
      if (!owner.isCurrent()) return;
      toastMsg(`Scene ${(idx + 1).toString().padStart(2, '0')} activated on node`);
    } catch (e: any) {
      if (owner.isCurrent()) error = e instanceof Error ? e.message : String(e);
    } finally {
      if (owner.isCurrent()) activating = false;
    }
  }

  async function reloadNodeVersion() {
    if (selectedIdx !== null && !saving && !documentLoading) await selectScene(selectedIdx, true);
  }

  function keepDraft() {
    showConflictModal = false;
    // Keeping browser edits does not authorize overwriting a newer node version.
  }

  // ── New Scene & Duplication Flows ─────────────────────────────────────────────
  let showNewSceneModal = $state(false);
  let newSceneSlot = $state<number | null>(null);
  let newSceneMode = $state<'blank' | 'duplicate'>('blank');

  const freeSlots = $derived.by(() => {
    const used = new Set(presets.map(p => p.index));
    const free: number[] = [];
    for (let i = 0; i < MAX_PRESETS; i++) {
      if (!used.has(i)) free.push(i);
    }
    return free;
  });

  function openNewSceneModal() {
    if (!libraryLoaded || documentLoading || !captureWorkspaceOperation().isCurrent()) return;
    requestNavigation(() => {
      newSceneSlot = freeSlots.length > 0 ? freeSlots[0] : null;
      newSceneMode = 'blank';
      showNewSceneModal = true;
    });
  }

  function confirmNewScene() {
    if (newSceneSlot === null || !libraryLoaded || !captureWorkspaceOperation().isCurrent()) return;
    ++selectionGen;
    documentLoading = saving = activating = false;
    const target = newSceneSlot;
    showNewSceneModal = false;

    if (newSceneMode === 'duplicate' && workingDocument) {
      const dupScene = clonePreset($state.snapshot(workingDocument.scene));
      dupScene.index = target;
      dupScene.name = `${dupScene.name} (copy)`.slice(0, 23);
      delete dupScene.checksum;

      const dupMotion = structuredClone($state.snapshot(workingDocument.motion));
      dupMotion.index = target;
      delete dupMotion.checksum;

      workingDocument = {
        scene: dupScene,
        motion: dupMotion,
      };
      persistedDocument = null; // Unsaved new draft!
      selectedIdx = target;
      toastMsg(`Duplicated current scene into Slot ${(target + 1).toString().padStart(2, '0')} (unsaved draft)`);
    } else {
      const defScene = createDefaultPreset(target, `Scene ${target + 1}`);
      const defMotion = createDefaultSceneMotion(target);

      workingDocument = {
        scene: defScene,
        motion: defMotion,
      };
      persistedDocument = null;
      selectedIdx = target;
      toastMsg(`Created new scene in Slot ${(target + 1).toString().padStart(2, '0')} (unsaved draft)`);
    }
  }

  // ── Safe Deletion with Reference Protection ───────────────────────────────────
  let showDeleteModal = $state(false);
  let deleteTargetIdx = $state<number | null>(null);
  let deleteReferences = $state<SceneReferences | null>(null);
  let checkingReferences = $state(false);
  let deletingScene = $state(false);
  let deleteOwner: ReturnType<typeof captureWorkspaceOperation> | null = null;
  let deleteRequest = 0;

  async function openDeleteModal(idx: number) {
    const owner = captureWorkspaceOperation();
    if (!owner.isCurrent() || !libraryLoaded) return;
    deleteOwner = owner;
    const request = ++deleteRequest;
    const isCurrent = () => owner.isCurrent() && request === deleteRequest && deleteTargetIdx === idx;
    deleteTargetIdx = idx;
    checkingReferences = true;
    deleteReferences = null;
    showDeleteModal = true;

    if (nodeIp) {
      try {
        const refs = await getSceneReferences(owner.ip, idx);
        if (isCurrent()) deleteReferences = refs;
      } catch (e) {
        if (isCurrent()) error = `Could not read scene dependencies: ${e instanceof Error ? e.message : String(e)}`;
      } finally {
        if (isCurrent()) checkingReferences = false;
      }
    } else {
      checkingReferences = false;
    }
  }

  const isSceneBlockedFromDelete = $derived.by(() => {
    if (!deleteReferences) return false;
    return (
      deleteReferences.active ||
      deleteReferences.fallback ||
      deleteReferences.cues.length > 0 ||
      deleteReferences.timelineTracks.length > 0 ||
      deleteReferences.schedules.length > 0
    );
  });

  async function confirmDeleteScene() {
    if (deleteTargetIdx === null || deletingScene || !nodeIp || !deleteReferences ||
        checkingReferences || isSceneBlockedFromDelete || !deleteOwner?.isCurrent()) return;
    const owner = deleteOwner;
    const selection = captureSceneOperation();
    const previousDraft = workingDocument ? structuredClone($state.snapshot(workingDocument)) : null;
    const request = deleteRequest;
    const isCurrent = () => owner.isCurrent() && request === deleteRequest;
    deletingScene = true;
    const idx = deleteTargetIdx;

    try {
      await deletePreset(owner.ip, idx);
      if (!isCurrent()) return;
      presets = presets.filter(p => p.index !== idx);
      if (selectedIdx === idx && selection.isCurrent() && !isSceneDocumentDirty(workingDocument, previousDraft)) {
        workingDocument = null;
        persistedDocument = null;
        selectedIdx = presets.length > 0 ? presets[0].index : null;
        if (selectedIdx !== null) {
          await selectScene(selectedIdx);
          if (!isCurrent()) return;
        }
      } else if (selectedIdx === idx && selection.isCurrent() && workingDocument) {
        // Deletion has completed, but edits made during the request remain recoverable.
        persistedDocument = null;
        delete workingDocument.sceneChecksum;
        delete workingDocument.motionChecksum;
        delete workingDocument.scene.checksum;
        delete workingDocument.motion.checksum;
      }
      toastMsg(`Deleted Scene ${(idx + 1).toString().padStart(2, '0')}`);
      showDeleteModal = false;
    } catch (e: any) {
      if (isCurrent()) error = e?.message || String(e);
    } finally {
      if (isCurrent()) deletingScene = false;
    }
  }

  // ── Import / Export Workflows ─────────────────────────────────────────────────
  let fileInputRef: HTMLInputElement | null = $state(null);

  function triggerExport() {
    if (!workingDocument) return;
    const jsonStr = exportSceneDocument(workingDocument);
    const blob = new Blob([jsonStr], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `scene_${(workingDocument.scene.index + 1).toString().padStart(2, '0')}_${workingDocument.scene.name.replace(/\s+/g, '_')}.json`;
    a.click();
    URL.revokeObjectURL(url);
    toastMsg('Exported scene file');
  }

  function triggerImportClick() {
    if (fileInputRef) {
      fileInputRef.click();
    }
  }

  async function handleImportFile(e: Event) {
    const input = e.target as HTMLInputElement;
    if (!input.files || input.files.length === 0) return;
    const file = input.files[0];
    const owner = captureSceneOperation();
    if (!owner.isCurrent() || !libraryLoaded) { input.value = ''; return; }
    try {
      const text = await file.text();
      if (!owner.isCurrent()) return;
      const valRes = validateImportedSceneDocument(text, { effects, palettes });
      if (valRes.valid === false) {
        error = `Cannot import scene: ${valRes.error}`;
        return;
      }
      requestNavigation(() => {
        if (!owner.isCurrent()) return;
        ++selectionGen;
        documentLoading = saving = activating = false;
        // Load into working document as unsaved draft for operator to choose slot and save
        const slot = selectedIdx ?? (freeSlots.length > 0 ? freeSlots[0] : 0);
        valRes.document.scene.index = slot;
        valRes.document.motion.index = slot;
        delete valRes.document.sceneChecksum;
        delete valRes.document.motionChecksum;

        workingDocument = valRes.document;
        persistedDocument = null;
        selectedIdx = slot;
        toastMsg(`Imported “${workingDocument.scene.name}” as unsaved draft in Slot ${(slot + 1).toString().padStart(2, '0')}`);
      });
    } catch (err: any) {
      if (owner.isCurrent()) error = `Import failed: ${err?.message || String(err)}`;
    } finally {
      input.value = '';
    }
  }

  // ── Channel Canvas & Selection ────────────────────────────────────────────────
  function isChannelSelected(idx: number): boolean {
    return selectedChannelIndices.includes(idx);
  }

  function handleChannelClick(idx: number, e: MouseEvent) {
    if (e.shiftKey || e.ctrlKey || e.metaKey) {
      if (selectedChannelIndices.includes(idx)) {
        if (selectedChannelIndices.length > 1) {
          selectedChannelIndices = selectedChannelIndices.filter(i => i !== idx);
        }
      } else {
        selectedChannelIndices = [...selectedChannelIndices, idx];
      }
    } else {
      selectedChannelIndices = [idx];
    }
  }

  function selectAllA() {
    selectedChannelIndices = [0, 1, 2, 3, 4, 5, 6, 7];
  }

  function selectAllP() {
    selectedChannelIndices = [8, 9, 10, 11, 12, 13, 14, 15];
  }

  function selectConfigured() {
    const configured: number[] = [];
    for (let i = 0; i < 16; i++) {
      const ch = hwChannels.find(c => c.index === i);
      if (ch && ch.configured) configured.push(i);
    }
    if (configured.length > 0) {
      selectedChannelIndices = configured;
    } else {
      toastMsg('No configured hardware channels found');
    }
  }

  function getChannelLabel(idx: number): string {
    const port = idx < 8 ? `A${idx + 1}` : `P${idx - 7}`;
    const ch = hwChannels.find(c => c.index === idx);
    if (ch && ch.name) return `${port} · ${ch.name}`;
    return `${port} · ${idx < 8 ? 'Addressable' : 'PWM Dimmer'}`;
  }

  // ── Clipboard Copy/Paste with PWM Normalization ────────────────────────────────
  function copyCurrentChannel() {
    if (!editScene) return;
    const ch = activeChannels[activeInspectorChannel];
    if (ch) {
      channelClipboard = structuredClone($state.snapshot(ch));
      toastMsg(`Copied ${getChannelLabel(activeInspectorChannel)} to clipboard`);
    }
  }

  function pasteCurrentChannel() {
    if (!editScene || !channelClipboard) return;
    for (const chIdx of selectedChannelIndices) {
      const targetCh = activeChannels[chIdx];
      if (!targetCh) continue;
      const isTargetPwm = chIdx >= 8;

      for (let l = 0; l < 3; l++) {
        const srcLayer = channelClipboard.layers[l];
        targetCh.layers[l] = normalizeLayerForChannel(srcLayer, isTargetPwm);
      }
    }
    toastMsg(`Pasted channel to ${selectedChannelIndices.length} channel${selectedChannelIndices.length > 1 ? 's' : ''}`);
  }

  function copyLayer(lIdx: number) {
    if (!editScene) return;
    const ch = activeChannels[activeInspectorChannel];
    if (ch && ch.layers[lIdx]) {
      layerClipboard = structuredClone($state.snapshot(ch.layers[lIdx]));
      toastMsg(`Copied Layer ${lIdx + 1} to clipboard`);
    }
  }

  function pasteLayer(lIdx: number) {
    if (!editScene || !layerClipboard) return;
    for (const chIdx of selectedChannelIndices) {
      const targetCh = activeChannels[chIdx];
      if (!targetCh) continue;
      const isTargetPwm = chIdx >= 8;
      targetCh.layers[lIdx] = normalizeLayerForChannel(layerClipboard, isTargetPwm);
    }
    toastMsg(`Pasted Layer ${lIdx + 1} to ${selectedChannelIndices.length} channel${selectedChannelIndices.length > 1 ? 's' : ''}`);
  }

  function normalizeLayerForChannel(src: SceneLayer, isPwm: boolean): SceneLayer {
    const clone = structuredClone($state.snapshot(src));
    if (isPwm) {
      // Normalization through PWM dimmer semantics:
      // derive intensity from max RGB component, set grayscale R=G=B
      const maxCol = Math.max(clone.primaryColor.r, clone.primaryColor.g, clone.primaryColor.b);
      clone.primaryColor = { r: maxCol, g: maxCol, b: maxCol };
      clone.secondaryColor = { r: 0, g: 0, b: 0 };
      clone.paletteId = 0;
    }
    return clone;
  }

  // ── Batch Editing Helpers ─────────────────────────────────────────────────────
  function applyLayerToSelection(layerIdx: number) {
    if (!editScene || selectedChannelIndices.length <= 1) return;
    const srcLayer = activeChannels[activeInspectorChannel]?.layers[layerIdx];
    if (!srcLayer) return;

    for (const chIdx of selectedChannelIndices) {
      if (chIdx === activeInspectorChannel) continue;
      const targetCh = activeChannels[chIdx];
      if (targetCh) {
        targetCh.layers[layerIdx] = normalizeLayerForChannel(srcLayer, chIdx >= 8);
      }
    }
    toastMsg(`Applied Layer ${layerIdx + 1} across ${selectedChannelIndices.length} selected channels`);
  }

  // ── Layer Reordering & Actions ────────────────────────────────────────────────
  function moveLayer(lIdx: number, dir: -1 | 1) {
    const targetIdx = lIdx + dir;
    if (targetIdx < 0 || targetIdx >= 3) return;
    for (const chIdx of selectedChannelIndices) {
      const ch = activeChannels[chIdx];
      if (!ch) continue;
      const tmp = ch.layers[lIdx];
      ch.layers[lIdx] = ch.layers[targetIdx];
      ch.layers[targetIdx] = tmp;
    }
  }

  function clearLayer(lIdx: number) {
    for (const chIdx of selectedChannelIndices) {
      const ch = activeChannels[chIdx];
      if (ch && ch.layers[lIdx]) {
        ch.layers[lIdx].enabled = false;
      }
    }
  }

  // ── Effect Change Normalization ───────────────────────────────────────────────
  function handleEffectChange(layer: SceneLayer, newEffectId: string) {
    const meta = effects.find(e => e.id === newEffectId);
    if (!meta) return;
    layer.effect = newEffectId;
    layer.fxParam = meta.hasParameter ? meta.parameterDefault ?? 0 : 0;
  }

  // ── Motion Helpers ────────────────────────────────────────────────────────────
  function addMotionTrack() {
    if (!workingDocument) return;
    if (workingDocument.motion.tracks.length >= 4) {
      toastMsg('Maximum 4 motion tracks allowed');
      return;
    }
    workingDocument.motion.tracks = [
      ...workingDocument.motion.tracks,
      createDefaultTrack('globalBrightness'),
    ];
  }

  function deleteMotionTrack(idx: number) {
    if (!workingDocument) return;
    workingDocument.motion.tracks = workingDocument.motion.tracks.filter((_, i) => i !== idx);
  }

  // ── Lifecycle & Invariant Guards ──────────────────────────────────────────────
  function syncFocusedNode(targetIp: string) {
    const session = nodeAuthStore.getSession(targetIp);
    const changedOwner = !workspaceBound || targetIp !== nodeIp || session !== workspaceSession;
    if (!changedOwner && workspaceOwner?.isCurrent()) return;
    workspaceBound = true;
    nodeIp = targetIp;
    workspaceSession = session;
    loadGen++;
    selectionGen++;
    workspaceOwner = nodeIp ? captureNodeOperation(nodeIp) : null;
    documentLoading = saving = activating = deletingScene = checkingReferences = false;
    showApplyChangesBanner = showConflictModal = showUnsavedModal = showNewSceneModal = showDeleteModal = false;
    pendingAction = null;
    deleteOwner = null; ++deleteRequest;
    deleteTargetIdx = null; deleteReferences = null;
    toast = error = '';
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = null;
    if (changedOwner || !nodeIp) {
      presets = []; effects = []; palettes = []; hwChannels = [];
      workingDocument = persistedDocument = null;
      selectedIdx = null;
      libraryLoaded = false;
    }
    if (nodeIp) void loadData(nodeIp, loadGen);
    else loading = false;
  }

  onMount(() => {
    disposed = false;
    // Unload protection for browser tab closing/reload when dirty
    const handleBeforeUnload = (e: BeforeUnloadEvent) => {
      if (isDirty) {
        e.preventDefault();
        e.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', handleBeforeUnload);

    const unsub = nodeStore.subscribe(state => syncFocusedNode(state.route || ''));

    return () => {
      disposed = true;
      ++loadGen; ++selectionGen; ++deleteRequest;
      if (toastTimer) clearTimeout(toastTimer);
      window.removeEventListener('beforeunload', handleBeforeUnload);
      unsub();
    };
  });

  function toastMsg(msg: string) {
    toast = msg;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      toast = '';
      toastTimer = null;
    }, 3000);
  }
</script>

<svelte:head>
  <title>Scene Studio — LumiGrid</title>
</svelte:head>

<!-- Hidden file input for single-scene import -->
<input
  type="file"
  accept=".json"
  style="display: none;"
  bind:this={fileInputRef}
  onchange={handleImportFile}
/>

{#if loading}
  <div class="scene-root">
    <div class="loading-box">
      <div class="spinner"></div>
      <p>Loading Scene Studio v2 workspace…</p>
    </div>
  </div>
{:else}
  <div class="scene-root">
    <!-- ═══════════════════════════════════════════════════════════════════════ -->
    <!-- 1. SCENE LIBRARY SIDEBAR (Visual Cards)                              -->
    <!-- ═══════════════════════════════════════════════════════════════════════ -->
    <aside class="library">
      <div class="lib-header">
        <div class="lib-title-row">
          <h2>Scene Library</h2>
          <span class="lib-count">{presets.length}/{MAX_PRESETS}</span>
        </div>
        <div class="lib-header-actions">
          <button class="btn-import" onclick={triggerImportClick} disabled={!libraryLoaded || documentLoading} title="Import single scene">Import</button>
          <button class="btn-primary btn-sm" onclick={openNewSceneModal} disabled={!libraryLoaded || documentLoading} title="Create new scene">+ New Scene</button>
        </div>
      </div>

      <div class="lib-list">
        {#if !libraryLoaded}
          <div class="empty-library-hint">
            <p>Scene library unavailable. Load the saved library before creating or saving scenes.</p>
            <button class="btn-primary btn-sm" onclick={() => loadData(nodeIp, loadGen)} disabled={!nodeIp}>Retry</button>
          </div>
        {:else if presets.length === 0}
          <div class="empty-library-hint">
            <p>No occupied scenes.</p>
            <button class="btn-primary btn-sm" onclick={openNewSceneModal}>Create First Scene</button>
          </div>
        {:else}
          {#each presets as preset (preset.index)}
            {@const isPresetActive = activePreset === preset.index && !sceneApplyPending}
            {@const isPresetSelected = selectedIdx === preset.index}
            <div
              class="scene-card"
              class:selected={isPresetSelected}
              class:active={isPresetActive}
              onclick={() => requestNavigation(() => selectScene(preset.index))}
              onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); requestNavigation(() => selectScene(preset.index)); } }}
              role="button"
              tabindex="0"
              aria-pressed={isPresetSelected}
            >
              <div class="card-top-row">
                <span class="card-name" title={preset.name}>{preset.name}</span>
                <span class="card-slot">{(preset.index + 1).toString().padStart(2, '0')}</span>
              </div>

              <!-- Reusable Scene Visual Thumbnail -->
              <div class="card-preview">
                <SceneThumbnail
                  {preset}
                  active={isPresetActive}
                  motionTracksCount={selectedIdx === preset.index ? currentMotion?.tracks.length ?? 0 : 0}
                />
              </div>

              <div class="card-meta-row">
                <span class="card-meta">
                  {Math.round((preset.globalBrightness / 255) * 100)}% · {(preset.transitionMs / 1000).toFixed(1)} s
                </span>
                {#if isPresetActive}
                  <span class="badge-active-live">ACTIVE</span>
                {/if}
              </div>
            </div>
          {/each}
        {/if}
      </div>
    </aside>

    <!-- ═══════════════════════════════════════════════════════════════════════ -->
    <!-- 2. SCENE WORKSPACE & EDITING CANVAS                                   -->
    <!-- ═══════════════════════════════════════════════════════════════════════ -->
    <main class="editor">
      {#if documentLoading}<p role="status">Loading selected scene…</p>{/if}
      <!-- Sync Follower Banner -->
      {#if isFollower}
        <div class="sync-follower-banner" role="alert">
          <span class="banner-icon">ℹ</span>
          <div class="banner-content">
            <strong>Playback controlled by Sync Leader ({syncMasterAddress})</strong>
            <p>Direct scene activation on this follower node is suppressed to protect cluster synchronization. Editing and saving remain available.</p>
          </div>
          <a href="/dashboard/sync" class="banner-action-btn">Open Sync Studio →</a>
        </div>
      {/if}

      <!-- Conflict Banner -->
      {#if showConflictModal}
        <div class="conflict-banner" role="alert">
          <span class="banner-icon">⚠️</span>
          <div class="banner-content">
            <strong>This scene changed on the node since you opened it.</strong>
            <p>Another operator or show trigger modified this preset slot.</p>
          </div>
          <div class="conflict-actions">
            <button class="btn-conflict-reload" onclick={reloadNodeVersion}>Reload Node Version</button>
            <button class="btn-conflict-draft" onclick={keepDraft}>Keep My Draft</button>
          </div>
        </div>
      {/if}

      <!-- Active Scene Saved Banner (Safe Persistence Invariant) -->
      {#if showApplyChangesBanner}
        <div class="apply-changes-banner" role="status">
          <span class="banner-icon">💾</span>
          <div class="banner-content">
            <strong>Saved to Node Storage</strong>
            <p>The running hardware output still uses the previously activated version.</p>
          </div>
          {#if !isFollower}
            <button class="btn-apply-changes" onclick={() => doActivate(editScene!.index)} disabled={documentLoading || !libraryLoaded}>
              Apply Changes
            </button>
          {/if}
        </div>
      {/if}

      {#if editScene && workingDocument}
        <!-- Top Workspace Header -->
        <header class="ed-header">
          <div class="ed-title-block">
            <div class="ed-name-row">
              <input
                class="ed-name-input"
                bind:value={workingDocument.scene.name}
                maxlength={MAX_SCENE_NAME_BYTES}
                placeholder="Scene name"
                aria-label="Scene Name"
              />
              <span class="ed-slot-tag">Slot {(editScene.index + 1).toString().padStart(2, '0')}</span>
              <span class="name-bytes-counter" class:warn={getUtf8ByteLength(workingDocument.scene.name.trim()) > MAX_SCENE_NAME_BYTES}>
                {getUtf8ByteLength(workingDocument.scene.name.trim())}/{MAX_SCENE_NAME_BYTES}B
              </span>
            </div>

            <div class="ed-status-pills">
              {#if isActive}
                <span class="sc-pill pill-active"><span class="dot-pulse"></span> ACTIVE</span>
              {/if}
              {#if isDirty}
                <span class="sc-pill pill-edited">EDITED</span>
              {:else if persistedDocument}
                <span class="sc-pill pill-saved">SAVED</span>
              {:else}
                <span class="sc-pill pill-new">DRAFT</span>
              {/if}
              {#if currentMotion && currentMotion.tracks.length > 0}
                <span class="sc-pill pill-motion">{currentMotion.tracks.length} motion track{currentMotion.tracks.length > 1 ? 's' : ''}</span>
              {/if}
            </div>
          </div>

          <div class="ed-header-actions">
            {#if isDirty}
              <button
                class="btn-primary"
                onclick={() => saveDocument(true)}
                disabled={saving || activating || isFollower || documentLoading || !libraryLoaded}
                title="Save document and immediately activate live"
              >
                {saving ? 'Saving...' : activating ? 'Activating...' : 'Save & Activate'}
              </button>
              <button
                class="btn-secondary"
                onclick={() => saveDocument(false)}
                disabled={saving || documentLoading || !libraryLoaded}
                title="Save document to node NVS storage only"
              >
                Save
              </button>
            {:else}
              <button
                class="btn-primary"
                onclick={() => doActivate(editScene.index)}
                disabled={activating || isFollower || isActive || documentLoading || !libraryLoaded}
                title="Activate saved version on live hardware"
              >
                {activating ? 'Activating...' : isActive ? 'Live Active' : 'Activate'}
              </button>
              <button
                class="btn-secondary"
                onclick={() => saveDocument(false)}
                disabled={saving || documentLoading || !libraryLoaded}
                title="Save changes to NVS"
              >
                Save
              </button>
            {/if}

            <div class="header-menu">
              <button class="btn-icon" onclick={triggerExport} title="Export Scene JSON">Export</button>
              <button class="btn-icon" onclick={() => openDeleteModal(editScene.index)} title="Delete Scene">Delete</button>
            </div>
          </div>
        </header>

        <!-- Global Scene Controls (Brightness & Integrated Transition) -->
        <section class="global-scene-strip">
          <div class="global-field">
            <label for="global-bri-slider">Global Brightness</label>
            <div class="slider-row">
              <input
                id="global-bri-slider"
                type="range"
                min="0"
                max="255"
                bind:value={workingDocument.scene.globalBrightness}
                class="range-slider"
              />
              <span class="slider-val">{Math.round((workingDocument.scene.globalBrightness / 255) * 100)}%</span>
            </div>
          </div>

          <!-- Integrated Transition Section (Requirement 29) -->
          <div class="global-transition">
            <TransitionEditor
              bind:durationMs={workingDocument.scene.transitionMs}
              bind:curve={workingDocument.motion.transitionCurve}
            />
          </div>
        </section>

        <!-- Tabs: Channels vs Motion -->
        <div class="editor-tabs-bar">
          <button
            class="tab-btn"
            class:active={editorTab === 'channels'}
            onclick={() => (editorTab = 'channels')}
          >
            16-Channel Canvas
          </button>
          <button
            class="tab-btn"
            class:active={editorTab === 'motion'}
            onclick={() => (editorTab = 'motion')}
          >
            Motion & Automation ({currentMotion?.tracks.length ?? 0}/4)
          </button>
        </div>

        {#if editorTab === 'channels'}
          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <!-- 3. 16-CHANNEL CANVAS (Addressable A1–A8 & PWM P1–P8)                -->
          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <section class="canvas-section">
            <div class="canvas-toolbar">
              <span class="canvas-label">
                Scene Preview
                <small class="preview-subtext">(Editor draft visualization)</small>
              </span>

              <div class="selection-controls">
                <span class="sel-count">Selected: {selectedChannelIndices.length}</span>
                <button class="btn-sel-chip" onclick={selectAllA}>All A</button>
                <button class="btn-sel-chip" onclick={selectAllP}>All P</button>
                <button class="btn-sel-chip" onclick={selectConfigured}>Configured</button>
                <button class="btn-sel-chip" onclick={copyCurrentChannel} title="Copy active channel">Copy Ch</button>
                <button
                  class="btn-sel-chip"
                  onclick={pasteCurrentChannel}
                  disabled={!channelClipboard}
                  title="Paste channel onto selected"
                >Paste Ch</button>
              </div>
            </div>

            <!-- Two Explicit Banks: ADDRESSABLE & PWM -->
            <div class="banks-container">
              <!-- Addressable Bank A1–A8 -->
              <div class="bank-group">
                <div class="bank-title">Addressable Channels (A1–A8)</div>
                <div class="bank-grid">
                  {#each Array(8) as _, i}
                    {@const ch = activeChannels[i]}
                    {@const isSel = isChannelSelected(i)}
                    {@const col = ch ? computeZonePreviewColor(ch, workingDocument.scene.globalBrightness) : { r: 0, g: 0, b: 0 }}
                    {@const hex = rgbToHex(col)}
                    {@const activeLayerCount = ch?.layers.filter(l => l.enabled).length ?? 0}
                    <div
                      class="channel-card"
                      class:selected={isSel}
                      class:pwm={false}
                      onclick={(e) => handleChannelClick(i, e)}
                      role="button"
                      tabindex="0"
                      onkeydown={(e) => { if (e.key === 'Enter') handleChannelClick(i, e as any); }}
                    >
                      <div class="ch-head">
                        <span class="ch-port">A{i + 1}</span>
                        <span class="ch-layers">{activeLayerCount} L</span>
                      </div>
                      <div class="ch-swatch" style="background-color: {activeLayerCount > 0 ? hex : '#1a1f2c'};"></div>
                      <div class="ch-name" title={getChannelLabel(i)}>
                        {hwChannels.find(c => c.index === i)?.name || 'Addressable'}
                      </div>
                    </div>
                  {/each}
                </div>
              </div>

              <!-- PWM Bank P1–P8 -->
              <div class="bank-group">
                <div class="bank-title">PWM Dimmer Channels (P1–P8)</div>
                <div class="bank-grid">
                  {#each Array(8) as _, i}
                    {@const chIdx = i + 8}
                    {@const ch = activeChannels[chIdx]}
                    {@const isSel = isChannelSelected(chIdx)}
                    {@const col = ch ? computeZonePreviewColor(ch, workingDocument.scene.globalBrightness) : { r: 0, g: 0, b: 0 }}
                    {@const intensityPct = Math.round((Math.max(col.r, col.g, col.b) / 255) * 100)}
                    {@const activeLayerCount = ch?.layers.filter(l => l.enabled).length ?? 0}
                    <div
                      class="channel-card"
                      class:selected={isSel}
                      class:pwm={true}
                      onclick={(e) => handleChannelClick(chIdx, e)}
                      role="button"
                      tabindex="0"
                      onkeydown={(e) => { if (e.key === 'Enter') handleChannelClick(chIdx, e as any); }}
                    >
                      <div class="ch-head">
                        <span class="ch-port">P{i + 1}</span>
                        <span class="ch-layers">{activeLayerCount} L</span>
                      </div>
                      <div class="pwm-bar-track">
                        <div class="pwm-bar-fill" style="height: {activeLayerCount > 0 ? intensityPct : 0}%;"></div>
                      </div>
                      <div class="ch-name" title={getChannelLabel(chIdx)}>
                        {hwChannels.find(c => c.index === chIdx)?.name || 'Dimmer'} · {intensityPct}%
                      </div>
                    </div>
                  {/each}
                </div>
              </div>
            </div>
          </section>

          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <!-- 4. CHANNEL INSPECTOR (Addressable vs PWM Dimmer Oriented)          -->
          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <section class="inspector-section">
            <div class="inspector-header">
              <div class="ins-title-row">
                <h3>{getChannelLabel(activeInspectorChannel)}</h3>
                {#if selectedChannelIndices.length > 1}
                  <span class="batch-badge">Editing {selectedChannelIndices.length} channels simultaneously</span>
                {/if}
              </div>

              {#if selectedChannelIndices.length > 1}
                <div class="batch-actions-strip">
                  <button class="btn-batch" onclick={() => applyLayerToSelection(0)}>Apply L1 to Selection</button>
                  <button class="btn-batch" onclick={() => applyLayerToSelection(1)}>Apply L2 to Selection</button>
                  <button class="btn-batch" onclick={() => applyLayerToSelection(2)}>Apply L3 to Selection</button>
                </div>
              {/if}
            </div>

            <!-- Ordered 3-Layer Stack -->
            <div class="layers-stack">
              {#each [0, 1, 2] as lIdx}
                {@const layer = activeChannels[activeInspectorChannel]?.layers[lIdx]}
                {#if layer}
                  {@const isPwm = activeInspectorChannel >= 8}
                  {@const effectMeta = effects.find(e => e.id === layer.effect)}
                  {@const pwmRecommended = effectMeta?.pwmRecommended === true}

                  <div class="layer-card" class:disabled={!layer.enabled}>
                    <EffectGuidance effect={effectMeta} channel={activeInspectorChannel} />
                    <div class="layer-header-row">
                      <div class="layer-toggle-wrap">
                        <input
                          type="checkbox"
                          bind:checked={layer.enabled}
                          id={`l-toggle-${lIdx}`}
                          class="toggle-checkbox"
                        />
                        <label for={`l-toggle-${lIdx}`} class="layer-title">Layer {lIdx + 1}</label>
                      </div>

                      <div class="layer-quick-actions">
                        <button class="btn-layer-act" onclick={() => copyLayer(lIdx)} title="Copy layer">Copy</button>
                        <button class="btn-layer-act" onclick={() => pasteLayer(lIdx)} disabled={!layerClipboard} title="Paste layer">Paste</button>
                        <button class="btn-layer-act" onclick={() => moveLayer(lIdx, -1)} disabled={lIdx === 0} title="Move Up">↑</button>
                        <button class="btn-layer-act" onclick={() => moveLayer(lIdx, 1)} disabled={lIdx === 2} title="Move Down">↓</button>
                        <button class="btn-layer-act btn-clear" onclick={() => clearLayer(lIdx)} title="Clear layer">Clear</button>
                      </div>
                    </div>

                    {#if layer.enabled}
                      <div class="layer-controls-grid">
                        <!-- Effect Selection -->
                        <div class="ctrl-field">
                          <label for={`effect-sel-${lIdx}`}>Effect</label>
                          <select
                            id={`effect-sel-${lIdx}`}
                            value={layer.effect}
                            onchange={(e) => handleEffectChange(layer, (e.target as HTMLSelectElement).value)}
                            class="input-select"
                          >
                            {#if !effectMeta}
                              <option value={layer.effect} disabled>{layer.effect} (Unavailable on this node)</option>
                            {/if}
                            {#each effects as eff}
                              <option value={eff.id}>
                                {eff.label} {isPwm && !(eff.pwmRecommended ?? !eff.spatial) ? '(Not recommended for PWM)' : ''}
                              </option>
                            {/each}
                          </select>
                          {#if !effectMeta}
                            <span class="pwm-warn-badge">Choose an effect supported by this node before saving.</span>
                          {:else if isPwm && !pwmRecommended}
                            <span class="pwm-warn-badge">⚠️ Not recommended for PWM dimmer</span>
                          {/if}
                        </div>

                        {#if isPwm}
                          <!-- PWM DIMMER-ORIENTED INSPECTOR (Requirement 22) -->
                          <!-- PWM level uses the layer's compositing intensity. -->
                          <div class="ctrl-field">
                            <label for={`pwm-int-${lIdx}`}>Intensity ({Math.round((getPwmIntensity(layer) / 255) * 100)}%)</label>
                            <input
                              id={`pwm-int-${lIdx}`}
                              type="range"
                              min="0"
                              max="255"
                              value={getPwmIntensity(layer)}
                              oninput={(e) => setPwmIntensity(layer, Number((e.target as HTMLInputElement).value))}
                              class="range-slider"
                            />
                          </div>
                        {:else}
                          <!-- ADDRESSABLE CHANNEL INSPECTOR (Requirement 21) -->
                          <!-- Primary Color with visual picker, hex, and recent palette -->
                          <div class="ctrl-field color-field">
                            <label for={`primary-col-${lIdx}`}>Primary Color</label>
                            <div class="color-picker-row">
                              <input
                                id={`primary-col-${lIdx}`}
                                type="color"
                                value={rgbToHex(layer.primaryColor)}
                                oninput={(e) => {
                                  const rgb = hexToRgb((e.target as HTMLInputElement).value);
                                  if (rgb) layer.primaryColor = rgb;
                                }}
                                class="native-color"
                              />
                              <input
                                type="text"
                                class="input-text hex-input"
                                value={rgbToHex(layer.primaryColor)}
                                onchange={(e) => {
                                  const rgb = hexToRgb((e.target as HTMLInputElement).value);
                                  if (rgb) layer.primaryColor = rgb;
                                }}
                              />
                            </div>
                            <!-- Recent Scene Colors -->
                            <div class="recent-colors-row">
                              {#each recentColors as rCol}
                                <button
                                  type="button"
                                  class="swatch-btn"
                                  style="background-color: {rCol};"
                                  onclick={() => {
                                    const rgb = hexToRgb(rCol);
                                    if (rgb) layer.primaryColor = rgb;
                                  }}
                                  title={rCol}
                                ></button>
                              {/each}
                            </div>
                          </div>

                          <!-- Secondary Color (when supported) -->
                          {#if effectMeta?.supportsSecondaryColor}
                            <div class="ctrl-field color-field">
                              <label for={`sec-col-${lIdx}`}>Secondary Color</label>
                              <div class="color-picker-row">
                                <input
                                  id={`sec-col-${lIdx}`}
                                  type="color"
                                  value={rgbToHex(layer.secondaryColor)}
                                  oninput={(e) => {
                                    const rgb = hexToRgb((e.target as HTMLInputElement).value);
                                    if (rgb) layer.secondaryColor = rgb;
                                  }}
                                  class="native-color"
                                />
                                <input
                                  type="text"
                                  class="input-text hex-input"
                                  value={rgbToHex(layer.secondaryColor)}
                                  onchange={(e) => {
                                    const rgb = hexToRgb((e.target as HTMLInputElement).value);
                                    if (rgb) layer.secondaryColor = rgb;
                                  }}
                                />
                              </div>
                            </div>
                          {/if}

                          <!-- Palette Dropdown (when supported) -->
                          {#if effectMeta?.supportsPalette}
                            <div class="ctrl-field">
                              <label for={`pal-sel-${lIdx}`}>Palette</label>
                              <select id={`pal-sel-${lIdx}`} bind:value={layer.paletteId} class="input-select">
                                {#if !palettes.some(p => p.id === layer.paletteId)}
                                  <option value={layer.paletteId} disabled>Palette {layer.paletteId} (Unavailable on this node)</option>
                                {/if}
                                {#each palettes as pal}
                                  <option value={pal.id}>{pal.label}</option>
                                {/each}
                              </select>
                            </div>
                          {/if}
                        {/if}

                        <!-- Speed (when supported) -->
                        {#if effectMeta?.supportsSpeed}
                          <div class="ctrl-field">
                            <label for={`spd-${lIdx}`}>Speed ({(layer.speedMs / 1000).toFixed(1)} s)</label>
                            <input
                              id={`spd-${lIdx}`}
                              type="range"
                              min="10"
                              max="10000"
                              step="50"
                              bind:value={layer.speedMs}
                              class="range-slider"
                            />
                          </div>
                        {/if}

                        <!-- Effect Parameter (when supported) -->
                        {#if effectMeta?.hasParameter}
                          <div class="ctrl-field">
                            <label for={`param-${lIdx}`}>
                              {effectMeta.parameterLabel || 'Parameter'}
                              ({layer.fxParam}{effectMeta.parameterUnit ? ` ${effectMeta.parameterUnit}` : ''})
                            </label>
                            <input
                              id={`param-${lIdx}`}
                              type="range"
                              min={effectMeta.parameterMin ?? 0}
                              max={effectMeta.parameterMax ?? 255}
                              bind:value={layer.fxParam}
                              class="range-slider"
                            />
                          </div>
                        {/if}

                        {#if !isPwm}
                          <div class="ctrl-field">
                            <label for={`opac-${lIdx}`}>Opacity ({Math.round((layer.opacity / 255) * 100)}%)</label>
                            <input
                              id={`opac-${lIdx}`}
                              type="range"
                              min="0"
                              max="255"
                              bind:value={layer.opacity}
                              class="range-slider"
                            />
                          </div>
                        {/if}

                        <!-- Blend Mode -->
                        <div class="ctrl-field">
                          <label for={`blend-${lIdx}`}>Blend Mode</label>
                          <select id={`blend-${lIdx}`} bind:value={layer.blendMode} class="input-select">
                            {#each BLEND_MODES as bm}
                              <option value={bm}>{bm}</option>
                            {/each}
                          </select>
                        </div>
                      </div>
                    {/if}
                  </div>
                {/if}
              {/each}
            </div>
          </section>
        {:else if editorTab === 'motion'}
          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <!-- 5. MOTION & AUTOMATION PANEL (Requirement 30, 31, 32, 33)           -->
          <!-- ═══════════════════════════════════════════════════════════════════ -->
          <section class="motion-section">
            <div class="motion-header">
              <div>
                <h3>Automation Tracks ({workingDocument.motion.tracks.length}/4)</h3>
                <p class="motion-subtext">Dynamic curve timelines synchronized to the scene cycle.</p>
              </div>
              <button
                class="btn-primary btn-sm"
                onclick={addMotionTrack}
                disabled={workingDocument.motion.tracks.length >= 4}
              >
                + Add Track
              </button>
            </div>

            {#if workingDocument.motion.tracks.length === 0}
              <div class="no-tracks-card">
                <p>No automation tracks in this scene.</p>
                <button class="btn-secondary btn-sm" onclick={addMotionTrack}>+ Add First Automation Track</button>
              </div>
            {:else}
              <div class="tracks-list">
                {#each workingDocument.motion.tracks as track, tIdx}
                  <AutomationTrackEditor
                    bind:track={workingDocument.motion.tracks[tIdx]}
                    trackIndex={tIdx}
                    {effects}
                    channels={activeChannels}
                    onDelete={() => deleteMotionTrack(tIdx)}
                  />
                {/each}
              </div>
            {/if}
          </section>
        {/if}
      {:else}
        <div class="no-scene-selected">
          <p>Select a scene from the library or create a new one to begin editing.</p>
          <button class="btn-primary" onclick={openNewSceneModal}>Create New Scene</button>
        </div>
      {/if}
    </main>
  </div>
{/if}

<!-- ═══════════════════════════════════════════════════════════════════════════ -->
<!-- MODAL DIALOGS                                                              -->
<!-- ═══════════════════════════════════════════════════════════════════════════ -->

<!-- 1. New Scene Free Slot Selector Dialog (Requirement 13) -->
{#if showNewSceneModal}
  <OperatorDialog
    open
    title="Create New Scene"
    onClose={() => (showNewSceneModal = false)}
  >
    <div class="modal-body-pad">
      <div class="modal-field">
        <div class="modal-field-label" id="slot-selector-label">Available Slots (01–32)</div>
        <div class="slot-chips-grid" aria-labelledby="slot-selector-label">
          {#each freeSlots as slot}
            <button
              type="button"
              class="slot-chip"
              class:selected={newSceneSlot === slot}
              onclick={() => (newSceneSlot = slot)}
            >
              {(slot + 1).toString().padStart(2, '0')}
            </button>
          {/each}
        </div>
      </div>

      <div class="modal-field">
        <div class="modal-field-label">Starting Point</div>
        <div class="radio-row">
          <label class="radio-lbl">
            <input type="radio" bind:group={newSceneMode} value="blank" />
            <span>Blank Scene</span>
          </label>
          <label class="radio-lbl">
            <input type="radio" bind:group={newSceneMode} value="duplicate" disabled={!workingDocument} />
            <span>Duplicate Current Scene</span>
          </label>
        </div>
      </div>

      <div class="modal-actions-right">
        <button type="button" data-dialog-cancel class="btn-secondary" onclick={() => (showNewSceneModal = false)}>Cancel</button>
        <button type="button" class="btn-primary" onclick={confirmNewScene} disabled={newSceneSlot === null}>
          Create Scene
        </button>
      </div>
    </div>
  </OperatorDialog>
{/if}

<!-- 2. Delete Scene Confirmation Dialog with Dependency Guard (Requirement 34, 35, 36) -->
{#if showDeleteModal && deleteTargetIdx !== null}
  <OperatorDialog
    open
    busy={deletingScene}
    title={`Delete Scene ${(deleteTargetIdx + 1).toString().padStart(2, '0')}?`}
    onClose={() => (showDeleteModal = false)}
  >
    <div class="modal-body-pad">
      {#if checkingReferences}
        <div class="loading-refs">
          <div class="spinner-sm"></div>
          <p>Evaluating scene dependencies...</p>
        </div>
      {:else if !deleteReferences}
        <div class="blocked-delete-box" role="alert">
          <p>Scene dependencies could not be loaded. Close and retry before deleting.</p>
        </div>
      {:else if isSceneBlockedFromDelete}
        <div class="blocked-delete-box" role="alert">
          <h4>Scene cannot be deleted</h4>
          <p>This scene is currently referenced by show dependencies:</p>
          <ul>
            {#if deleteReferences?.active}
              <li>Currently active on node live output</li>
            {/if}
            {#if deleteReferences?.fallback}
              <li>Configured as fallback preset</li>
            {/if}
            {#if deleteReferences && deleteReferences.cues.length > 0}
              <li>Used in Cue List: {deleteReferences.cues.map(c => `List ${c}`).join(', ')}</li>
            {/if}
            {#if deleteReferences && deleteReferences.timelineTracks.length > 0}
              <li>Used in Timeline Tracks: {deleteReferences.timelineTracks.join(', ')}</li>
            {/if}
            {#if deleteReferences && deleteReferences.schedules.length > 0}
              <li>Used in Calendar Schedules: {deleteReferences.schedules.join(', ')}</li>
            {/if}
          </ul>
          <p class="resolve-hint">Please resolve or reassign these references before deleting.</p>
        </div>
      {:else}
        <p class="delete-confirm-text">
          Are you sure you want to delete <strong>“{presets.find(p => p.index === deleteTargetIdx)?.name || `Scene ${deleteTargetIdx + 1}`}”</strong>?
          This will permanently remove both Scene and Motion data from the node.
        </p>
      {/if}
      <div class="modal-actions-right">
        <button type="button" data-dialog-cancel class="btn-secondary" onclick={() => (showDeleteModal = false)} disabled={deletingScene}>
          {!checkingReferences && (!deleteReferences || isSceneBlockedFromDelete) ? 'Close' : 'Cancel'}
        </button>
        {#if !checkingReferences && deleteReferences && !isSceneBlockedFromDelete}
          <button type="button" class="btn-danger" onclick={confirmDeleteScene} disabled={deletingScene}>
            {deletingScene ? 'Deleting...' : 'Delete Scene'}
          </button>
        {/if}
      </div>
    </div>
  </OperatorDialog>
{/if}

<!-- 3. Unsaved Changes Navigation Modal (Requirement 40) -->
{#if showUnsavedModal}
  <OperatorDialog
    open
    title="Unsaved Scene Changes"
    onClose={cancelDiscard}
  >
    <div class="modal-body-pad">
      <p>You have unsaved changes in the current scene document. If you proceed, these changes will be discarded.</p>
      <div class="modal-actions-right">
        <button type="button" data-dialog-cancel class="btn-secondary" onclick={cancelDiscard}>Stay on Scene</button>
        <button type="button" class="btn-danger" onclick={confirmDiscard}>Discard Changes</button>
      </div>
    </div>
  </OperatorDialog>
{/if}

{#if toast}
  <div class="toast" role="status">{toast}</div>
{/if}

{#if error}
  <div class="error-banner" role="alert">
    <span>{error}</span>
    <button class="err-close" onclick={() => (error = '')}>✕</button>
  </div>
{/if}

<style>
  .scene-root {
    display: flex;
    height: calc(100vh - 64px);
    background: #090d16;
    color: #f1f5f9;
    overflow: hidden;
    font-family: inherit;
  }

  .loading-box {
    margin: auto;
    text-align: center;
    color: #94a3b8;
  }

  .spinner {
    width: 36px;
    height: 36px;
    border: 3px solid rgba(255, 255, 255, 0.1);
    border-top-color: #6366f1;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin: 0 auto 12px;
  }

  .spinner-sm {
    width: 20px;
    height: 20px;
    border: 2px solid rgba(255, 255, 255, 0.1);
    border-top-color: #6366f1;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  /* ── 1. Library Sidebar ─────────────────────────────────────────────────── */
  .library {
    width: 280px;
    min-width: 280px;
    background: #0f1422;
    border-right: 1px solid rgba(255, 255, 255, 0.08);
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .lib-header {
    padding: 14px 16px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .lib-title-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  .lib-title-row h2 {
    font-size: 1rem;
    font-weight: 700;
    margin: 0;
    color: #f8fafc;
  }

  .lib-count {
    font-size: 0.75rem;
    color: #64748b;
  }

  .lib-header-actions {
    display: flex;
    gap: 8px;
  }

  .btn-import {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #cbd5e1;
    border-radius: 6px;
    padding: 5px 10px;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .btn-import:hover {
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
  }

  .lib-list {
    flex: 1;
    overflow-y: auto;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .empty-library-hint {
    padding: 24px 12px;
    text-align: center;
    color: #64748b;
    font-size: 0.85rem;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
  }

  .scene-card {
    background: rgba(255, 255, 255, 0.025);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 8px;
    padding: 10px;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    gap: 6px;
    transition: all 0.15s ease;
  }

  .scene-card:hover {
    background: rgba(255, 255, 255, 0.05);
    border-color: rgba(255, 255, 255, 0.15);
  }

  .scene-card.selected {
    background: rgba(99, 102, 241, 0.08);
    border-color: #6366f1;
  }

  .scene-card.active {
    box-shadow: 0 0 10px rgba(99, 102, 241, 0.2);
  }

  .card-top-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .card-name {
    font-size: 0.85rem;
    font-weight: 600;
    color: #f1f5f9;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .card-slot {
    font-size: 0.72rem;
    font-family: monospace;
    color: #64748b;
  }

  .card-preview {
    margin: 2px 0;
  }

  .card-meta-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .card-meta {
    font-size: 0.72rem;
    color: #94a3b8;
  }

  .badge-active-live {
    font-size: 0.65rem;
    font-weight: 700;
    color: #10b981;
    background: rgba(16, 185, 129, 0.12);
    border: 1px solid rgba(16, 185, 129, 0.3);
    padding: 1px 5px;
    border-radius: 4px;
    letter-spacing: 0.04em;
  }

  /* ── 2. Editor Workspace ────────────────────────────────────────────────── */
  .editor {
    flex: 1;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    background: #090d16;
  }

  .sync-follower-banner {
    display: flex;
    align-items: center;
    gap: 12px;
    background: rgba(59, 130, 246, 0.1);
    border-bottom: 1px solid rgba(59, 130, 246, 0.3);
    padding: 10px 20px;
    color: #93c5fd;
    font-size: 0.82rem;
  }

  .conflict-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    background: rgba(239, 68, 68, 0.12);
    border-bottom: 1px solid rgba(239, 68, 68, 0.4);
    padding: 10px 20px;
    color: #fca5a5;
    font-size: 0.82rem;
  }

  .conflict-actions {
    display: flex;
    gap: 8px;
  }

  .btn-conflict-reload {
    background: #ef4444;
    color: #fff;
    border: none;
    border-radius: 6px;
    padding: 6px 12px;
    font-size: 0.78rem;
    font-weight: 600;
    cursor: pointer;
  }

  .btn-conflict-draft {
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
    border: 1px solid rgba(255, 255, 255, 0.2);
    border-radius: 6px;
    padding: 6px 12px;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .apply-changes-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    background: rgba(16, 185, 129, 0.1);
    border-bottom: 1px solid rgba(16, 185, 129, 0.3);
    padding: 10px 20px;
    color: #6ee7b7;
    font-size: 0.82rem;
  }

  .btn-apply-changes {
    background: #10b981;
    color: #042f2e;
    border: none;
    font-weight: 700;
    border-radius: 6px;
    padding: 6px 14px;
    font-size: 0.8rem;
    cursor: pointer;
  }

  .ed-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 24px;
    background: #0f1422;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }

  .ed-title-block {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .ed-name-row {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .ed-name-input {
    background: transparent;
    border: none;
    font-size: 1.25rem;
    font-weight: 700;
    color: #f8fafc;
    border-bottom: 1px dashed rgba(255, 255, 255, 0.3);
    padding: 2px 4px;
    font-family: inherit;
    outline: none;
  }

  .ed-name-input:focus {
    border-bottom-color: #6366f1;
  }

  .ed-slot-tag {
    font-size: 0.75rem;
    color: #64748b;
    background: rgba(255, 255, 255, 0.05);
    padding: 3px 8px;
    border-radius: 4px;
    font-family: monospace;
  }

  .name-bytes-counter {
    font-size: 0.7rem;
    color: #64748b;
  }

  .name-bytes-counter.warn {
    color: #ef4444;
    font-weight: 700;
  }

  .ed-status-pills {
    display: flex;
    gap: 6px;
    align-items: center;
  }

  .sc-pill {
    font-size: 0.68rem;
    font-weight: 700;
    padding: 2px 7px;
    border-radius: 4px;
    letter-spacing: 0.04em;
  }

  .pill-active {
    color: #10b981;
    background: rgba(16, 185, 129, 0.15);
    display: flex;
    align-items: center;
    gap: 5px;
  }

  .dot-pulse {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #10b981;
    box-shadow: 0 0 6px #10b981;
  }

  .pill-edited {
    color: #f59e0b;
    background: rgba(245, 158, 11, 0.15);
  }

  .pill-saved {
    color: #94a3b8;
    background: rgba(255, 255, 255, 0.08);
  }

  .pill-new {
    color: #38bdf8;
    background: rgba(56, 189, 248, 0.15);
  }

  .pill-motion {
    color: #a5b4fc;
    background: rgba(99, 102, 241, 0.12);
  }

  .ed-header-actions {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .header-menu {
    display: flex;
    gap: 6px;
    margin-left: 6px;
  }

  .btn-icon {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #cbd5e1;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 0.78rem;
    cursor: pointer;
  }

  .btn-icon:hover {
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
  }

  /* ── Global Scene Controls ──────────────────────────────────────────────── */
  .global-scene-strip {
    display: flex;
    gap: 24px;
    padding: 14px 24px;
    background: rgba(255, 255, 255, 0.02);
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    flex-wrap: wrap;
    align-items: center;
  }

  .global-field {
    display: flex;
    flex-direction: column;
    gap: 4px;
    min-width: 220px;
  }

  .global-field label {
    font-size: 0.75rem;
    color: #94a3b8;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .slider-row {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .slider-val {
    font-size: 0.85rem;
    font-weight: 600;
    color: #f1f5f9;
    min-width: 40px;
  }

  .global-transition {
    flex: 1;
    min-width: 320px;
  }

  /* ── Tabs Bar ───────────────────────────────────────────────────────────── */
  .editor-tabs-bar {
    display: flex;
    gap: 2px;
    padding: 0 24px;
    background: #0f1422;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }

  .tab-btn {
    background: transparent;
    border: none;
    color: #94a3b8;
    padding: 10px 16px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    border-bottom: 2px solid transparent;
  }

  .tab-btn.active {
    color: #6366f1;
    border-bottom-color: #6366f1;
  }

  /* ── 3. Canvas Section ─────────────────────────────────────────────────── */
  .canvas-section {
    padding: 16px 24px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }

  .canvas-toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 10px;
  }

  .canvas-label {
    font-size: 0.9rem;
    font-weight: 700;
    color: #f8fafc;
  }

  .preview-subtext {
    font-size: 0.72rem;
    color: #64748b;
    font-weight: normal;
    margin-left: 6px;
  }

  .selection-controls {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .sel-count {
    font-size: 0.75rem;
    color: #94a3b8;
  }

  .btn-sel-chip {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #cbd5e1;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 0.72rem;
    cursor: pointer;
  }

  .btn-sel-chip:hover {
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
  }

  .banks-container {
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .bank-group {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .bank-title {
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    color: #64748b;
    font-weight: 600;
  }

  .bank-grid {
    display: grid;
    grid-template-columns: repeat(8, 1fr);
    gap: 8px;
  }

  @media (max-width: 1024px) {
    .bank-grid {
      grid-template-columns: repeat(4, 1fr);
    }
  }

  .channel-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    padding: 6px 8px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .channel-card:hover {
    border-color: rgba(255, 255, 255, 0.2);
  }

  .channel-card.selected {
    border-color: #6366f1;
    background: rgba(99, 102, 241, 0.1);
  }

  .ch-head {
    display: flex;
    justify-content: space-between;
    font-size: 0.72rem;
    color: #94a3b8;
  }

  .ch-port {
    font-weight: 700;
    color: #f1f5f9;
  }

  .ch-swatch {
    height: 20px;
    border-radius: 3px;
    border: 1px solid rgba(255, 255, 255, 0.1);
  }

  .pwm-bar-track {
    height: 20px;
    background: rgba(255, 255, 255, 0.05);
    border-radius: 3px;
    display: flex;
    align-items: flex-end;
    overflow: hidden;
  }

  .pwm-bar-fill {
    width: 100%;
    background: #e2e8f0;
    transition: height 0.15s ease;
  }

  .ch-name {
    font-size: 0.68rem;
    color: #64748b;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  /* ── 4. Inspector Section ───────────────────────────────────────────────── */
  .inspector-section {
    padding: 16px 24px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .inspector-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 10px;
  }

  .ins-title-row {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .ins-title-row h3 {
    margin: 0;
    font-size: 1.1rem;
    font-weight: 700;
    color: #f8fafc;
  }

  .batch-badge {
    font-size: 0.75rem;
    color: #a5b4fc;
    background: rgba(99, 102, 241, 0.15);
    padding: 3px 8px;
    border-radius: 4px;
  }

  .batch-actions-strip {
    display: flex;
    gap: 8px;
  }

  .btn-batch {
    background: rgba(99, 102, 241, 0.12);
    border: 1px solid rgba(99, 102, 241, 0.3);
    color: #a5b4fc;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 0.72rem;
    cursor: pointer;
  }

  .btn-batch:hover {
    background: rgba(99, 102, 241, 0.25);
    color: #fff;
  }

  .layers-stack {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .layer-card {
    background: rgba(255, 255, 255, 0.025);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    padding: 12px 16px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .layer-card.disabled {
    opacity: 0.55;
  }

  .layer-header-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .layer-toggle-wrap {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .layer-title {
    font-size: 0.88rem;
    font-weight: 700;
    color: #f1f5f9;
    cursor: pointer;
  }

  .layer-quick-actions {
    display: flex;
    gap: 6px;
  }

  .btn-layer-act {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: #94a3b8;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 0.7rem;
    cursor: pointer;
  }

  .btn-layer-act:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
  }

  .btn-layer-act:disabled {
    opacity: 0.3;
    cursor: not-allowed;
  }

  .btn-clear:hover {
    color: #ef4444;
  }

  .layer-controls-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 12px;
    padding-top: 4px;
  }

  .ctrl-field {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }

  .ctrl-field label {
    font-size: 0.72rem;
    color: #94a3b8;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .input-select, .input-text {
    min-height: 32px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #fff;
    padding: 4px 8px;
    font-size: 0.82rem;
    font-family: inherit;
    box-sizing: border-box;
  }

  .range-slider {
    width: 100%;
    accent-color: #6366f1;
  }

  .pwm-warn-badge {
    font-size: 0.68rem;
    color: #f59e0b;
    margin-top: 2px;
  }

  .color-picker-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .native-color {
    width: 32px;
    height: 32px;
    padding: 0;
    border: none;
    border-radius: 4px;
    background: transparent;
    cursor: pointer;
  }

  .hex-input {
    width: 90px;
    font-family: monospace;
  }

  .recent-colors-row {
    display: flex;
    gap: 4px;
    margin-top: 4px;
  }

  .swatch-btn {
    width: 16px;
    height: 16px;
    border-radius: 3px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    cursor: pointer;
    padding: 0;
  }

  /* ── 5. Motion Section ──────────────────────────────────────────────────── */
  .motion-section {
    padding: 16px 24px;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  .motion-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .motion-header h3 {
    margin: 0;
    font-size: 1.1rem;
    font-weight: 700;
    color: #f8fafc;
  }

  .motion-subtext {
    font-size: 0.78rem;
    color: #64748b;
    margin: 2px 0 0;
  }

  .no-tracks-card {
    background: rgba(255, 255, 255, 0.02);
    border: 1px dashed rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    padding: 32px;
    text-align: center;
    color: #64748b;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
  }

  .tracks-list {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  /* ── General Buttons & Modals ───────────────────────────────────────────── */
  .btn-primary {
    background: #6366f1;
    color: #fff;
    border: none;
    border-radius: 6px;
    padding: 8px 16px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease;
  }

  .btn-primary:hover:not(:disabled) {
    background: #4f46e5;
  }

  .btn-primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .btn-primary.btn-sm {
    padding: 5px 12px;
    font-size: 0.78rem;
  }

  .btn-secondary {
    background: rgba(255, 255, 255, 0.08);
    color: #e2e8f0;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 6px;
    padding: 8px 14px;
    font-size: 0.85rem;
    cursor: pointer;
  }

  .btn-secondary:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.12);
    color: #fff;
  }

  .btn-danger {
    background: #ef4444;
    color: #fff;
    border: none;
    border-radius: 6px;
    padding: 8px 14px;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
  }

  .btn-danger:hover:not(:disabled) {
    background: #dc2626;
  }

  .no-scene-selected {
    margin: auto;
    text-align: center;
    color: #64748b;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
  }

  .modal-body-pad {
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .modal-field {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .modal-field label,
  .modal-field-label {
    font-size: 0.78rem;
    color: #94a3b8;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .slot-chips-grid {
    display: grid;
    grid-template-columns: repeat(8, 1fr);
    gap: 6px;
    max-height: 160px;
    overflow-y: auto;
    padding: 4px;
    background: rgba(0, 0, 0, 0.2);
    border-radius: 6px;
  }

  .slot-chip {
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #cbd5e1;
    border-radius: 4px;
    padding: 6px 4px;
    font-family: monospace;
    font-size: 0.8rem;
    cursor: pointer;
  }

  .slot-chip:hover {
    background: rgba(255, 255, 255, 0.12);
  }

  .slot-chip.selected {
    background: #6366f1;
    border-color: #6366f1;
    color: #fff;
    font-weight: 700;
  }

  .radio-row {
    display: flex;
    gap: 16px;
  }

  .radio-lbl {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 0.85rem;
    color: #e2e8f0;
    cursor: pointer;
  }

  .modal-actions-right {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    margin-top: 8px;
  }

  .blocked-delete-box {
    background: rgba(239, 68, 68, 0.1);
    border: 1px solid rgba(239, 68, 68, 0.3);
    border-radius: 6px;
    padding: 12px;
    color: #fca5a5;
  }

  .blocked-delete-box h4 {
    margin: 0 0 6px;
    font-size: 0.9rem;
    color: #ef4444;
  }

  .blocked-delete-box ul {
    margin: 6px 0 8px 18px;
    padding: 0;
    font-size: 0.82rem;
  }

  .resolve-hint {
    font-size: 0.78rem;
    color: #fca5a5;
    margin: 4px 0 0;
    font-style: italic;
  }

  .delete-confirm-text {
    font-size: 0.88rem;
    color: #cbd5e1;
    margin: 0;
    line-height: 1.5;
  }

  .loading-refs {
    display: flex;
    align-items: center;
    gap: 10px;
    color: #94a3b8;
    padding: 12px 0;
  }

  .toast {
    position: fixed;
    bottom: 24px;
    right: 24px;
    background: #1e293b;
    border: 1px solid #334155;
    color: #f8fafc;
    padding: 10px 18px;
    border-radius: 8px;
    font-size: 0.85rem;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.3);
    z-index: 1000;
  }

  .error-banner {
    position: fixed;
    top: 24px;
    right: 24px;
    background: #7f1d1d;
    border: 1px solid #b91c1c;
    color: #fef2f2;
    padding: 10px 16px;
    border-radius: 8px;
    font-size: 0.85rem;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.4);
    z-index: 1000;
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .err-close {
    background: none;
    border: none;
    color: #fca5a5;
    cursor: pointer;
    font-size: 0.9rem;
  }
</style>
