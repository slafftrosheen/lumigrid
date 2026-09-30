<script lang="ts">
  import {
    timelineEditor,
    isTimelineEditorDirty,
    type TimelineZoomLevel,
  } from '$lib/timeline/editorStore';
  import {
    startTimeline,
    stopTimeline,
    restartTimeline,
  } from '$lib/api/showTransport';
  import type { RuntimeSnapshotV3 } from '$lib/state/runtimeSnapshot';
  import { nodeStore } from '$lib/state/nodeStore';

  interface Props {
    snapshot: RuntimeSnapshotV3 | null;
    isFollower: boolean;
    onExport: () => void;
    onImport: () => void;
  }

  const { snapshot, isFollower, onExport, onImport }: Props = $props();

  const working = $derived($timelineEditor.working);
  const persisted = $derived($timelineEditor.persisted);
  const isDirty = $derived($isTimelineEditorDirty);
  const zoom = $derived($timelineEditor.zoom);
  const saving = $derived($timelineEditor.saving);
  const conflict = $derived($timelineEditor.conflict);

  // Runtime transport truth from canonical snapshot (LG-031 / LG-033)
  const isRunning = $derived(snapshot?.transport?.mode === 'timeline' || Boolean(snapshot?.transport?.timeline?.running));
  const runtimeBpm = $derived(snapshot?.transport?.timeline?.bpm ?? 120);
  const runtimeGeneration = $derived(snapshot?.transport?.timeline?.generation ?? 0);
  const contentStale = $derived(Boolean(snapshot?.transport?.timeline?.contentStale));

  const bpmPresets = [60, 90, 120, 128, 140];

  let actionInProgress = $state(false);

  async function handleRun() {
    const ip = $nodeStore.nodeIp;
    if (!ip || isFollower) return;

    actionInProgress = true;
    try {
      const bpm = working?.bpm ?? 120;
      await startTimeline(ip, bpm);
    } catch (err: any) {
      console.error('Failed to start timeline:', err);
    } finally {
      actionInProgress = false;
    }
  }

  async function handleStop() {
    const ip = $nodeStore.nodeIp;
    if (!ip || isFollower) return;

    actionInProgress = true;
    try {
      await stopTimeline(ip);
    } catch (err: any) {
      console.error('Failed to stop timeline:', err);
    } finally {
      actionInProgress = false;
    }
  }

  async function handleRestartWithSaved() {
    const ip = $nodeStore.nodeIp;
    if (!ip || isFollower) return;

    actionInProgress = true;
    try {
      const bpm = persisted?.bpm ?? working?.bpm ?? 120;
      await restartTimeline(ip, bpm);
    } catch (err: any) {
      console.error('Failed to restart timeline:', err);
    } finally {
      actionInProgress = false;
    }
  }

  async function handleSaveAndRun() {
    const ip = $nodeStore.nodeIp;
    if (!ip || isFollower) return;

    actionInProgress = true;
    try {
      const ok = await timelineEditor.save();
      if (ok) {
        const bpm = $timelineEditor.working?.bpm ?? 120;
        await startTimeline(ip, bpm);
      }
    } catch (err: any) {
      console.error('Save & Run failed:', err);
    } finally {
      actionInProgress = false;
    }
  }

  async function handleSaveOnly() {
    actionInProgress = true;
    try {
      await timelineEditor.save();
    } finally {
      actionInProgress = false;
    }
  }

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
      {#if conflict}
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
          disabled={saving || actionInProgress}
        >
          {saving ? 'SAVING...' : 'SAVE & RUN'}
        </button>
        {#if isRunning}
          <button
            class="btn btn-danger"
            onclick={handleStop}
            disabled={actionInProgress}
          >
            STOP
          </button>
        {:else}
          <button
            class="btn btn-secondary"
            onclick={handleRun}
            disabled={actionInProgress}
            title="Runs previously saved version from node"
          >
            RUN SAVED
          </button>
        {/if}
        <button
          class="btn btn-outline"
          onclick={handleSaveOnly}
          disabled={saving || actionInProgress}
        >
          {saving ? 'SAVING...' : 'SAVE'}
        </button>
      {:else if isRunning && contentStale}
        <button
          class="btn btn-amber"
          onclick={handleRestartWithSaved}
          disabled={actionInProgress}
        >
          RESTART WITH SAVED
        </button>
        <button
          class="btn btn-danger"
          onclick={handleStop}
          disabled={actionInProgress}
        >
          STOP
        </button>
      {:else if isRunning}
        <button
          class="btn btn-danger"
          onclick={handleStop}
          disabled={actionInProgress}
        >
          STOP
        </button>
      {:else}
        <button
          class="btn btn-success"
          onclick={handleRun}
          disabled={actionInProgress}
        >
          RUN
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
        value={working?.bpm ?? 120}
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
          >
            {bpm}
          </button>
        {/each}
      </div>

      {#if isRunning && runtimeBpm !== working?.bpm}
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
      <button class="btn btn-ghost" onclick={onImport} title="Import JSON timeline document">
        Import
      </button>
      <button class="btn btn-ghost" onclick={onExport} title="Export JSON timeline document">
        Export
      </button>
    </div>
  </div>
</div>

<style>
  .timeline-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 10px 16px;
    background: var(--dash-card-bg, #111827);
    border: 1px solid var(--dash-border, #1f2937);
    border-radius: 8px;
    margin-bottom: 12px;
    flex-wrap: wrap;
  }

  .toolbar-section {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .status-group {
    display: flex;
    align-items: center;
    gap: 6px;
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
    background: #3b82f6;
    color: white;
  }
  .btn-primary:hover:not(:disabled) {
    background: #2563eb;
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
    background: #3b82f6;
    color: white;
    border-color: #3b82f6;
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
</style>
