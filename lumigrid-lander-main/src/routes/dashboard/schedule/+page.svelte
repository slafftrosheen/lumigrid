<script lang="ts">
  import { onMount } from 'svelte';
  import { getCalendarSchedule, saveCalendarSchedule, getPresets } from '$lib/api/node';
  import type { CalendarEntry, CalendarSchedule, ScenePreset } from '$lib/api/node';
  import { readString, NODE_IP_KEY } from '$lib/auth/storage';

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

  async function load() {
    loading = true;
    error = '';
    try {
      const [s, p] = await Promise.all([
        getCalendarSchedule(nodeIp).catch(() => null),
        getPresets(nodeIp).catch(() => [] as ScenePreset[]),
      ]);
      if (s && s.entries) {
        while (s.entries.length < ENTRY_COUNT) s.entries.push(defaultEntry(s.entries.length));
        schedule = s;
      } else {
        schedule = defaultSchedule();
      }
      presets = p;
    } catch (e) {
      error = String(e);
    }
    loading = false;
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

  function dayBit(day: number): number { return 1 << day; }
  function isDayActive(entry: CalendarEntry, day: number): boolean { return (entry.daysOfWeek & dayBit(day)) !== 0; }
  function toggleDay(entry: CalendarEntry, day: number) { entry.daysOfWeek ^= dayBit(day); }
  function setTime(entry: CalendarEntry, hhmm: string) { const [h, m] = hhmm.split(':').map(Number); entry.hour = h; entry.minute = m; }
  function timeStr(entry: CalendarEntry): string { return `${String(entry.hour).padStart(2, '0')}:${String(entry.minute).padStart(2, '0')}`; }
  function presetName(idx: number): string { const p = presets.find(pr => pr.index === idx); return p ? p.name : `Preset ${idx}`; }
  function daySummary(entry: CalendarEntry): string {
    const active = DAYS.filter((_, i) => isDayActive(entry, i));
    if (active.length === 7) return 'Every day';
    if (active.length === 0) return 'No days';
    if (active.length <= 3) return active.join(', ');
    return `${active.length} days`;
  }

  onMount(() => {
    nodeIp = readString(NODE_IP_KEY) ?? '';
    if (nodeIp) load();
    else loading = false;
  });
</script>

{#if !nodeIp}
  <p class="loading">No node selected. Add a node IP in the sidebar.</p>
{:else if loading}
  <p class="loading">Loading schedule…</p>
{:else if schedule}
  <div class="sched-root">
    <header class="sched-header">
      <div><h2>Schedule</h2><p class="sched-sub">{ENTRY_COUNT} time-based rules · {schedule.entries.filter(e => e.enabled).length} active</p></div>
      <button class="btn-save" onclick={save} disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
    </header>

    <div class="week-grid">
      <div class="week-header-row">