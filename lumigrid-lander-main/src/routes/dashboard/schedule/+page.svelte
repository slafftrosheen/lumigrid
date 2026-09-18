<script lang="ts">
  import { onMount } from 'svelte';
  import { getCalendarSchedule, saveCalendarSchedule, getPresets } from '$lib/api/node';
  import type { CalendarEntry, CalendarSchedule, ScenePreset } from '$lib/api/node';
  import { getSelectedNodeIp } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';

  let nodeIp = $state('');
  let loading = $state(true);
  let saving = $state(false);
  let error = $state('');
  let toast = $state('');

  let schedule = $state<CalendarSchedule | null>(null);
  let presets = $state<ScenePreset[]>([]);
  let selectedIdx = $state(0);

  const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DAY_FULL = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const ENTRY_COUNT = 8;

  function defaultEntry(index: number): CalendarEntry {
    return { index, hour: 8, minute: 0, daysOfWeek: 0x7F, action: 'preset', presetIndex: 0, enabled: false };
  }

  function defaultSchedule(): CalendarSchedule {
    return { entries: Array.from({ length: ENTRY_COUNT }, (_, i) => defaultEntry(i)) };
  }

  let editEntry = $derived(schedule?.entries[selectedIdx] ?? null);

  let loadGen = 0;

  async function load(targetIp = nodeIp, gen = loadGen) {
    if (!targetIp) {
      schedule = null;
      presets = [];
      loading = false;
      return;
    }
    loading = true;
    error = '';
    try {
      const [s, p] = await Promise.all([
        getCalendarSchedule(targetIp).catch(() => null),
        getPresets(targetIp).catch(() => [] as ScenePreset[]),
      ]);
      if (gen !== loadGen) return;
      if (s && s.entries) {
        while (s.entries.length < ENTRY_COUNT) s.entries.push(defaultEntry(s.entries.length));
        schedule = s;
      } else {
        schedule = defaultSchedule();
      }
      presets = p;
    } catch (e) {
      if (gen !== loadGen) return;
      error = String(e);
    } finally {
      if (gen === loadGen) {
        loading = false;
      }
    }
  }

  async function save() {
    if (!schedule || saving) return;
    saving = true;
    error = '';
    try {
      await saveCalendarSchedule(nodeIp, schedule);
      toast = 'Schedule saved';
      setTimeout(() => (toast = ''), 2000);
    } catch (e) {
      error = String(e);
    }
    saving = false;
  }

  function dayBit(day: number): number {
    return 1 << day;
  }

  function isDayActive(entry: CalendarEntry, day: number): boolean {
    return (entry.daysOfWeek & dayBit(day)) !== 0;
  }

  function toggleDay(entry: CalendarEntry, day: number) {
    entry.daysOfWeek ^= dayBit(day);
  }

  function setTime(entry: CalendarEntry, hhmm: string) {
    const [h, m] = hhmm.split(':').map(Number);
    entry.hour = h;
    entry.minute = m;
  }

  function timeStr(entry: CalendarEntry): string {
    return `${String(entry.hour).padStart(2, '0')}:${String(entry.minute).padStart(2, '0')}`;
  }

  function presetName(idx: number): string {
    const p = presets.find(pr => pr.index === idx);
    return p ? p.name : `Scene ${idx + 1}`;
  }

  function daySummary(entry: CalendarEntry): string {
    const active = DAYS.filter((_, i) => isDayActive(entry, i));
    if (active.length === 7) return 'Every day';
    if (active.length === 0) return 'No days';
    if (active.length <= 3) return active.join(', ');
    return `${active.length} days`;
  }

  onMount(() => {
    const unsub = nodeStore.subscribe(state => {
      const targetIp = state.nodeIp || getSelectedNodeIp();
      if (targetIp !== nodeIp) {
        nodeIp = targetIp;
        loadGen++;
        if (nodeIp) {
          schedule = null;
          presets = [];
          loading = true;
          load(nodeIp, loadGen);
        } else {
          schedule = null;
          presets = [];
          loading = false;
          error = '';
        }
      }
    });
    return unsub;
  });
