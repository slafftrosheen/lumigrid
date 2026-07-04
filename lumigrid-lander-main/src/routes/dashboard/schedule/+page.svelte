<script lang="ts">
  import { onMount } from 'svelte';
  import { getSchedule, saveSchedule } from '$lib/api/wled';
  import type { AnimationBlock } from '$lib/api/wled';

  // ── Feature 1: Visual Schedule Builder ───────────────────────────────────────

  let nodeIp  = $state('');
  let blocks  = $state<AnimationBlock[]>([]);
  let loading = $state(true);
  let saving  = $state(false);
  let saved   = $state(false);
  let error   = $state('');

  // Edit modal state
  let editing     = $state<AnimationBlock | null>(null);
  let editIndex   = $state(-1);
  let showModal   = $state(false);

  // Form fields
  let fId       = $state('');
  let fFile     = $state('');
  let fStart    = $state('08:00');
  let fDuration = $state(120);
  let fPreset   = $state(1);

  onMount(async () => {
    nodeIp = localStorage.getItem('lg_node_ip') ?? '';
    if (nodeIp) {
      try {
        const s = await getSchedule(nodeIp);
        blocks = s ?? [];
      } catch (e) {
        error = String(e);
      }
    }
    loading = false;
  });

  function openNew() {
    editIndex = -1;
    fId       = `block_${Date.now()}`;
    fFile     = '';
    fStart    = '08:00';
    fDuration = 120;
    fPreset   = 1;
    showModal = true;
  }

  function openEdit(idx: number) {
    const b = blocks[idx];
    editIndex = idx;
    fId       = b.id;
    fFile     = b.file;
    fStart    = b.start_time;
    fDuration = b.duration_minutes;
    fPreset   = b.fallback_preset;
    showModal = true;
  }

  function saveBlock() {
    const block: AnimationBlock = {
      id:               fId.trim() || `block_${Date.now()}`,
      file:             fFile.trim(),
      start_time:       fStart,
      duration_minutes: Math.max(1, Math.min(1440, fDuration)),
      fallback_preset:  Math.max(1, Math.min(250, fPreset))
    };
    if (editIndex >= 0) {
      blocks = blocks.map((b, i) => i === editIndex ? block : b);
    } else {
      blocks = [...blocks, block];
    }
    blocks = [...blocks].sort((a, b) => a.start_time.localeCompare(b.start_time));
    showModal = false;
  }

  function deleteBlock(idx: number) {
    blocks = blocks.filter((_, i) => i !== idx);
  }

  async function persist() {
    saving = true; error = '';
    try {
      await saveSchedule(nodeIp, blocks);
      saved = true;
      setTimeout(() => { saved = false; }, 3000);
    } catch (e) {
      error = String(e);
    }
    saving = false;
  }

  // Timeline helpers
  function timeToMin(t: string): number {
    const [h, m] = t.split(':').map(Number);
    return h * 60 + m;
  }
  function minToTime(m: number): string {
    return `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;
  }
  function pct(t: string): number { return (timeToMin(t) / 1440) * 100; }
  function barWidth(b: AnimationBlock): number { return Math.min(b.duration_minutes / 1440 * 100, 100 - pct(b.start_time)); }

  const HOURS = Array.from({length: 25}, (_, i) => i);
  const PALETTE = ['#ff2d55','#ff9500','#00d4ff','#34c759','#5856d6','#ff6b30','#ff3b30','#30d158'];
</script>

<svelte:head>
  <title>Schedule Builder — LumiGrid</title>
</svelte:head>

<div class="page">
  <header class="page-header">
    <div>
      <h1>Schedule Builder</h1>
      <p class="sub">{nodeIp || 'No node selected'} · {blocks.length} block{blocks.length !== 1 ? 's' : ''}</p>
    </div>
    <div class="head-btns">
      <button class="ghost-btn" onclick={openNew}>+ Add Block</button>
      <button class="save-btn" onclick={persist} disabled={saving || !nodeIp}>
        {saving ? 'Saving…' : saved ? '✓ Saved' : 'Save to Node'}
      </button>
    </div>
  </header>

  {#if error}<div class="err-box">{error}</div>{/if}

  {#if loading}
    <div class="loading"><div class="spinner"></div>Loading schedule…</div>
  {:else if !nodeIp}
    <div class="empty">Select a node in the sidebar to load and edit its schedule.</div>
  {:else}
    <!-- 24-hour timeline -->
    <div class="timeline-card">
      <h2>24-Hour Timeline</h2>
      <div class="timeline">
        <!-- Hour markers -->
        <div class="hour-track">
          {#each HOURS as h}
            <div class="hour-mark" style:left={`${(h/24)*100}%`}>
              <div class="hour-line"></div>
              {#if h % 3 === 0}<span class="hour-label">{h}:00</span>{/if}
            </div>
          {/each}
        </div>
        <!-- Blocks -->
        <div class="block-lane">
          {#each blocks as b, i}
            <button
              class="timeline-block"
              style:left={`${pct(b.start_time)}%`}
              style:width={`${barWidth(b)}%`}
              style:background={PALETTE[i % PALETTE.length]}
              onclick={() => openEdit(i)}
              title={`${b.start_time} — ${minToTime(timeToMin(b.start_time) + b.duration_minutes)}\n${b.file}`}
            >
              <span class="tb-label">{b.file || b.id}</span>
            </button>
          {/each}
          {#if !blocks.length}
            <div class="timeline-empty">No blocks — click "+ Add Block" to create one</div>
          {/if}
        </div>
      </div>
    </div>

    <!-- Block list -->
    <div class="block-list">
      {#each blocks as b, i}
        <div class="block-row">
          <div class="block-color" style:background={PALETTE[i % PALETTE.length]}></div>
          <div class="block-info">
            <span class="block-id">{b.id}</span>
            <span class="block-file">{b.file || '—'}</span>
          </div>
          <div class="block-time">
            <span>{b.start_time}</span>
            <span class="arrow">→</span>
            <span>{minToTime(timeToMin(b.start_time) + b.duration_minutes)}</span>
            <span class="dur">({b.duration_minutes} min)</span>
          </div>
          <div class="block-preset">Preset #{b.fallback_preset}</div>
          <div class="block-actions">
            <button class="edit-btn" onclick={() => openEdit(i)}>Edit</button>
            <button class="del-btn" onclick={() => deleteBlock(i)}>×</button>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>

<!-- Edit modal -->
{#if showModal}
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <div
    class="modal-overlay"
    onclick={(e) => e.target === e.currentTarget && (showModal = false)}
    onkeydown={(e) => e.key === 'Escape' && (showModal = false)}
    role="dialog"
    aria-modal="true"
    aria-label="Edit schedule block"
    tabindex="-1"
  >
    <div class="modal">
      <h2>{editIndex >= 0 ? 'Edit Block' : 'New Block'}</h2>

      <label class="field">
        <span>ID</span>
        <input type="text" bind:value={fId} placeholder="morning_greeting" />
      </label>
      <label class="field">
        <span>File on WLED FS</span>
        <input type="text" bind:value={fFile} placeholder="greeting.gif" />
      </label>
      <label class="field">
        <span>Start time</span>
        <input type="time" bind:value={fStart} />
      </label>
      <label class="field">
        <span>Duration (minutes)</span>
        <input type="number" min="1" max="1440" bind:value={fDuration} />
      </label>
      <label class="field">
        <span>Fallback preset #</span>
        <input type="number" min="1" max="250" bind:value={fPreset} />
      </label>
      <p class="field-hint">
        Ends at: <strong>{minToTime(timeToMin(fStart) + fDuration)}</strong>
      </p>

      <div class="modal-btns">
        <button class="ghost-btn" onclick={() => showModal = false}>Cancel</button>
        <button class="save-btn" onclick={saveBlock}>
          {editIndex >= 0 ? 'Update' : 'Add Block'}
        </button>
      </div>
    </div>
  </div>
{/if}

<style>
  .page { max-width: 960px; }
  .page-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; gap: 1rem; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: #f5f5f7; }
  .sub { margin: .25rem 0 0; color: #a1a1a6; font-size: .85rem; }
  .head-btns { display: flex; gap: .6rem; }
  .ghost-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.14); border-radius: 999px; color: #f5f5f7; padding: .55rem 1.1rem; cursor: pointer; font-size: .88rem; transition: background .2s; }
  .ghost-btn:hover { background: rgba(255,255,255,.14); }
  .save-btn { background: rgba(255,45,85,.88); border: none; border-radius: 999px; color: #fff; font-weight: 600; padding: .55rem 1.25rem; cursor: pointer; font-size: .88rem; transition: background .2s, box-shadow .2s; box-shadow: 0 4px 14px rgba(255,45,85,.4); }
  .save-btn:hover:not(:disabled) { background: #ff3b30; box-shadow: 0 6px 20px rgba(255,45,85,.55); }
  .save-btn:disabled { opacity: .5; cursor: default; }
  .err-box { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.3); border-radius: 12px; color: #ff453a; padding: .9rem 1rem; font-size: .85rem; margin-bottom: 1rem; }
  .loading { display: flex; align-items: center; gap: .75rem; color: #a1a1a6; padding: 3rem; justify-content: center; }
  .spinner { width: 18px; height: 18px; border: 2px solid rgba(255,255,255,.1); border-top-color: #ff2d55; border-radius: 50%; animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .empty { color: #a1a1a6; padding: 3rem; text-align: center; }

  /* Timeline */
  .timeline-card { background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 18px; padding: 1.25rem; margin-bottom: 1rem; overflow: hidden; }
  h2 { margin: 0 0 1rem; font-size: .95rem; font-weight: 600; color: #f5f5f7; }
  .timeline { position: relative; }
  .hour-track { position: relative; height: 28px; margin-bottom: .25rem; }
  .hour-mark { position: absolute; top: 0; }
  .hour-line { width: 1px; height: 8px; background: rgba(255,255,255,.12); }
  .hour-label { font-size: .62rem; color: #a1a1a6; white-space: nowrap; transform: translateX(-50%); display: block; margin-top: 2px; }
  .block-lane { position: relative; height: 40px; background: rgba(255,255,255,.03); border-radius: 8px; overflow: hidden; }
  .timeline-block { position: absolute; top: 4px; height: 32px; border: none; border-radius: 6px; cursor: pointer; overflow: hidden; min-width: 4px; display: flex; align-items: center; padding: 0 .35rem; transition: filter .15s, transform .15s; }
  .timeline-block:hover { filter: brightness(1.2); transform: scaleY(1.05); }
  .tb-label { font-size: .68rem; font-weight: 600; color: #fff; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; pointer-events: none; }
  .timeline-empty { position: absolute; inset: 0; display: flex; align-items: center; justify-content: center; color: #a1a1a6; font-size: .82rem; }

  /* Block list */
  .block-list { display: flex; flex-direction: column; gap: .5rem; }
  .block-row { display: flex; align-items: center; gap: .75rem; background: rgba(29,29,31,.72); border: 1px solid rgba(255,255,255,.08); border-radius: 14px; padding: .75rem 1rem; flex-wrap: wrap; }
  .block-color { width: 10px; height: 36px; border-radius: 4px; flex-shrink: 0; }
  .block-info { display: flex; flex-direction: column; flex: 1; min-width: 100px; }
  .block-id { font-size: .78rem; font-weight: 600; color: #f5f5f7; }
  .block-file { font-size: .72rem; color: #a1a1a6; font-family: monospace; }
  .block-time { display: flex; align-items: center; gap: .35rem; font-size: .8rem; color: #f5f5f7; font-family: monospace; }
  .arrow { color: #a1a1a6; }
  .dur { color: #a1a1a6; font-size: .72rem; }
  .block-preset { font-size: .78rem; color: #ff9500; background: rgba(255,149,0,.10); padding: .2rem .5rem; border-radius: 999px; white-space: nowrap; }
  .block-actions { display: flex; gap: .35rem; margin-left: auto; }
  .edit-btn { background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.10); border-radius: 7px; color: #f5f5f7; font-size: .75rem; padding: .25rem .6rem; cursor: pointer; }
  .edit-btn:hover { background: rgba(255,255,255,.14); }
  .del-btn { background: rgba(255,69,58,.12); border: 1px solid rgba(255,69,58,.25); border-radius: 7px; color: #ff453a; font-size: .9rem; padding: .15rem .5rem; cursor: pointer; }
  .del-btn:hover { background: rgba(255,69,58,.25); }

  /* Modal */
  .modal-overlay { position: fixed; inset: 0; background: rgba(0,0,0,.65); backdrop-filter: blur(8px); display: flex; align-items: center; justify-content: center; z-index: 100; }
  .modal { background: rgba(25,25,28,.97); border: 1px solid rgba(255,255,255,.12); border-radius: 22px; padding: 2rem; width: min(420px, 92vw); display: flex; flex-direction: column; gap: .85rem; box-shadow: 0 24px 64px rgba(0,0,0,.7); }
  .modal h2 { margin: 0; font-size: 1.15rem; font-weight: 700; color: #f5f5f7; }
  .field { display: flex; flex-direction: column; gap: .3rem; }
  .field span { font-size: .78rem; font-weight: 600; color: #a1a1a6; text-transform: uppercase; letter-spacing: .05em; }
  .field input { background: rgba(255,255,255,.06); border: 1px solid rgba(255,255,255,.12); border-radius: 10px; color: #f5f5f7; font-size: .9rem; padding: .55rem .75rem; }
  .field input:focus { outline: none; border-color: #ff2d55; box-shadow: 0 0 0 2px rgba(255,45,85,.2); }
  .field-hint { margin: -.3rem 0 0; font-size: .8rem; color: #a1a1a6; }
  .field-hint strong { color: #f5f5f7; }
  .modal-btns { display: flex; gap: .6rem; justify-content: flex-end; margin-top: .5rem; }
</style>
