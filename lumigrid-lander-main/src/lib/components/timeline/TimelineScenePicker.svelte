<script lang="ts">
  import type { ScenePreset } from '$lib/api/scenes';
  import OperatorDialog from '$lib/components/dashboard/OperatorDialog.svelte';
  import { getChannelPort } from '$lib/api/timeline';
  import SceneThumbnail from '$lib/components/dashboard/SceneThumbnail.svelte';

  interface Props {
    channelId: number;
    scenes: Map<number, ScenePreset>;
    currentPresetIndex?: number;
    onSelect: (presetIndex: number) => void;
    onClose: () => void;
  }

  const {
    channelId,
    scenes,
    currentPresetIndex,
    onSelect,
    onClose,
  }: Props = $props();

  let searchQuery = $state('');

  const availableSlots = $derived(
    Array.from({ length: 32 }, (_, idx) => {
      const scene = scenes.get(idx);
      const exists = scene !== undefined && Boolean(scene.name);
      return {
        slot: idx,
        scene,
        exists,
      };
    }).filter((item) => {
      if (!searchQuery.trim()) return true;
      if (!item.exists) return false;
      const q = searchQuery.toLowerCase();
      return (
        item.scene?.name?.toLowerCase().includes(q) ||
        String(item.slot + 1).includes(q)
      );
    })
  );
</script>

<OperatorDialog open title={`Select scene for ${getChannelPort(channelId)}`} description="Choose a saved scene for this timeline block." oncancel={onClose}>
    <!-- Search Input -->
    <div class="search-bar">
      <input
        type="text"
        aria-label="Filter scenes by name or slot"
        placeholder="Filter scenes by name or slot..."
        bind:value={searchQuery}
        class="search-input"
      />
    </div>

    <!-- Scene Grid -->
    <div class="scenes-grid">
      {#each availableSlots as item}
        {@const isCurrent = currentPresetIndex === item.slot}
        <button
          type="button"
          class="scene-card"
          class:current={isCurrent}
          class:empty={!item.exists}
          disabled={!item.exists}
          onclick={() => item.exists && onSelect(item.slot)}
        >
          <div class="scene-card-header">
            <span class="slot-badge">S{String(item.slot + 1).padStart(2, '0')}</span>
            <span class="scene-name" title={item.scene?.name || 'Empty Slot'}>
              {item.scene?.name || 'Empty'}
            </span>
          </div>

          {#if item.exists && item.scene}
            <div class="scene-thumb-wrapper">
              <SceneThumbnail preset={item.scene} active={isCurrent} />
            </div>
          {:else}
            <div class="scene-empty-placeholder">
              <span>Unconfigured</span>
            </div>
          {/if}
        </button>
      {/each}
    </div>

    <div class="modal-footer">
      <button type="button" data-dialog-cancel class="btn btn-secondary" onclick={onClose}>
        Cancel
      </button>
    </div>
</OperatorDialog>

<style>
  .search-bar {
    padding: 12px 20px;
    border-bottom: 1px solid #1e293b;
  }

  .search-input {
    width: 100%;
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 8px 12px;
    color: #f1f5f9;
    font-size: 13px;
    box-sizing: border-box;
  }
  .search-input:focus {
    outline: none;
    border-color: #38bdf8;
  }

  .scenes-grid {
    flex: 1;
    max-height: 50vh;
    overflow-y: auto;
    padding: 16px 20px;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(135px, 1fr));
    gap: 10px;
  }

  .scene-card {
    background: #111827;
    border: 1px solid #1f2937;
    border-radius: 8px;
    padding: 8px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    cursor: pointer;
    text-align: left;
    transition: all 0.15s ease;
  }

  .scene-card:hover:not(:disabled) {
    border-color: #38bdf8;
    background: #1e293b;
    transform: translateY(-1px);
  }

  .scene-card.current {
    border-color: #10b981;
    box-shadow: 0 0 0 1px #10b981;
  }

  .scene-card:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }

  .scene-card-header {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .slot-badge {
    font-size: 10px;
    font-weight: 700;
    color: #38bdf8;
    background: rgba(56, 189, 248, 0.15);
    padding: 1px 4px;
    border-radius: 3px;
  }

  .scene-name {
    font-size: 11px;
    font-weight: 600;
    color: #f1f5f9;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .scene-thumb-wrapper {
    height: 38px;
    border-radius: 4px;
    overflow: hidden;
  }

  .scene-empty-placeholder {
    height: 38px;
    background: #1f2937;
    border-radius: 4px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 10px;
    color: #475569;
  }

  .modal-footer {
    display: flex;
    align-items: center;
    justify-content: flex-end;
    padding: 12px 20px;
    border-top: 1px solid #1e293b;
    background: #0f172a;
  }

  .btn {
    font-size: 12px;
    font-weight: 600;
    padding: 6px 14px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
  }

  .btn-secondary {
    background: #334155;
    color: #e2e8f0;
  }
  .btn-secondary:hover {
    background: #475569;
  }
  button:focus-visible, input:focus-visible { outline: 2px solid var(--dash-accent); outline-offset: 2px; }
</style>
