<script lang="ts">
  import { onDestroy } from 'svelte';
  import {
    timelineEditor,
    isTimelineEditorDirty,
    type TimelineZoomLevel,
  } from '$lib/timeline/editorStore';
  import {
    startTimeline,
    stopTimeline,
    restartTimeline,
    type ShowControlResponse,
  } from '$lib/api/showTransport';
  import type { RuntimeSnapshotV3 } from '$lib/state/runtimeSnapshot';
  import { nodeStore, captureNodeOperation } from '$lib/state/nodeStore';

  interface Props {
    snapshot: RuntimeSnapshotV3 | null;
    isFollower: boolean;
    statusFresh: boolean;
    canSave: boolean;
    libraryReady: boolean;
    onExport: () => void;
    onImport: () => void;
  }

  const { snapshot, isFollower, statusFresh, canSave, libraryReady, onExport, onImport }: Props = $props();

  const working = $derived($timelineEditor.working);
  const persisted = $derived($timelineEditor.persisted);
  const isDirty = $derived($isTimelineEditorDirty);
  const zoom = $derived($timelineEditor.zoom);
  const saving = $derived($timelineEditor.saving);
  const conflict = $derived($timelineEditor.conflict);

  // Runtime transport truth from canonical snapshot (LG-031 / LG-033)
  const isRunning = $derived(snapshot?.transport?.mode === 'timeline' || Boolean(snapshot?.transport?.timeline?.running));
  const runtimeBpm = $derived(snapshot?.transport?.timeline?.bpm);
  const contentStale = $derived(Boolean(snapshot?.transport?.timeline?.contentStale));
  const canControl = $derived(statusFresh && Boolean($nodeStore.nodeIp) && !isFollower);
  const canRunSaved = $derived(canControl && $timelineEditor.loaded && !$timelineEditor.loading &&
    !$timelineEditor.remoteChanged && persisted !== null);

  const bpmPresets = [60, 90, 120, 128, 140];

  let actionInProgress = $state(false);
  let actionError = $state('');
  let actionFeedback = $state('');
  let actionRequest = 0;
  let disposed = false;
  let confirmationTimer: ReturnType<typeof setTimeout> | null = null;
  let actionOwner = $state.raw<{ ip: string; isCurrent: () => boolean } | null>(null);
  let pendingTransport = $state.raw<{
    kind: 'start' | 'restart' | 'stop';
    response: ShowControlResponse;
    before: RuntimeSnapshotV3 | null;
    owner: ReturnType<typeof captureToolbarOperation>;
  } | null>(null);

  function captureToolbarOperation() {
    const operation = captureNodeOperation($nodeStore.nodeIp || '');
    const request = ++actionRequest;
    const loadGeneration = $timelineEditor.loadGeneration;
    const owner = { ip: operation.ip, isCurrent: () => !disposed && request === actionRequest &&
      loadGeneration === $timelineEditor.loadGeneration && operation.isCurrent() };
    actionOwner = owner;
    return owner;
  }

  function finishConfirmation() {
    if (confirmationTimer) clearTimeout(confirmationTimer);
    confirmationTimer = null;
    pendingTransport = null;
    actionInProgress = false;
  }

  function observeTransport() {
    const pending = pendingTransport;
    if (!pending || !pending.owner.isCurrent() || !statusFresh || !snapshot || snapshot === pending.before) return;
    const timeline = snapshot.transport.timeline;
    const matchesGeneration = typeof pending.response.generation === 'number' &&
      snapshot.transport.generation === pending.response.generation;
    const matchesState = pending.kind === 'stop'
      ? snapshot.transport.mode !== 'timeline' && !timeline.running
      : snapshot.transport.mode === 'timeline' && timeline.running &&
        (pending.response.checksum === undefined || timeline.activeChecksum === pending.response.checksum);
    if (!matchesGeneration || !matchesState) return;
    actionFeedback = pending.kind === 'stop' ? 'Stopped — confirmed by the node.' : 'Running saved timeline — confirmed by the node.';
    finishConfirmation();
  }

  $effect(() => {
    if (actionOwner && !actionOwner.isCurrent()) {
      finishConfirmation();
      actionOwner = null;
      actionError = actionFeedback = '';
    } else observeTransport();
  });

  async function requestTransport(kind: 'start' | 'restart' | 'stop', owner = captureToolbarOperation()) {
    if (!owner.isCurrent() || !canControl || (kind !== 'stop' && !canRunSaved)) return;
    actionInProgress = true;
    actionError = '';
    actionFeedback = 'Sending playback request…';
    const before = snapshot;
    try {
      const response = kind === 'stop' ? await stopTimeline(owner.ip)
        : kind === 'restart' ? await restartTimeline(owner.ip, persisted!.bpm)
          : await startTimeline(owner.ip, persisted!.bpm);
      if (!owner.isCurrent()) return;
      if (response.accepted !== true) throw new Error(response.error || 'The node did not accept the playback request.');
      pendingTransport = { kind, response, before, owner };
      actionFeedback = 'Accepted — waiting for fresh node confirmation…';
      confirmationTimer = setTimeout(() => {
        if (!owner.isCurrent()) return;
        finishConfirmation();
        actionFeedback = '';
        actionError = 'Playback was accepted, but no matching status arrived. Check the live status before trying again.';
      }, 12000);
      observeTransport();
    } catch (error) {
      if (!owner.isCurrent()) return;
      actionInProgress = false;
      actionFeedback = '';
      actionError = error instanceof Error ? error.message : 'Playback request failed.';
    }
  }

  async function handleRun() {
    if (actionInProgress || !canRunSaved) return;
    await requestTransport('start');
  }

  async function handleStop() {
    if (actionInProgress || !canControl) return;
    await requestTransport('stop');
  }

  async function handleRestartWithSaved() {
    if (actionInProgress || !canRunSaved) return;
    await requestTransport('restart');
  }

  async function handleSaveAndRun() {
    if (actionInProgress || !canSave || !canRunSaved) return;
    const owner = captureToolbarOperation();
    actionInProgress = true;
    actionError = actionFeedback = '';
    try {
      const ok = await timelineEditor.save();
      if (!owner.isCurrent()) return;
      if (ok && !$isTimelineEditorDirty && canRunSaved) {
        await requestTransport('start', owner);
        return;
      }
      if (ok) actionFeedback = 'Submitted version saved. Newer edits or changed live status need review before running.';
    } finally {
      if (owner.isCurrent() && !pendingTransport) actionInProgress = false;
    }
  }

  async function handleSaveOnly() {
    if (actionInProgress || !canSave) return;
    const owner = captureToolbarOperation();
    actionInProgress = true;
    actionError = actionFeedback = '';
    try {
      const ok = await timelineEditor.save();
      if (ok && owner.isCurrent()) actionFeedback = $isTimelineEditorDirty
        ? 'Submitted version saved. Newer edits remain in this draft.' : 'Timeline saved. Playback is unchanged.';
    } finally {
      if (owner.isCurrent()) actionInProgress = false;
    }
  }

  onDestroy(() => {
    disposed = true;
    ++actionRequest;
    if (confirmationTimer) clearTimeout(confirmationTimer);
  });

  function handleBpmChange(e: Event) {
    const val = parseInt((e.target as HTMLInputElement).value, 10);
    if (!isNaN(val)) {
      timelineEditor.setBpm(val);
    }
  }

  function handleSetBpmPreset(bpm: number) {
    timelineEditor.setBpm(bpm);
  }

  function handleZoomChange(lvl: TimelineZoomLevel) {
    timelineEditor.setZoom(lvl);
  }
