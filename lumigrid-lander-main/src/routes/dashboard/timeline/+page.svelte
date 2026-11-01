<script lang="ts">
  import { onMount } from 'svelte';
  import { beforeNavigate, goto } from '$app/navigation';
  import { nodeStore, captureNodeOperation } from '$lib/state/nodeStore';
  import { nodeAuthStore } from '$lib/auth/nodeAuth';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import { getPresets, type ScenePreset } from '$lib/api/scenes';
  import {
    timelineEditor,
    isTimelineEditorDirty,
    timelinePixelsPerBeat,
  } from '$lib/timeline/editorStore';
  import { getTrackTotalBeats } from '$lib/timeline/evaluation';
  import { validateTimelineDocument } from '$lib/timeline/validation';
  import type { TimelineDocument } from '$lib/api/timeline';

  import TimelineToolbar from '$lib/components/timeline/TimelineToolbar.svelte';
  import TimelineRuler from '$lib/components/timeline/TimelineRuler.svelte';
  import TimelineTrack from '$lib/components/timeline/TimelineTrack.svelte';
  import TimelineBlockInspector from '$lib/components/timeline/TimelineBlockInspector.svelte';
  import TimelineScenePicker from '$lib/components/timeline/TimelineScenePicker.svelte';
  import TimelinePlayhead from '$lib/components/timeline/TimelinePlayhead.svelte';
  import TimelineConflictDialog from '$lib/components/timeline/TimelineConflictDialog.svelte';
  import TimelineImportDialog from '$lib/components/timeline/TimelineImportDialog.svelte';

  const snapshot = $derived($nodeStore.snapshot);
  const snapSync = $derived(snapshot?.sync);
  const isFollower = $derived(
    snapSync?.activeRole === 'worker' || snapSync?.activeRole === 'follower'
  );
  const syncLeaderAddress = $derived(snapSync?.masterAddress || 'Leader');

  const working = $derived($timelineEditor.working);
  const isDirty = $derived($isTimelineEditorDirty);
  const pixelsPerBeat = $derived($timelinePixelsPerBeat);
  const selectedChannel = $derived($timelineEditor.selectedChannel);
  const selectedStep = $derived($timelineEditor.selectedStep);

  let scenes = $state<Map<number, ScenePreset>>(new Map());
  let existingSceneSlots = $state<Set<number>>(new Set());
  let scenesLoading = $state(false);
  let scenesLoaded = $state(false);
  let scenesError = $state('');
  let clock = $state(Date.now());
  const statusFresh = $derived(snapshot !== null && clock - $nodeStore.lastSnapshotAt <= 10000 &&
    ($nodeStore.connection === 'online' || $nodeStore.connection === 'degraded'));

  // Dialog states
  let isScenePickerOpen = $state(false);
  let scenePickerChannel = $state(0);
  let scenePickerTargetStep = $state<number | null>(null);
  let isImportDialogOpen = $state(false);
  let dialogOwner: ReturnType<typeof captureWorkspaceOperation> | null = null;

  // Horizontal scroll sync
  let scrollContainer: HTMLElement | null = $state(null);
  let scrollLeft = $state(0);

  function handleScroll(e: Event) {
    scrollLeft = (e.target as HTMLElement).scrollLeft;
  }

  // Selected block derived info
  const selectedBlockInfo = $derived.by(() => {
    if (selectedChannel === null || selectedStep === null || !working) {
      return null;
    }
    const track = working.tracks[selectedChannel];
    if (!track || selectedStep < 0 || selectedStep >= track.steps.length) {
      return null;
    }
    const step = track.steps[selectedStep];
    const scene = scenes.get(step.presetIndex);
    return {
      channelId: selectedChannel,
      stepIndex: selectedStep,
      step,
      scene,
      canMoveLeft: selectedStep > 0,
      canMoveRight: selectedStep < track.steps.length - 1,
      canDuplicate: track.steps.length < 16,
    };
  });

  // Calculate max beats across all tracks to size ruler
  const maxBeats = $derived.by(() => {
    if (!working) return 32;
    let max = 16;
    for (const t of working.tracks) {
      const beats = getTrackTotalBeats(t);
      if (beats > max) max = beats;
    }
    return Math.max(32, max);
  });

  // Polyrhythmic check
  const polyrhythmicNotice = $derived.by(() => {
    if (!working) return null;
    const activeLengths = new Set<number>();
    for (const t of working.tracks) {
      if (t.enabled && t.numSteps > 0) {
        activeLengths.add(getTrackTotalBeats(t));
      }
    }
    if (activeLengths.size > 1) {
      return 'Tracks have independent loop lengths';
    }
    return null;
  });

  // Validation check against existing scenes
  const validationResult = $derived.by(() => {
    if (!working) return { valid: true, errors: [], missingScenes: [] };
    return validateTimelineDocument(working, scenesLoaded ? existingSceneSlots : undefined);
  });
  const canSave = $derived($timelineEditor.loaded && !$timelineEditor.loading &&
    scenesLoaded && !scenesLoading && validationResult.valid);

  let activeNodeIp = $state('');
  let activeSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let workspaceOwner: ReturnType<typeof captureNodeOperation> | null = null;
  let workspaceGeneration = $state(0);
  let libraryRequest = 0;
  let disposed = false;

  function captureWorkspaceOperation() {
    const owner = captureNodeOperation(activeNodeIp);
    const generation = workspaceGeneration;
    return { ip: owner.ip, isCurrent: () => !disposed && generation === workspaceGeneration && owner.isCurrent() };
  }

  async function loadNodeData(preserveDraft = true) {
    if (!activeNodeIp) return;
    const owner = captureWorkspaceOperation();
    const request = ++libraryRequest;
    const isCurrent = () => owner.isCurrent() && request === libraryRequest;
    scenesLoading = true;
    scenesLoaded = false;
    scenesError = '';
    timelineEditor.setSceneSlots(null);
    const documentLoad = timelineEditor.load($nodeStore.nodeId || owner.ip, owner.ip, preserveDraft);
    try {
      const presets = await getPresets(owner.ip);
      if (!isCurrent()) return;
      const sceneMap = new Map<number, ScenePreset>();
      const slots = new Set<number>();
      for (const p of presets) {
        sceneMap.set(p.index, p);
        slots.add(p.index);
      }
      scenes = sceneMap;
      existingSceneSlots = slots;
      scenesLoaded = true;
      timelineEditor.setSceneSlots(slots);
    } catch (error) {
      if (isCurrent()) scenesError = error instanceof Error ? error.message : 'Could not load the scene library.';
    } finally {
      if (isCurrent()) scenesLoading = false;
    }
    await documentLoad;
  }

  function syncFocusedNode(ip: string) {
    const session = nodeAuthStore.getSession(ip);
    const changedOwner = ip !== activeNodeIp || session !== activeSession;
    if (!changedOwner && workspaceOwner?.isCurrent()) return;
    activeNodeIp = ip;
    activeSession = session;
    ++workspaceGeneration;
    ++libraryRequest;
    workspaceOwner = captureNodeOperation(ip);
    isScenePickerOpen = isImportDialogOpen = showUnsavedModal = false;
    dialogOwner = null;
    pendingDestination = null;
    scenesError = '';
    scenesLoaded = false;
    if (changedOwner || !ip) {
      scenes = new Map();
      existingSceneSlots = new Set();
      timelineEditor.reset();
    }
    if (ip) void loadNodeData(!changedOwner);
    else scenesLoading = false;
  }

  // Navigation guard
  let showUnsavedModal = $state(false);
  let pendingDestination: string | null = null;

  beforeNavigate((navigation) => {
    if (!$isTimelineEditorDirty) return;
    navigation.cancel();
    // Leaving the app uses the browser's own unload confirmation.
    if (navigation.willUnload) return;
    pendingDestination = navigation.to?.url?.href ?? null;
    showUnsavedModal = true;
  });

  function confirmDiscardNavigation() {
    const destination = pendingDestination;
    pendingDestination = null;
    showUnsavedModal = false;
    timelineEditor.reset();
    if (destination) void goto(destination);
  }

  function cancelDiscardNavigation() {
    showUnsavedModal = false;
    pendingDestination = null;
  }

  onMount(() => {
    disposed = false;
    const timer = setInterval(() => { clock = Date.now(); }, 1000);
    const beforeUnload = (event: BeforeUnloadEvent) => {
      if (isDirty) { event.preventDefault(); event.returnValue = ''; }
    };
    window.addEventListener('beforeunload', beforeUnload);
    const unsubscribe = nodeStore.subscribe(state => syncFocusedNode(state.route || ''));
    return () => {
      disposed = true;
      ++workspaceGeneration;
      ++libraryRequest;
      unsubscribe();
      clearInterval(timer);
      window.removeEventListener('beforeunload', beforeUnload);
      timelineEditor.reset();
    };
  });

  // Block Inspector Handlers
  function handleUpdateLength(beats: number) {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.resizeBlock(selectedChannel, selectedStep, beats);
    }
  }

  function handleOpenChangeScene() {
    if (scenesLoaded && selectedChannel !== null && selectedStep !== null) {
      dialogOwner = captureWorkspaceOperation();
      scenePickerChannel = selectedChannel;
      scenePickerTargetStep = selectedStep;
      isScenePickerOpen = true;
    }
  }

  function handleDuplicate() {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.duplicateBlock(selectedChannel, selectedStep);
    }
  }

  function handleDelete() {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.deleteBlock(selectedChannel, selectedStep);
    }
  }

  function handleMoveLeft() {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.moveBlockLeft(selectedChannel, selectedStep);
    }
  }

  function handleMoveRight() {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.moveBlockRight(selectedChannel, selectedStep);
    }
  }

  function handleCopy() {
    if (selectedChannel !== null && selectedStep !== null) {
      timelineEditor.copyBlock(selectedChannel, selectedStep);
    }
  }

  function handleCloseInspector() {
    timelineEditor.selectStep(null, null);
  }

  // Track Action Handlers
  function handleOpenAddScene(channelId: number) {
    if (!scenesLoaded) return;
    dialogOwner = captureWorkspaceOperation();
    scenePickerChannel = channelId;
    scenePickerTargetStep = null;
    isScenePickerOpen = true;
  }

  function handleScenePicked(presetIndex: number) {
    if (!dialogOwner?.isCurrent() || !scenesLoaded || !existingSceneSlots.has(presetIndex)) return;
    if (scenePickerTargetStep !== null) {
      timelineEditor.replaceBlockScene(scenePickerChannel, scenePickerTargetStep, presetIndex);
    } else {
      // Add new block
      timelineEditor.addBlock(scenePickerChannel, presetIndex, 4);
    }
    isScenePickerOpen = false;
  }

  function openImportDialog() {
    if (!$timelineEditor.loaded || !scenesLoaded) return;
    dialogOwner = captureWorkspaceOperation();
    isImportDialogOpen = true;
  }

  function handleImportDocument(doc: TimelineDocument) {
    if (!dialogOwner?.isCurrent() || !isImportDialogOpen) return;
    timelineEditor.importDocument(doc);
  }

  // Export clean JSON document
  function handleExportTimeline() {
    if (!working) return;
    const cleanDoc = {
      schema: 2,
      type: 'lumigrid-timeline',
      bpm: working.bpm,
      tracks: working.tracks.map((t) => ({
        channelId: t.channelId,
        numSteps: t.numSteps,
        enabled: t.enabled,
        steps: t.steps.slice(0, t.numSteps).map((s) => ({
          presetIndex: s.presetIndex,
          lengthBeats: s.lengthBeats,
        })),
      })),
    };

    const blob = new Blob([JSON.stringify(cleanDoc, null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `timeline-export-bpm${working.bpm}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }
</script>

<div class="timeline-studio-page">
  {#if scenesError || $timelineEditor.error}
    <div class="load-error" role="alert">
      {#if scenesError}<p>Scene library unavailable: {scenesError}</p>{/if}
      {#if $timelineEditor.error}<p>{$timelineEditor.error}</p>{/if}
      {#if !scenesLoaded || !$timelineEditor.loaded}
        <button type="button" class="dialog-btn dialog-secondary" disabled={scenesLoading || $timelineEditor.loading} onclick={() => loadNodeData(true)}>Retry loading</button>
      {/if}
    </div>
  {/if}
  <!-- Follower Notice Banner -->
  {#if isFollower}
    <div class="follower-banner" role="status">
      <span class="follower-icon">⚡</span>
      <span class="follower-text">
        <strong>PLAYBACK CONTROLLED BY SYNC LEADER</strong> ({syncLeaderAddress}) — You may edit and save timeline patterns, but live transport is driven by the cluster leader.
      </span>
    </div>
  {/if}

  <!-- Missing Scene Warning Banner -->
  {#if validationResult.missingScenes.length > 0}
    <div class="missing-scene-banner" role="alert">
      <span class="banner-icon">⚠️</span>
      <div class="banner-content">
        <strong>{validationResult.missingScenes.length} Timeline blocks reference missing scenes:</strong>
        <span class="banner-sub">
          Saving is blocked until missing scenes are resolved or blocks are removed.
        </span>
      </div>
    </div>
  {/if}

  <!-- Polyrhythmic Notice -->
  {#if polyrhythmicNotice}
    <div class="polyrhythmic-pill" title="Tracks have different loop lengths and will cycle independently">
      <span>{polyrhythmicNotice}</span>
    </div>
  {/if}

  <!-- Main Toolbar -->
  {#key workspaceGeneration}
    <TimelineToolbar
      {snapshot}
      {isFollower}
      {statusFresh}
      {canSave}
      libraryReady={scenesLoaded}
      onExport={handleExportTimeline}
      onImport={openImportDialog}
    />
  {/key}

  <!-- Sequencer Area -->
  <div class="sequencer-container">
    <!-- Synced Scroll Wrapper -->
    <div
      class="sequencer-scroll-wrapper"
      bind:this={scrollContainer}
      onscroll={handleScroll}
    >
      <!-- Beat Ruler -->
      <TimelineRuler {maxBeats} {pixelsPerBeat} {scrollLeft} />

      <!-- Tracks Container -->
      <div class="tracks-stack">
        <!-- Live Playhead Line -->
        <TimelinePlayhead
          snapshot={statusFresh ? snapshot : null}
          {pixelsPerBeat}
          {scrollLeft}
        />

        {#if working}
          {#each working.tracks as track, i}
            <TimelineTrack
              {track}
              channelId={i}
              {pixelsPerBeat}
              {scenes}
              {selectedChannel}
              {selectedStep}
              globalBeat={snapshot?.transport?.timeline?.currentBeat ?? 0}
              isTimelineRunning={statusFresh && (snapshot?.transport?.mode === 'timeline' || Boolean(snapshot?.transport?.timeline?.running))}
              onSelectBlock={(ch, step) => timelineEditor.selectStep(ch, step)}
              onResizeBlock={(ch, step, beats) => timelineEditor.resizeBlock(ch, step, beats)}
              onReorderBlock={(ch, from, to) => timelineEditor.reorderBlock(ch, from, to)}
              onToggleEnabled={(ch) => timelineEditor.toggleTrack(ch)}
              onOpenAddScene={handleOpenAddScene}
              onCopyTrack={(ch) => timelineEditor.copyTrack(ch)}
              onPasteTrack={(ch) => timelineEditor.pasteTrack(ch)}
              onClearTrack={(ch) => timelineEditor.clearTrack(ch)}
              canPasteTrack={true}
            />
          {/each}
        {:else}
          <div class="loading-placeholder">
            <span>{$timelineEditor.loading ? 'Loading saved timeline…' : activeNodeIp ? 'Saved timeline unavailable. Retry loading before editing or saving.' : 'Select a node to load its saved timeline.'}</span>
          </div>
        {/if}
      </div>
    </div>
  </div>

  <!-- Bottom Block Inspector -->
  {#if selectedBlockInfo}
    <TimelineBlockInspector
      channelId={selectedBlockInfo.channelId}
      stepIndex={selectedBlockInfo.stepIndex}
      step={selectedBlockInfo.step}
      scene={selectedBlockInfo.scene}
      bpm={working?.bpm ?? 120}
      canMoveLeft={selectedBlockInfo.canMoveLeft}
      canMoveRight={selectedBlockInfo.canMoveRight}
      canDuplicate={selectedBlockInfo.canDuplicate}
      onUpdateLength={handleUpdateLength}
      onOpenChangeScene={handleOpenChangeScene}
      onDuplicate={handleDuplicate}
      onDelete={handleDelete}
      onMoveLeft={handleMoveLeft}
      onMoveRight={handleMoveRight}
      onCopy={handleCopy}
      onClose={handleCloseInspector}
    />
  {/if}

  <!-- Modals -->
  {#if isScenePickerOpen}
    <TimelineScenePicker
      channelId={scenePickerChannel}
      {scenes}
      currentPresetIndex={scenePickerTargetStep !== null ? working?.tracks[scenePickerChannel]?.steps[scenePickerTargetStep]?.presetIndex : undefined}
      onSelect={handleScenePicked}
      onClose={() => (isScenePickerOpen = false)}
    />
  {/if}

  {#if $timelineEditor.conflict}
    <TimelineConflictDialog
      currentChecksum={$timelineEditor.conflictChecksum}
      busy={$timelineEditor.loading}
      error={$timelineEditor.error}
      onReload={() => timelineEditor.reloadNodeVersion()}
      onKeepDraft={() => timelineEditor.keepDraft()}
    />
  {/if}

  {#if isImportDialogOpen}
    <TimelineImportDialog
      {existingSceneSlots}
      onImportSuccess={handleImportDocument}
      onClose={() => (isImportDialogOpen = false)}
    />
  {/if}

  {#if showUnsavedModal}
    <OperatorDialog
      open
      title="Unsaved Timeline Changes"
      description="This draft has changes that are not saved to the node. Stay to keep editing, or discard them and continue."
      confirmLabel="Discard and leave"
      cancelLabel="Stay on page"
      tone="danger"
      onconfirm={confirmDiscardNavigation}
      oncancel={cancelDiscardNavigation}
    />
  {/if}
</div>

<style>
  .timeline-studio-page {
    display: flex;
    flex-direction: column;
    height: 100%;
    padding: 16px;
    box-sizing: border-box;
    background: #06090e;
    overflow-y: auto;
  }

  .follower-banner {
    display: flex;
    align-items: center;
    gap: 10px;
    background: rgba(14, 165, 233, 0.12);
    border: 1px solid rgba(14, 165, 233, 0.35);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 12px;
    color: #e0f2fe;
    font-size: 13px;
  }

  .follower-icon {
    font-size: 16px;
    color: #38bdf8;
  }

  .missing-scene-banner {
    display: flex;
    align-items: center;
    gap: 10px;
    background: rgba(245, 158, 11, 0.12);
    border: 1px solid rgba(245, 158, 11, 0.35);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 12px;
    color: #fef3c7;
    font-size: 13px;
  }

  .banner-icon {
    font-size: 18px;
  }

  .banner-content {
    display: flex;
    flex-direction: column;
  }

  .banner-sub {
    font-size: 11px;
    color: #fcd34d;
  }

  .polyrhythmic-pill {
    align-self: flex-start;
    font-size: 10px;
    font-weight: 700;
    color: #94a3b8;
    background: #0f172a;
    border: 1px solid #1e293b;
    padding: 3px 8px;
    border-radius: 4px;
    margin-bottom: 8px;
    letter-spacing: 0.04em;
    text-transform: uppercase;
  }

  .sequencer-container {
    flex: 1 1 auto;
    min-height: 280px;
    background: #0b0f19;
    border: 1px solid #1e293b;
    border-radius: 8px;
    overflow: hidden;
    display: flex;
    flex-direction: column;
  }

  .sequencer-scroll-wrapper {
    min-height: 0;
    overflow-x: auto;
    overflow-y: auto;
    position: relative;
    max-width: 100%;
  }

  .tracks-stack {
    position: relative;
    display: flex;
    flex-direction: column;
  }

  .loading-placeholder {
    padding: 60px;
    text-align: center;
    color: #64748b;
    font-size: 13px;
  }

  .load-error {
    color: var(--dash-danger);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-sm);
    background: var(--dash-surface);
    padding: 12px 16px;
    margin-bottom: 12px;
    overflow-wrap: anywhere;
  }
  .load-error p { margin: 4px 0 12px; }

  .dialog-btn {
    padding: 8px 16px;
    border-radius: 6px;
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    border: none;
    transition: opacity 0.15s ease;
  }

  .dialog-btn:hover {
    opacity: 0.9;
  }

  .dialog-secondary {
    background: var(--dash-surface);
    color: var(--dash-text);
    border: 1px solid var(--dash-border);
  }

</style>
