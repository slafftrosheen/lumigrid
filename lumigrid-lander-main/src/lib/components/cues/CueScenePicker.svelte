<script lang="ts">
  /**
   * CueScenePicker — visual Scene selection (LG-034 §50).
   * Thumbnails + names + slots, reused from Scene Studio data.
   */
  interface SceneOption {
    index: number;
    name: string;
    thumbnail?: string | null;
  }

  interface Props {
    scenes: SceneOption[];
    value: number;
    disabled?: boolean;
    onchange: (sceneIndex: number) => void;
  }

  let { scenes, value, disabled = false, onchange }: Props = $props();

  let query = $state('');
  let open = $state(false);

  let filtered = $derived(
    query.trim() === ''
      ? scenes
      : scenes.filter((s) => s.name.toLowerCase().includes(query.trim().toLowerCase()))
  );

  let selected = $derived(scenes.find((s) => s.index === value) ?? null);

  function choose(index: number) {
    onchange(index);
    open = false;
  }

  function slotLabel(index: number): string {
    return String(index + 1).padStart(2, '0');
  }
</script>

<div class="picker" class:picker-open={open}>
  <button
    class="picker-trigger"
    type="button"
    {disabled}
    aria-haspopup="listbox"
    aria-expanded={open}
    onclick={() => (open = !open)}
  >
    {#if selected?.thumbnail}
      <img class="picker-thumb" src={selected.thumbnail} alt="" />
    {:else}
      <span class="picker-thumb picker-thumb-fallback" aria-hidden="true"></span>
    {/if}
    <span class="picker-name">{selected ? selected.name : `Scene #${value + 1}`}</span>
    <span class="picker-slot">{slotLabel(value)}</span>
  </button>

  {#if open}
    <div class="picker-pop" role="listbox" aria-label="Choose scene">
      <input
        class="picker-search"
        type="search"
        placeholder="Search scenes…"
        bind:value={query}
        aria-label="Search scenes"
      />
      <div class="picker-grid">
        {#each filtered as scene (scene.index)}
          <button
            class="picker-item"
            class:picker-item-active={scene.index === value}
            type="button"
            role="option"
            aria-selected={scene.index === value}
            onclick={() => choose(scene.index)}
          >
            {#if scene.thumbnail}
              <img class="picker-thumb" src={scene.thumbnail} alt="" />
            {:else}
              <span class="picker-thumb picker-thumb-fallback" aria-hidden="true"></span>
            {/if}
            <span class="picker-item-name">{scene.name}</span>
            <span class="picker-slot">{slotLabel(scene.index)}</span>
          </button>
        {/each}
        {#if filtered.length === 0}
          <div class="picker-none">No matching scenes</div>
        {/if}
      </div>
    </div>
  {/if}
</div>

<style>
  .picker {
    position: relative;
    min-width: 200px;
  }
  .picker-trigger {
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: 8px;
    color: #fff;
    padding: 6px 10px;
    cursor: pointer;
  }
  .picker-trigger:hover:not(:disabled) {
    border-color: var(--dash-accent);
  }
  .picker-trigger:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .picker-thumb {
    width: 42px;
    height: 26px;
    border-radius: 4px;
    object-fit: cover;
    border: 1px solid rgba(255, 255, 255, 0.12);
    flex: 0 0 auto;
  }
  .picker-thumb-fallback {
    display: inline-block;
    background: linear-gradient(135deg, rgba(var(--dash-accent-rgb), 0.25), rgba(255, 255, 255, 0.06));
  }
  .picker-name {
    flex: 1;
    text-align: left;
    font-size: 0.86rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .picker-slot {
    font-family: monospace;
    font-size: 0.72rem;
    color: rgba(255, 255, 255, 0.5);
  }
  .picker-pop {
    position: absolute;
    z-index: 40;
    top: calc(100% + 6px);
    left: 0;
    width: min(360px, 80vw);
    background: rgba(22, 22, 24, 0.98);
    border: 1px solid rgba(255, 255, 255, 0.14);
    border-radius: 10px;
    padding: 10px;
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.6);
  }
  .picker-search {
    width: 100%;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.12);
    border-radius: 6px;
    color: #fff;
    padding: 6px 10px;
    margin-bottom: 8px;
  }
  .picker-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px;
    max-height: 260px;
    overflow-y: auto;
  }
  .picker-item {
    display: flex;
    align-items: center;
    gap: 8px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: #fff;
    padding: 6px;
    cursor: pointer;
    text-align: left;
  }
  .picker-item:hover {
    border-color: rgba(var(--dash-accent-rgb), 0.5);
  }
  .picker-item-active {
    border-color: var(--dash-accent);
    background: rgba(var(--dash-accent-rgb), 0.1);
  }
  .picker-item-name {
    flex: 1;
    font-size: 0.8rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .picker-none {
    grid-column: 1 / -1;
    text-align: center;
    font-size: 0.8rem;
    color: rgba(255, 255, 255, 0.4);
    padding: 12px 0;
  }
</style>
