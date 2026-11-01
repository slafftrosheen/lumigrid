import { beforeEach, describe, expect, it, vi } from 'vitest';
import { get } from 'svelte/store';
import { createTimelineEditorStore } from './editorStore';
import { createDefaultTimelineDocument, getSequence, isTimelineDirty, saveSequence, type TimelineDocument } from '../api/timeline';
import { parseTimelineDocument } from './validation';

const ownership = vi.hoisted(() => ({ ip: 'node-a', epoch: 0, session: {} }));
vi.mock('../state/nodeStore', () => ({
  captureNodeOperation: (ip: string) => {
    const epoch = ownership.epoch;
    const session = ownership.session;
    return { ip, isCurrent: () => ownership.ip === ip && ownership.epoch === epoch && ownership.session === session };
  },
}));
vi.mock('../auth/nodeAuth', () => ({ nodeAuthStore: { getSession: () => ownership.session } }));
vi.mock('../api/timeline', async (original) => ({
  ...await original<typeof import('../api/timeline')>(), getSequence: vi.fn(), saveSequence: vi.fn(),
}));

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

function document(bpm = 120, checksum = 11) {
  return { ...createDefaultTimelineDocument(bpm), checksum };
}

async function ready() {
  const editor = createTimelineEditorStore();
  await editor.load('node-a-id', 'node-a');
  editor.setSceneSlots(new Set([0, 2, 5]));
  return editor;
}