</script>

<div class="timeline-toolbar" role="toolbar" aria-label="Timeline Controls">
  <!-- Left: Status & Primary Transport Actions -->
  <div class="toolbar-section left">
    <div class="status-group">
      {#if !statusFresh}
        <span class="badge badge-stale">{snapshot ? 'STALE STATUS' : 'STATUS UNAVAILABLE'}</span>
      {:else if conflict}
        <span class="badge badge-conflict">CONFLICT</span>
      {:else if isFollower}
        <span class="badge badge-follower" title="Playback is controlled by sync leader">FOLLOWER</span>
      {:else if isRunning && contentStale}
        <span class="badge badge-stale" title="Node is executing older snapshot. Changes saved.">RUNNING PREVIOUS VERSION</span>
      {:else if isRunning}
        <span class="badge badge-running">RUNNING</span>
      {:else}
        <span class="badge badge-stopped">STOPPED</span>
      {/if}

      {#if isDirty}
        <span class="badge badge-edited">EDITED</span>
      {:else if $timelineEditor.loading}
        <span class="badge badge-saved">LOADING DOCUMENT</span>
      {:else if !$timelineEditor.loaded || !persisted}
        <span class="badge badge-stale">DOCUMENT UNAVAILABLE</span>
      {:else}
        <span class="badge badge-saved">SAVED</span>
      {/if}
    </div>

    <!-- Transport Controls -->
    <div class="transport-actions">
      {#if isFollower}
        <button class="btn btn-disabled" disabled title="Playback controlled by sync leader">
          FOLLOWER LOCKED
        </button>
      {:else if isDirty}
        <button
          class="btn btn-primary"
          onclick={handleSaveAndRun}
          disabled={!canSave || !canRunSaved || saving || actionInProgress}
        >
          {saving ? 'SAVING...' : 'SAVE & RUN'}
        </button>
        {#if isRunning}
          <button
            class="btn btn-danger"
            onclick={handleStop}
            disabled={!canControl || actionInProgress}
          >
            STOP
          </button>
        {:else}
          <button
            class="btn btn-secondary"
            onclick={handleRun}
            disabled={!canRunSaved || actionInProgress}
            title="Runs previously saved version from node"
          >
            RUN SAVED
          </button>
        {/if}
      {:else if isRunning && contentStale}
        <button
          class="btn btn-amber"
          onclick={handleRestartWithSaved}
          disabled={!canRunSaved || actionInProgress}
        >
          RESTART WITH SAVED
        </button>
        <button
          class="btn btn-danger"
          onclick={handleStop}
          disabled={!canControl || actionInProgress}
        >
          STOP
        </button>
      {:else if isRunning}
        <button
          class="btn btn-danger"
          onclick={handleStop}
          disabled={!canControl || actionInProgress}
        >
          STOP
        </button>
      {:else}
        <button
          class="btn btn-success"
          onclick={handleRun}
          disabled={!canRunSaved || actionInProgress}
        >
          RUN
        </button>
      {/if}
      {#if isDirty}
        <button
          class="btn btn-outline"
          onclick={handleSaveOnly}
          disabled={!canSave || saving || actionInProgress}
        >
          {saving ? 'SAVING...' : 'SAVE'}
        </button>
      {/if}
    </div>
  </div>

  <!-- Center: BPM Controller -->
  <div class="toolbar-section center">
    <div class="bpm-control">
      <label for="timeline-bpm-input" class="bpm-label">BPM</label>
      <input
        id="timeline-bpm-input"
        type="number"
        min="10"
        max="500"
        value={working?.bpm ?? ''}
        disabled={!working}
        onchange={handleBpmChange}
        class="bpm-input"
      />

      <div class="bpm-presets">
        {#each bpmPresets as bpm}
          <button
            type="button"
            class="bpm-preset-btn"
            class:active={working?.bpm === bpm}
            onclick={() => handleSetBpmPreset(bpm)}
            disabled={!working}
          >
            {bpm}
          </button>
        {/each}
      </div>

      {#if statusFresh && isRunning && runtimeBpm !== undefined && runtimeBpm !== working?.bpm}
        <span class="bpm-mismatch" title="Running at {runtimeBpm} BPM. Restart needed to apply {working?.bpm} BPM.">
          running @ {runtimeBpm}
        </span>
      {/if}
    </div>
  </div>

  <!-- Right: Zoom & Import/Export -->
  <div class="toolbar-section right">
    <div class="zoom-group" role="group" aria-label="Timeline Zoom">
      <span class="zoom-label">ZOOM</span>
      <button
        type="button"
        class="zoom-btn"
        class:active={zoom === 'compact'}
        onclick={() => handleZoomChange('compact')}
      >
        S
      </button>
      <button
        type="button"
        class="zoom-btn"
        class:active={zoom === 'normal'}
        onclick={() => handleZoomChange('normal')}
      >
        M
      </button>
      <button
        type="button"
        class="zoom-btn"
        class:active={zoom === 'wide'}
        onclick={() => handleZoomChange('wide')}
      >
        L
      </button>
    </div>

    <div class="file-actions">
      <button class="btn btn-ghost" onclick={onImport} disabled={!$timelineEditor.loaded || !libraryReady} title="Import JSON timeline document as an unsaved draft">
        Import
      </button>
      <button class="btn btn-ghost" onclick={onExport} disabled={!working} title="Export current timeline draft">
        Export
      </button>
    </div>
  </div>
  <div class="toolbar-feedback">
    {#if $timelineEditor.remoteChanged && !conflict}
      <p role="status">The node's saved timeline changed. Review and reload its version before running saved content.</p>
      <button type="button" class="btn btn-secondary" onclick={() => timelineEditor.reviewConflict()}>Review saved version</button>
    {/if}
    {#if !statusFresh}
      <p role="status">{snapshot ? 'Showing the last received playback status.' : 'Waiting for playback status.'} Live controls require fresh node status.</p>
    {/if}
    {#if working && !canSave && isDirty}
      <p role="status">Draft kept in this browser. Load the saved document and scene library, and resolve missing references before saving.</p>
    {/if}
    {#if actionFeedback}<p role="status">{actionFeedback}</p>{/if}
    {#if actionError}<p role="alert" class="action-error">{actionError}</p>{/if}
  </div>
</div>

<style>
  .timeline-toolbar {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 10px 16px;
    background: var(--dash-surface);
    border: 1px solid var(--dash-border);
    border-radius: 8px;
    margin-bottom: 12px;
    flex-wrap: wrap;
  }

  .toolbar-section {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
    min-width: 0;
  }

  .status-group {
    display: flex;
    align-items: center;
    gap: 6px;
    flex-wrap: wrap;
  }

  .badge {
    font-size: 11px;
    font-weight: 700;
    padding: 3px 8px;
    border-radius: 4px;
    letter-spacing: 0.05em;
    text-transform: uppercase;
  }

  .badge-running {
    background: rgba(16, 185, 129, 0.2);
    color: #10b981;
    border: 1px solid rgba(16, 185, 129, 0.4);
  }

  .badge-stopped {
    background: rgba(107, 114, 128, 0.2);
    color: #9ca3af;
    border: 1px solid rgba(107, 114, 128, 0.3);
  }

  .badge-stale {
    background: rgba(245, 158, 11, 0.2);
    color: #f59e0b;
    border: 1px solid rgba(245, 158, 11, 0.4);
  }

  .badge-follower {
    background: rgba(14, 165, 233, 0.2);
    color: #38bdf8;
    border: 1px solid rgba(14, 165, 233, 0.4);
  }

  .badge-conflict {
    background: rgba(244, 63, 94, 0.2);
    color: #f43f5e;
    border: 1px solid rgba(244, 63, 94, 0.4);
  }

  .badge-edited {
    background: rgba(234, 179, 8, 0.2);
    color: #eab308;
    border: 1px solid rgba(234, 179, 8, 0.4);
  }

  .badge-saved {
    background: rgba(75, 85, 99, 0.2);
    color: #9ca3af;
    border: 1px solid rgba(75, 85, 99, 0.3);
  }

  .transport-actions {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }

  .btn {
    font-size: 12px;
    font-weight: 600;
    padding: 6px 14px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    transition: all 0.15s ease;
    display: inline-flex;
    align-items: center;
    justify-content: center;
  }

  .btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .btn-primary {
    background: var(--dash-accent);
    color: var(--dash-text);
  }
  .btn-primary:hover:not(:disabled) {
    background: var(--dash-accent-hover);
  }

  .btn-success {
    background: #10b981;
    color: white;
  }
  .btn-success:hover:not(:disabled) {
    background: #059669;
  }

  .btn-danger {
    background: #ef4444;
    color: white;
  }
  .btn-danger:hover:not(:disabled) {
    background: #dc2626;
  }

  .btn-amber {
    background: #f59e0b;
    color: black;
    font-weight: 700;
  }
  .btn-amber:hover:not(:disabled) {
    background: #d97706;
  }

  .btn-secondary {
    background: #374151;
    color: #e5e7eb;
  }
  .btn-secondary:hover:not(:disabled) {
    background: #4b5563;
  }

  .btn-outline {
    background: transparent;
    border: 1px solid #4b5563;
    color: #e5e7eb;
  }
  .btn-outline:hover:not(:disabled) {
    background: #1f2937;
  }

  .btn-ghost {
    background: transparent;
    color: #9ca3af;
  }
  .btn-ghost:hover:not(:disabled) {
    color: #f3f4f6;
    background: #1f2937;
  }

  .btn-disabled {
    background: #1f2937;
    color: #6b7280;
    cursor: not-allowed;
  }

  .bpm-control {
    flex-wrap: wrap;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .bpm-label {
    font-size: 11px;
    font-weight: 700;
    color: #9ca3af;
  }

  .bpm-input {
    width: 64px;
    background: #1f2937;
    border: 1px solid #374151;
    border-radius: 4px;
    color: #f9fafb;
    font-size: 13px;
    font-weight: 700;
    padding: 4px 6px;
    text-align: center;
  }

  .bpm-presets {
    display: flex;
    gap: 4px;
  }

  .bpm-preset-btn {
    font-size: 11px;
    background: #1f2937;
    color: #9ca3af;
    border: 1px solid #374151;
    border-radius: 4px;
    padding: 3px 6px;
    cursor: pointer;
  }
  .bpm-preset-btn:hover {
    background: #374151;
    color: #f3f4f6;
  }
  .bpm-preset-btn.active {
    background: var(--dash-accent);
    color: var(--dash-text);
    border-color: var(--dash-accent);
  }

  .bpm-mismatch {
    font-size: 11px;
    color: #f59e0b;
    font-style: italic;
  }

  .zoom-group {
    display: flex;
    align-items: center;
    gap: 4px;
    background: #1f2937;
    padding: 2px 4px;
    border-radius: 6px;
    border: 1px solid #374151;
  }

  .zoom-label {
    font-size: 10px;
    font-weight: 700;
    color: #6b7280;
    margin-right: 2px;
  }

  .zoom-btn {
    font-size: 11px;
    font-weight: 600;
    background: transparent;
    border: none;
    color: #9ca3af;
    padding: 2px 6px;
    border-radius: 4px;
    cursor: pointer;
  }
  .zoom-btn:hover {
    color: white;
  }
  .zoom-btn.active {
    background: #374151;
    color: white;
  }

  .file-actions {
    display: flex;
    gap: 4px;
  }
  .toolbar-feedback { flex-basis: 100%; color: var(--dash-muted); font-size: 12px; overflow-wrap: anywhere; }
  .toolbar-feedback:empty { display: none; }
  .toolbar-feedback p { margin: 4px 0; }
  .toolbar-feedback .action-error { color: var(--dash-danger); }
  button:focus-visible, input:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 2px; }
</style>