</script>

{#if !nodeIp}
  <p class="loading">No node selected. Add a node IP in the sidebar.</p>
{:else if loading}
  <p class="loading">Loading schedule…</p>
{:else if schedule}
  <div class="sched-root">
    <header class="sched-header">
      <div>
        <h2>Schedule</h2>
        <p class="sched-sub">{ENTRY_COUNT} time-based rules · {schedule.entries.filter(e => e.enabled).length} active</p>
      </div>
      <button class="btn-save" onclick={save} disabled={saving}>
        {saving ? 'Saving…' : 'Save'}
      </button>
    </header>

    <div class="week-grid">
      <div class="week-header-row">
        <span class="week-time-col">Time</span>
        {#each DAYS as day}
          <span class="week-day-col">{day}</span>
        {/each}
        <span class="week-action-col">Action</span>
      </div>
      {#each schedule.entries as entry, i}
        <button
          class="week-row"
          class:disabled={!entry.enabled}
          class:selected={selectedIdx === i}
          onclick={() => selectedIdx = i}
          type="button"
        >
          <span class="week-time">{timeStr(entry)}</span>
          {#each DAYS as _, dayIdx}
            <span class="week-cell">
              {#if isDayActive(entry, dayIdx)}
                <span class="week-dot" class:active={entry.enabled}></span>
              {:else}
                <span class="week-dot"></span>
              {/if}
            </span>
          {/each}
          <span class="week-action-label">
            {#if entry.action === 'sequence'}
              <span class="action-badge seq">SEQ</span>
            {:else}
              <span class="action-badge preset">{presetName(entry.presetIndex)}</span>
            {/if}
          </span>
        </button>
      {/each}
    </div>

    {#if editEntry}
      <section class="entry-editor">
        <div class="editor-top">
          <h3>Rule {selectedIdx + 1}</h3>
          <label class="inline-toggle">
            <input type="checkbox" bind:checked={editEntry.enabled} />
            <span>Enabled</span>
          </label>
        </div>

        <div class="editor-grid">
          <label class="field">
            <span>Time</span>
            <input type="time" value={timeStr(editEntry)}
              onchange={(e) => setTime(editEntry, (e.target as HTMLInputElement).value)} />
          </label>

          <label class="field">
            <span>Action</span>
            <select bind:value={editEntry.action}>
              <option value="preset">Activate Scene</option>
              <option value="sequence">Start Sequence</option>
            </select>
          </label>

          {#if editEntry.action === 'preset'}
            <label class="field">
              <span>Scene</span>
              <select bind:value={editEntry.presetIndex}>
                {#each Array(32) as _, i}
                  <option value={i}>{presetName(i)}</option>
                {/each}
              </select>
            </label>
          {/if}
        </div>

        <div class="days-section">
          <span class="field-label">Active Days</span>
          <div class="day-toggles">
            {#each DAYS as day, idx}
              <button
                class="day-btn"
                class:active={isDayActive(editEntry, idx)}
                onclick={() => toggleDay(editEntry, idx)}
                title={DAY_FULL[idx]}
              >
                {day}
              </button>
            {/each}
          </div>
          <p class="days-summary">{daySummary(editEntry)}</p>
        </div>

        <div class="quick-days">
          <button class="quick-btn" onclick={() => editEntry.daysOfWeek = 0x7F}>Every day</button>
          <button class="quick-btn" onclick={() => editEntry.daysOfWeek = 0x3E}>Weekdays</button>
          <button class="quick-btn" onclick={() => editEntry.daysOfWeek = 0x41}>Weekends</button>
          <button class="quick-btn" onclick={() => editEntry.daysOfWeek = 0}>Clear</button>
        </div>
      </section>
    {/if}

    {#if toast}
      <div class="toast">{toast}</div>
    {/if}
    {#if error}
      <div class="error-bar">{error}</div>
    {/if}
  </div>
{/if}

<style>
  .sched-root {
    width: 100%;
    max-width: 1120px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
    position: relative;
    color: var(--dash-text);
  }
  .loading { color: #a1a1a6; padding: 2rem; }

  .sched-header {
    position: sticky;
    top: 0;
    z-index: 18;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: .85rem 1rem;
    background: rgba(22,22,24,.90);
    backdrop-filter: blur(22px);
    -webkit-backdrop-filter: blur(22px);
    border: 1px solid var(--dash-border);
    border-radius: var(--dash-radius-lg);
    box-shadow: var(--dash-shadow);
  }
  .sched-header h2 { margin: 0; font-size: 1.15rem; font-weight: 700; }
  .sched-sub { margin: .2rem 0 0; font-size: .82rem; color: #a1a1a6; }

  .btn-save {
    background: rgba(255,45,85,.88); color: #fff; border: none;
    border-radius: 10px; padding: .5rem 1.25rem; font-size: .88rem;
    font-weight: 600; cursor: pointer; transition: background .2s, opacity .2s;
  }
  .btn-save:hover:not(:disabled) { background: #ff3b30; }
  .btn-save:disabled { opacity: .5; cursor: default; }

  .week-grid {
    background: linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    box-shadow: var(--dash-shadow);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: var(--dash-radius-lg);
    overflow: hidden;
  }
  .week-header-row {
    display: grid;
    grid-template-columns: 72px repeat(7, 1fr) 72px;
    padding: .5rem .75rem;
    border-bottom: 1px solid rgba(255,255,255,.06);
  }
  .week-header-row span {
    font-size: .68rem;
    font-weight: 600;
    color: #a1a1a6;
    text-align: center;
    text-transform: uppercase;
    letter-spacing: .04em;
  }
  .week-time-col { text-align: left !important; }
  .week-action-col { text-align: right !important; }

  .week-row {
    width: 100%;
    border: none;
    background: transparent;
    text-align: left;
    display: grid;
    grid-template-columns: 72px repeat(7, 1fr) 72px;
    padding: .5rem .75rem;
    align-items: center;
    cursor: pointer;
    border-bottom: 1px solid rgba(255,255,255,.03);
    transition: background .12s;
  }
  .week-row:last-child { border-bottom: none; }
  .week-row:hover { background: rgba(255,255,255,.03); }
  .week-row.selected {
    background: linear-gradient(90deg, rgba(var(--dash-accent-rgb),.14), rgba(var(--dash-accent-rgb),.05));
    box-shadow: inset 3px 0 0 var(--dash-accent);
  }
  .week-row.disabled { opacity: .35; }

  .week-time {
    font-size: .85rem;
    font-weight: 600;
    font-family: monospace;
    color: #f5f5f7;
  }
  .week-cell { text-align: center; }
  .week-dot {
    display: inline-block;
    width: 8px; height: 8px;
    border-radius: 50%;
    background: rgba(255,255,255,.10);
    transition: background .15s;
  }
  .week-dot.active { background: #30d158; }

  .week-action-label { text-align: right; }
  .action-badge {
    font-size: .68rem;
    font-weight: 700;
    padding: .15rem .45rem;
    border-radius: 6px;
    white-space: nowrap;
  }
  .action-badge.preset {
    display: inline-block;
    max-width: 90px;
    overflow: hidden;
    text-overflow: ellipsis;
    background: rgba(var(--dash-accent-rgb),.12);
    color: var(--dash-accent);
    border: 1px solid rgba(var(--dash-accent-rgb),.22);
  }
  .action-badge.seq {
    background: color-mix(in oklab, var(--dash-warm) 13%, transparent);
    color: var(--dash-warm);
    border: 1px solid color-mix(in oklab, var(--dash-warm) 28%, transparent);
  }

  .entry-editor {
    background: linear-gradient(180deg, rgba(29,29,31,.82), rgba(18,18,20,.78));
    box-shadow: var(--dash-shadow);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: var(--dash-radius-lg);
    padding: 1.25rem;
  }
  .editor-top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 1rem;
  }
  .editor-top h3 { margin: 0; font-size: .95rem; font-weight: 600; }

  .inline-toggle {
    display: flex; align-items: center; gap: .35rem;
    font-size: .82rem; color: #a1a1a6; cursor: pointer;
  }
  .inline-toggle input[type="checkbox"] { accent-color: var(--dash-accent); width: 15px; height: 15px; }

  .editor-grid {
    display: flex;
    gap: 1.25rem;
    flex-wrap: wrap;
    margin-bottom: 1rem;
  }
  .field { display: flex; flex-direction: column; gap: .3rem; }
  .field span, .field-label {
    font-size: .72rem; font-weight: 600; color: #a1a1a6;
    text-transform: uppercase; letter-spacing: .04em;
  }
  .field select, .field input[type="time"] {
    background: rgba(255,255,255,.06);
    border: 1px solid rgba(255,255,255,.12);
    border-radius: 8px;
    color: #f5f5f7;
    font-size: .88rem;
    padding: .45rem .6rem;
    min-width: 130px;
    font-family: monospace;
  }
  .field select:focus-visible, .field input:focus-visible { border-color: var(--dash-accent); box-shadow: 0 0 0 3px rgba(var(--dash-accent-rgb),.12); }

  .days-section { margin-bottom: .75rem; }
  .field-label { display: block; margin-bottom: .5rem; }

  .day-toggles {
    display: flex;
    gap: .4rem;
    flex-wrap: wrap;
  }
  .day-btn {
    background: rgba(255,255,255,.05);
    border: 1px solid rgba(255,255,255,.10);
    border-radius: 8px;
    color: #a1a1a6;
    font-size: .82rem;
    font-weight: 500;
    padding: .4rem .75rem;
    cursor: pointer;
    transition: background .15s, color .15s, border-color .15s;
  }
  .day-btn:hover { background: rgba(255,255,255,.08); color: #f5f5f7; }
  .day-btn.active {
    background: rgba(48,209,88,.15);
    border-color: rgba(48,209,88,.4);
    color: #30d158;
    font-weight: 600;
  }

  .days-summary {
    margin: .5rem 0 0;
    font-size: .78rem;
    color: #a1a1a6;
  }

  .quick-days {
    display: flex;
    gap: .4rem;
    flex-wrap: wrap;
  }
  .quick-btn {
    background: rgba(255,255,255,.04);
    border: 1px solid rgba(255,255,255,.06);
    border-radius: 6px;
    color: #a1a1a6;
    font-size: .72rem;
    padding: .25rem .6rem;
    cursor: pointer;
    transition: background .15s, color .15s;
  }
  .quick-btn:hover { background: rgba(255,255,255,.08); color: #f5f5f7; }

  .toast {
    position: fixed; bottom: 1.5rem; right: 1.5rem;
    background: rgba(48,209,88,.92); color: #fff;
    padding: .6rem 1.25rem; border-radius: 10px;
    font-size: .88rem; font-weight: 600;
    box-shadow: 0 4px 16px rgba(0,0,0,.4);
    animation: toast-in .25s ease-out;
  }
  .error-bar {
    position: fixed; bottom: 1.5rem; left: 50%; transform: translateX(-50%);
    background: rgba(255,69,58,.92); color: #fff;
    padding: .6rem 1.25rem; border-radius: 10px;
    font-size: .85rem; max-width: 90vw;
    box-shadow: 0 4px 16px rgba(0,0,0,.4);
  }
  @keyframes toast-in {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }

  @media (max-width: 760px) {
    .sched-header { position: static; align-items: flex-start; gap: .8rem; flex-wrap: wrap; }
    .sched-header .btn-save { width: 100%; }
    .week-grid { overflow-x: auto; }
    .week-header-row, .week-row { min-width: 720px; }
    .editor-grid { flex-direction: column; gap: .85rem; }
    .field select, .field input[type="time"] { width: 100%; min-width: 0; }
  }

</style>
