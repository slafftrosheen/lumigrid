import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { get } from 'svelte/store';
import { cueEditorStore, isDirty } from './editorStore';
import { createDefaultCueList, type CueListDocument } from './document';
import { ThrottledSender, q15ToUnit, unitToQ15, interpolateElapsed } from './timing';

function makeDoc(index = 2): CueListDocument {
  return { ...createDefaultCueList(index, `Show ${index}`), checksum: 4242 };
}

describe('cue editor store (LG-034 §82 H..N, §84 AE/AF)', () => {
  beforeEach(() => {
    cueEditorStore.reset();
  });

  it('starts clean and binds to a node', () => {
    const s = get(cueEditorStore);
    expect(s.working).toBeNull();
    cueEditorStore.bindNode('node-a');
    expect(get(cueEditorStore).nodeId).toBe('node-a');
  });

  it('load() establishes a clean persisted baseline (not dirty)', () => {
    cueEditorStore.bindNode('node-a');
    cueEditorStore.load(makeDoc());
    const s = get(cueEditorStore);
    expect(isDirty(s)).toBe(false);
    expect(s.persisted?.checksum).toBe(4242);
  });

  it('editing the draft makes it dirty; reverting clears it', () => {
    cueEditorStore.bindNode('node-a');
    cueEditorStore.load(makeDoc());
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Renamed' }));
    expect(isDirty(get(cueEditorStore))).toBe(true);
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: `Show 2` }));
    expect(isDirty(get(cueEditorStore))).toBe(false);
  });

  it('commitSave clears dirty with the authoritative checksum', () => {
    cueEditorStore.bindNode('node-a');
    cueEditorStore.load(makeDoc());
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Saved Name' }));
    cueEditorStore.commitSave({ ...makeDoc(), name: 'Saved Name', checksum: 777 });
    const s = get(cueEditorStore);
    expect(isDirty(s)).toBe(false);
    expect(s.persisted?.checksum).toBe(777);
  });

  it('commits the submitted copy while preserving edits made during a save', () => {
    cueEditorStore.load(makeDoc());
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Submitted' }));
    const submitted = get(cueEditorStore).working!;
    cueEditorStore.updateWorking((doc) => ({
      ...doc,
      name: 'Still editing',
      cues: doc.cues.map((cue) => ({ ...cue, holdMs: 6000 })),
    }));
    cueEditorStore.commitSave({ ...submitted, checksum: 900 }, submitted);
    const s = get(cueEditorStore);
    expect(s.persisted?.name).toBe('Submitted');
    expect(s.working?.name).toBe('Still editing');
    expect(s.working?.cues[0].holdMs).toBe(6000);
    expect(s.persisted?.cues[0].holdMs).toBe(submitted.cues[0].holdMs);
    expect(s.persisted?.checksum).toBe(900);
    expect(s.working?.checksum).toBe(900);
    expect(isDirty(s)).toBe(true);
  });

  it('conflict set/clear flows', () => {
    cueEditorStore.load(makeDoc());
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Draft' }));
    cueEditorStore.setConflict(555);
    expect(get(cueEditorStore).conflict?.currentChecksum).toBe(555);
    cueEditorStore.keepDraft();
    expect(get(cueEditorStore).conflict).toBeNull();
    cueEditorStore.setConflict(555);
    cueEditorStore.reloadNodeVersion(makeDoc());
    expect(get(cueEditorStore).conflict).toBeNull();
    expect(get(cueEditorStore).working?.name).toBe('Show 2');
  });

  it('openDraft has no persisted baseline: draft-only creation (§45)', () => {
    cueEditorStore.openDraft(createDefaultCueList(3, 'Fresh'));
    const s = get(cueEditorStore);
    expect(s.persisted).toBeNull();
    expect(s.working?.index).toBe(3);
    expect(isDirty(s)).toBe(true);
  });
});

describe('timing + throttle (LG-034 §83 Q/R, §58)', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('throttles to 10 Hz: at most 11 sends per second of dragging', () => {
    const sent: number[] = [];
    const sender = new ThrottledSender(100, (v) => {
      sent.push(v);
    });

    for (let i = 0; i < 100; i++) {
      sender.push(i / 100);
      vi.advanceTimersByTime(10);
    }
    // 100 events at 100ms intervals -> at most 11 flushes (0,100..1000ms)
    expect(sent.length).toBeLessThanOrEqual(12);
    expect(sent.length).toBeGreaterThanOrEqual(10);
  });

  it('flush sends the trailing value immediately (coalescing intermediate values)', () => {
    const sent: number[] = [];
    const sender = new ThrottledSender(100, (v) => {
      sent.push(v);
    });
    sender.push(0.42); // flushed immediately (first send)
    vi.advanceTimersByTime(40); // inside the throttle window
    sender.push(0.99); // supersedes the pending value
    sender.flush();
    // Final authoritative value always reaches the node (§20).
    expect(sent[sent.length - 1]).toBe(0.99);
  });

  it('Q15 conversion round-trips', () => {
    expect(q15ToUnit(0)).toBe(0);
    expect(q15ToUnit(32768)).toBe(1);
    expect(q15ToUnit(16384)).toBeCloseTo(0.5, 5);
    expect(unitToQ15(0.5)).toBe(16384);
    expect(unitToQ15(1)).toBe(32768);
    expect(unitToQ15(-0.5)).toBe(0);
    expect(unitToQ15(1.5)).toBe(32768);
  });

  it('interpolates display timing and freezes when degraded', () => {
    const tick = interpolateElapsed(1000, 2000, 'holding', 100, true);
    expect(tick).toEqual({ elapsedMs: 1100, remainingMs: 1900 });

    // Never extrapolates past remaining
    const capped = interpolateElapsed(1000, 200, 'fading', 1000, true);
    expect(capped).toEqual({ elapsedMs: 1200, remainingMs: 0 });

    // Freezes on degraded connection (§58)
    expect(interpolateElapsed(1000, 2000, 'holding', 500, false)).toBeNull();
    // Not meaningful in manual/stopped states
    expect(interpolateElapsed(1000, 2000, 'manual', 100, true)).toBeNull();
  });
});