describe('Timeline document ownership and drafts', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    ownership.ip = 'node-a'; ownership.epoch = 0; ownership.session = {};
    vi.mocked(getSequence).mockResolvedValue(document());
    vi.mocked(saveSequence).mockResolvedValue({ saved: true, checksum: 22 });
  });

  it('clears foreign-node content immediately even when the new document cannot load', async () => {
    const editor = await ready(); editor.setBpm(140);
    const pending = deferred<TimelineDocument>();
    vi.mocked(getSequence).mockReturnValueOnce(pending.promise);
    ownership.ip = 'node-b'; ++ownership.epoch;
    const load = editor.load('node-b-id', 'node-b');
    expect(get(editor).working).toBeNull();
    expect(get(editor).persisted).toBeNull();
    pending.reject(new Error('Node unreachable')); await load;
    expect(get(editor)).toMatchObject({ loaded: false, loading: false, error: 'Node unreachable' });
    expect(await editor.save()).toBe(false);
    editor.importDocument(document());
    expect(get(editor).working).toBeNull();
    expect(saveSequence).not.toHaveBeenCalled();
  });

  it('ignores an old A response after A → B → A', async () => {
    const editor = createTimelineEditorStore();
    const pending = deferred<TimelineDocument>();
    vi.mocked(getSequence).mockReturnValueOnce(pending.promise);
    const loadA = editor.load('node-a-id', 'node-a');
    ownership.ip = 'node-b'; ++ownership.epoch;
    await editor.load('node-b-id', 'node-b');
    ownership.ip = 'node-a'; ++ownership.epoch;
    vi.mocked(getSequence).mockResolvedValueOnce(document(160, 33));
    await editor.load('node-a-id', 'node-a');
    pending.resolve(document(90, 4)); await loadA;
    expect(get(editor).working).toMatchObject({ bpm: 160, checksum: 33 });
  });

  it('reset invalidates a pending load rather than repopulating an unmounted editor', async () => {
    const editor = createTimelineEditorStore();
    const pending = deferred<TimelineDocument>(); vi.mocked(getSequence).mockReturnValueOnce(pending.promise);
    const load = editor.load('node-a-id', 'node-a'); editor.reset();
    pending.resolve(document()); await load;
    expect(get(editor)).toMatchObject({ nodeIp: null, working: null, persisted: null, loading: false });
  });

  it('replaces the same-host credential owner and discards its delayed save', async () => {
    const editor = await ready(); editor.setBpm(135);
    const pending = deferred<any>(); vi.mocked(saveSequence).mockReturnValueOnce(pending.promise);
    const save = editor.save();
    ownership.session = {};
    vi.mocked(getSequence).mockResolvedValueOnce(document(95, 44));
    await editor.load('node-a-id', 'node-a', true);
    pending.resolve({ saved: true, checksum: 99 });
    expect(await save).toBe(false);
    expect(get(editor).working).toMatchObject({ bpm: 95, checksum: 44 });
  });

  it('keeps a same-session reconnect draft and does not silently rebase a remote conflict', async () => {
    const editor = await ready(); editor.setBpm(140);
    ++ownership.epoch;
    await editor.load('node-a-id', 'node-a', true);
    expect(get(editor).working?.bpm).toBe(140);
    expect(get(editor).persisted?.checksum).toBe(11);
    ++ownership.epoch;
    vi.mocked(getSequence).mockResolvedValueOnce(document(150, 33));
    await editor.load('node-a-id', 'node-a', true);
    expect(get(editor)).toMatchObject({ conflict: true, conflictChecksum: 33, loaded: true });
    expect(get(editor).working?.bpm).toBe(140);
    expect(get(editor).persisted).toMatchObject({ bpm: 120, checksum: 11 });
    editor.keepDraft();
    expect(get(editor)).toMatchObject({ conflict: false, remoteChanged: true });
    editor.reviewConflict();
    expect(get(editor).conflict).toBe(true);
  });

  it('saves an immutable submission while preserving newer edits against that saved baseline', async () => {
    const editor = await ready(); editor.setBpm(140);
    const pending = deferred<any>(); vi.mocked(saveSequence).mockReturnValueOnce(pending.promise);
    const save = editor.save();
    editor.setBpm(160); editor.addBlock(0, 5, 8);
    expect(vi.mocked(saveSequence).mock.calls[0][1]).toMatchObject({ bpm: 140 });
    pending.resolve({ saved: true, checksum: 22 }); expect(await save).toBe(true);
    const state = get(editor);
    expect(state.persisted).toMatchObject({ bpm: 140, checksum: 22 });
    expect(state.persisted?.tracks[0].steps).toEqual([]);
    expect(state.working).toMatchObject({ bpm: 160, checksum: 22 });
    expect(state.working?.tracks[0].steps).toEqual([{ presetIndex: 5, lengthBeats: 8 }]);
    expect(isTimelineDirty(state.working, state.persisted)).toBe(true);
    expect(saveSequence).toHaveBeenCalledWith('node-a', expect.any(Object), 11);
  });

  it('ignores a save superseded by a same-node reload', async () => {
    const editor = await ready(); editor.setBpm(140);
    const pending = deferred<any>(); vi.mocked(saveSequence).mockReturnValueOnce(pending.promise);
    const save = editor.save();
    vi.mocked(getSequence).mockResolvedValueOnce(document(180, 55));
    await editor.reloadNodeVersion(); // Reload is blocked while a save owns this document.
    expect(getSequence).toHaveBeenCalledTimes(1);
    await editor.load('node-a-id', 'node-a', true); // Reconnect/retry invalidates the pending save.
    pending.resolve({ saved: true, checksum: 22 }); expect(await save).toBe(false);
    expect(get(editor).working?.bpm).toBe(140);
    expect(get(editor).conflictChecksum).toBe(55);
  });

  it('blocks saves when the required scene library is missing or references are invalid', async () => {
    const editor = await ready(); editor.addBlock(0, 5, 8); editor.setSceneSlots(null);
    expect(await editor.save()).toBe(false);
    editor.setSceneSlots(new Set([0]));
    expect(await editor.save()).toBe(false);
    expect(saveSequence).not.toHaveBeenCalled();
    expect(get(editor).error).toMatch(/missing scene/);
  });

  it('keeps a conflicted draft through a failed reload, then replaces it only with a fresh successful read', async () => {
    const editor = await ready(); editor.setBpm(140);
    vi.mocked(saveSequence).mockRejectedValueOnce({ status: 409, currentChecksum: 33 });
    await editor.save();
    vi.mocked(getSequence).mockRejectedValueOnce(new Error('Connection lost'));
    await editor.reloadNodeVersion();
    expect(get(editor)).toMatchObject({ conflict: true, loading: false, loaded: false });
    expect(get(editor).working?.bpm).toBe(140);
    vi.mocked(getSequence).mockResolvedValueOnce(document(150, 33));
    await editor.reloadNodeVersion();
    expect(get(editor)).toMatchObject({ conflict: false, loaded: true });
    expect(get(editor).remoteChanged).toBe(false);
    expect(get(editor).working).toMatchObject({ bpm: 150, checksum: 33 });
  });

  it('retains an unresolved conflict when the HTTP error has no checksum body', async () => {
    const editor = await ready(); editor.setBpm(140);
    vi.mocked(saveSequence).mockRejectedValueOnce({ status: 409, serverMessage: 'timeline_conflict' });
    await editor.save(); editor.keepDraft();
    expect(get(editor)).toMatchObject({ conflict: false, conflictChecksum: null, remoteChanged: true });
    expect(get(editor).persisted?.checksum).toBe(11);
  });

  it('does not turn a malformed successful read into an empty saved document', async () => {
    vi.mocked(getSequence).mockResolvedValueOnce({} as TimelineDocument);
    const editor = await ready();
    expect(get(editor)).toMatchObject({ working: null, persisted: null, loaded: false });
    expect(get(editor).error).toMatch(/schema 2/);
  });

  it('does not claim a save receipt without a usable saved checksum', async () => {
    const editor = await ready(); editor.setBpm(140);
    vi.mocked(saveSequence).mockResolvedValueOnce({ saved: true });
    expect(await editor.save()).toBe(false);
    expect(get(editor)).toMatchObject({ loaded: false, saving: false });
    expect(get(editor).persisted?.bpm).toBe(120);
    expect(get(editor).working?.bpm).toBe(140);
    expect(get(editor).error).toMatch(/could not be confirmed/);
  });

  it('changes the selected scene in place without appending or resizing its block', async () => {
    const editor = await ready(); editor.addBlock(0, 0, 12); editor.addBlock(0, 5, 6);
    editor.replaceBlockScene(0, 0, 2);
    expect(get(editor).working?.tracks[0]).toMatchObject({ numSteps: 2, steps: [
      { presetIndex: 2, lengthBeats: 12 }, { presetIndex: 5, lengthBeats: 6 },
    ] });
  });

  it('imports valid exported JSON as a dirty draft without trusting its checksum', async () => {
    const editor = await ready();
    const { checksum: _checksum, ...exported } = document(145);
    editor.importDocument(parseTimelineDocument({ ...exported, type: 'lumigrid-timeline' }));
    expect(get(editor).working?.bpm).toBe(145);
    expect(get(editor).persisted?.checksum).toBe(11);
    await editor.save();
    expect(saveSequence).toHaveBeenCalledWith('node-a', expect.any(Object), 11);
  });

  it.each([{}, [], { schema: 2 }, { ...document(), schema: 99 },
    { ...document(), bpm: NaN }, { ...document(), bpm: 120.5 },
    { ...document(), tracks: [ { channelId: 0, enabled: true, numSteps: 1000000000, steps: [] } ] },
  ])('rejects malformed source documents before normalization (%j)', (input) => {
    expect(() => parseTimelineDocument(input)).toThrow();
  });
});
