import { beforeEach, describe, expect, it, vi } from 'vitest';
import { get } from 'svelte/store';
import { loadPageFunctions } from '$lib/testing/pageScriptHarness';
import { cueEditorStore, isDirty } from '$lib/cues/editorStore';
import { createDefaultCueList, importCueList, MAX_CUE_LISTS } from '$lib/cues/document';
import { CueListConflictError, toDocument } from '$lib/api/cues';

const a = '192.168.1.50';
const b = '192.168.1.60';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

function wireDoc(index = 0, name = 'Saved show') {
  return { ...createDefaultCueList(index, name), endAction: 0, checksum: 42 };
}

function summary(index = 0, name = 'Saved show') {
  return { index, name, count: 1, mode: 'single_shot', endAction: 0, checksum: 42 };
}

async function settle() {
  for (let i = 0; i < 8; i++) await Promise.resolve();
}

// Run the page's production functions. State getters represent the editor's
// derived bindings; API promises are held at the actual network boundaries.
function workspace() {
  let focusedIp = a;
  let generation = 1;
  const sessions = new Map([[a, {}], [b, {}]]);
  cueEditorStore.bindNode(a);
  cueEditorStore.load(toDocument(wireDoc()));
  const context: Record<string, any> = {
    mounted: true,
    nodeIp: a,
    nodeOwner: null,
    nodeSession: sessions.get(a),
    selectionGeneration: 0,
    libraryGeneration: 0,
    loadingGeneration: 0,
    toastTimer: null,
    toast: '',
    loading: false,
    deleting: false,
    error: '',
    cueLists: [summary()],
    presets: [{ index: 0, name: 'Scene 1' }],
    libraryReady: true,
    deleteTarget: null,
    deleteDialogOpen: false,
    newSlotChooserOpen: false,
    navGuardOpen: false,
    pendingNav: null,
    isFollower: false,
    runtimeListIndex: null,
    editor: cueEditorStore,
    get,
    isDirty,
    createDefaultCueList,
    importCueList,
    MAX_CUE_LISTS,
    CueListConflictError,
    toDocument,
    nodeAuthStore: { getSession: (ip: string) => sessions.get(ip) ?? null },
    captureNodeOperation: (ip: string) => {
      const capturedGeneration = generation;
      const session = sessions.get(ip);
      return { ip, isCurrent: () => generation === capturedGeneration && focusedIp === ip && sessions.get(ip) === session };
    },
    getCueLists: vi.fn(async () => [summary()]),
    getPresets: vi.fn(async () => [{ index: 0, name: 'Scene 1' }]),
    getCueList: vi.fn(async (_ip: string, index: number) => wireDoc(index)),
    saveCueList: vi.fn(async () => ({ saved: true, checksum: 99, commandId: 10 })),
    deleteCueList: vi.fn(async () => ({ deleted: true })),
    startCueList: vi.fn(async () => ({ accepted: true, commandId: 11 })),
    restartCueList: vi.fn(async () => ({ accepted: true, commandId: 12 })),
    setCueCrossfade: vi.fn(async () => ({ accepted: true, commandId: 13 })),
    commandTracker: { register: vi.fn() },
    toastMsg: vi.fn((message: string) => { context.toast = message; }),
  };
  Object.defineProperties(context, {
    editorSnapshot: { get: () => get(cueEditorStore) },
    working: { get: () => get(cueEditorStore).working },
    saving: { get: () => get(cueEditorStore).saving },
    freeSlots: { get: () => Array.from({ length: MAX_CUE_LISTS }, (_, index) => index)
      .filter((index) => !context.cueLists.some((list: { index: number }) => list.index === index)) },
  });
  const page = loadPageFunctions(new URL('./+page.svelte', import.meta.url), [
    'capturePageOperation', 'captureEditorOperation', 'beginSelection', 'openDraft',
    'loadData', 'selectList', 'doSelectList', 'persistDraft', 'saveDraft',
    'saveAndRun', 'startSavedList', 'runShow', 'runSavedVersion', 'restartShow',
    'transportAction', 'sendCrossfade', 'requestDeleteList', 'confirmDeleteList',
    'onImportFile', 'reloadNodeVersion', 'syncFocusedNode',
  ], context);
  context.nodeOwner = page.capturePageOperation();
  function invalidate(ip = focusedIp, replaceSession = false) {
    generation++;
    focusedIp = ip;
    if (replaceSession) sessions.set(ip, {});
  }
  function focus(ip: string, replaceSession = false) {
    invalidate(ip, replaceSession);
    page.syncFocusedNode(ip);
  }
  return { context, page, focus, invalidate };
}

describe('Cue Studio asynchronous ownership', () => {
  beforeEach(() => cueEditorStore.reset());

  it('surfaces failed library reads without erasing the saved baseline or draft', async () => {
    const { context, page } = workspace();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Keep this draft' }));
    context.getCueLists.mockRejectedValueOnce(new Error('Library unavailable'));
    await page.loadData();
    expect(context.error).toBe('Library unavailable');
    expect(context.cueLists).toEqual([summary()]);
    expect(context.working.name).toBe('Keep this draft');
    expect(context.editorSnapshot.persisted.name).toBe('Saved show');
    expect(context.getCueList).not.toHaveBeenCalled();
    expect(context.loading).toBe(false);
  });

  it('does not invent an empty library when scene metadata fails', async () => {
    const { context, page } = workspace();
    context.libraryReady = false;
    context.getPresets.mockRejectedValueOnce(new Error('Scene read failed'));
    await page.loadData(false);
    expect(context.error).toBe('Scene read failed');
    expect(context.libraryReady).toBe(false);
    expect(context.working.checksum).toBe(42);
    expect(context.getCueList).not.toHaveBeenCalled();
  });

  it('keeps the current document when the requested list cannot be read', async () => {
    const { context, page } = workspace();
    context.getCueList.mockRejectedValueOnce(new Error('Document read failed'));
    await page.doSelectList(4);
    expect(context.error).toBe('Document read failed');
    expect(context.working.index).toBe(0);
    expect(context.editorSnapshot.persisted.checksum).toBe(42);
  });

  it('accepts only the latest document selection', async () => {
    const { context, page } = workspace();
    const first = deferred<ReturnType<typeof wireDoc>>();
    context.getCueList.mockReturnValueOnce(first.promise);
    const older = page.doSelectList(1);
    await page.doSelectList(2);
    first.resolve(wireDoc(1, 'Late old selection'));
    await older;
    expect(context.working.index).toBe(2);
  });

  it('preserves edits made while a selected document is loading', async () => {
    const { context, page } = workspace();
    const document = deferred<ReturnType<typeof wireDoc>>();
    context.getCueList.mockReturnValueOnce(document.promise);
    const selection = page.doSelectList(2);
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Newer edits' }));
    document.resolve(wireDoc(2));
    await selection;
    expect(context.working.name).toBe('Newer edits');
    expect(context.working.index).toBe(0);
  });

  it('does not commit a save of the previous visible document into a newly loaded selection', async () => {
    const { context, page } = workspace();
    const document = deferred<ReturnType<typeof wireDoc>>();
    const saved = deferred<{ saved: boolean; checksum: number; commandId: number }>();
    context.getCueList.mockReturnValueOnce(document.promise);
    context.saveCueList.mockReturnValueOnce(saved.promise);
    const selection = page.doSelectList(2);
    const saving = page.saveAndRun();
    document.resolve(wireDoc(2, 'New selection'));
    await selection;
    saved.resolve({ saved: true, checksum: 999, commandId: 99 });
    await saving;
    expect(context.working.index).toBe(2);
    expect(context.editorSnapshot.persisted.name).toBe('New selection');
    expect(context.commandTracker.register).not.toHaveBeenCalled();
    expect(context.startCueList).not.toHaveBeenCalled();
    expect(context.saving).toBe(false);
  });

  it('finishes loading when a save refresh supersedes an earlier workspace read', async () => {
    const { context, page } = workspace();
    const lists = deferred<ReturnType<typeof summary>[]>();
    context.getCueLists.mockReturnValueOnce(lists.promise).mockResolvedValueOnce([summary(0, 'Saved revision')]);
    const loading = page.loadData();
    await page.persistDraft();
    lists.resolve([summary(0, 'Older metadata')]);
    await loading;
    expect(context.loading).toBe(false);
    expect(context.cueLists[0].name).toBe('Saved revision');
  });

  it('does not let an old library response replace the newly focused node', async () => {
    const { context, page, focus } = workspace();
    const oldLists = deferred<ReturnType<typeof summary>[]>();
    context.getCueLists.mockReturnValueOnce(oldLists.promise).mockResolvedValueOnce([summary(3, 'Node B show')]);
    context.getCueList.mockResolvedValueOnce(wireDoc(3, 'Node B show'));
    const loading = page.loadData(false);
    focus(b);
    await settle();
    oldLists.resolve([summary(1, 'Node A show')]);
    await loading;
    expect(context.nodeIp).toBe(b);
    expect(context.cueLists[0].name).toBe('Node B show');
    expect(context.working.name).toBe('Node B show');
    expect(context.loading).toBe(false);
  });

  it('clears node-owned state and dialogs when the selected node is removed', async () => {
    const { context, focus } = workspace();
    context.navGuardOpen = true;
    context.pendingNav = vi.fn();
    context.newSlotChooserOpen = true;
    focus('');
    expect(context.nodeIp).toBe('');
    expect(context.working).toBeNull();
    expect(context.cueLists).toEqual([]);
    expect(context.presets).toEqual([]);
    expect(context.libraryReady).toBe(false);
    expect(context.pendingNav).toBeNull();
    expect(context.navGuardOpen).toBe(false);
    expect(context.newSlotChooserOpen).toBe(false);
    expect(context.loading).toBe(false);
  });

  it('loads an initially absent selection when a node becomes focused', async () => {
    const { context, focus } = workspace();
    focus('');
    focus(b);
    await settle();
    expect(context.getCueLists).toHaveBeenCalledWith(b);
    expect(context.getCueList).toHaveBeenCalledWith(b, 0);
    expect(context.working.name).toBe('Saved show');
  });

  it('preserves a dirty draft across reconnects and clears it for replaced credentials', async () => {
    const { context, focus } = workspace();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Local draft' }));
    focus(a);
    await settle();
    expect(context.working.name).toBe('Local draft');
    expect(context.getCueList).not.toHaveBeenCalled();
    const lists = deferred<ReturnType<typeof summary>[]>();
    context.getCueLists.mockReturnValueOnce(lists.promise);
    focus(a, true);
    expect(context.working).toBeNull();
    expect(context.cueLists).toEqual([]);
    lists.resolve([summary()]);
    await settle();
    expect(context.working.name).toBe('Saved show');
  });

  it('commits exactly the submitted copy and runs it while preserving newer edits', async () => {
    const { context, page } = workspace();
    const saved = deferred<{ saved: boolean; checksum: number; commandId: number }>();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Submitted show' }));
    context.saveCueList.mockReturnValueOnce(saved.promise);
    const running = page.saveAndRun();
    expect(context.saving).toBe(true);
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Next revision' }));
    saved.resolve({ saved: true, checksum: 88, commandId: 30 });
    await running;
    expect(context.saveCueList).toHaveBeenCalledWith(a, expect.objectContaining({ name: 'Submitted show' }), 42);
    expect(context.editorSnapshot.persisted.name).toBe('Submitted show');
    expect(context.working.name).toBe('Next revision');
    expect(context.working.checksum).toBe(88);
    expect(isDirty(context.editorSnapshot)).toBe(true);
    expect(context.startCueList).toHaveBeenCalledWith(a, 0, 0);
    expect(context.toast).toBe('Running “Submitted show”');
    expect(context.saving).toBe(false);
  });

  it('prevents duplicate saves while a submission is pending', async () => {
    const { context, page } = workspace();
    const saved = deferred<{ saved: boolean; checksum: number }>();
    context.saveCueList.mockReturnValueOnce(saved.promise);
    const first = page.persistDraft();
    expect(await page.persistDraft()).toBeNull();
    expect(context.saveCueList).toHaveBeenCalledTimes(1);
    saved.resolve({ saved: true, checksum: 99 });
    await first;
  });

  it.each(['node', 'return', 'session', 'destroy'] as const)('drops a pending save/run after %s ownership changes', async (change) => {
    const { context, page, invalidate } = workspace();
    const saved = deferred<{ saved: boolean; checksum: number; commandId: number }>();
    context.saveCueList.mockReturnValueOnce(saved.promise);
    const pending = page.saveAndRun();
    if (change === 'node') invalidate(b);
    if (change === 'return') { invalidate(b); invalidate(a); }
    if (change === 'session') invalidate(a, true);
    if (change === 'destroy') context.mounted = false;
    saved.resolve({ saved: true, checksum: 999, commandId: 99 });
    await pending;
    expect(context.editorSnapshot.persisted.checksum).toBe(42);
    expect(context.commandTracker.register).not.toHaveBeenCalled();
    expect(context.getCueLists).not.toHaveBeenCalled();
    expect(context.startCueList).not.toHaveBeenCalled();
  });

  it('does not run a saved list after switching selection during the library refresh', async () => {
    const { context, page } = workspace();
    const lists = deferred<ReturnType<typeof summary>[]>();
    context.getCueLists.mockReturnValueOnce(lists.promise);
    const pending = page.saveAndRun();
    await settle();
    await page.doSelectList(2);
    lists.resolve([summary()]);
    await pending;
    expect(context.startCueList).not.toHaveBeenCalled();
    expect(context.working.index).toBe(2);
    expect(context.saving).toBe(false);
  });

  it('retains edits and stops save/run on a rejected save', async () => {
    const { context, page } = workspace();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Unsaved edit' }));
    context.saveCueList.mockRejectedValueOnce(new Error('Write rejected'));
    await page.saveAndRun();
    expect(context.editorSnapshot.error).toBe('Write rejected');
    expect(context.working.name).toBe('Unsaved edit');
    expect(isDirty(context.editorSnapshot)).toBe(true);
    expect(context.saving).toBe(false);
    expect(context.startCueList).not.toHaveBeenCalled();
  });

  it('keeps the conflict and draft when loading the node version fails', async () => {
    const { context, page } = workspace();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Conflicted draft' }));
    cueEditorStore.setConflict(77);
    context.getCueList.mockRejectedValueOnce(new Error('Reload unavailable'));
    await page.reloadNodeVersion();
    expect(context.working.name).toBe('Conflicted draft');
    expect(context.editorSnapshot.conflict.currentChecksum).toBe(77);
    expect(context.editorSnapshot.error).toBe('Reload unavailable');
    expect(context.saving).toBe(false);
  });

  it('ignores a conflict reload if the operator edits during the request', async () => {
    const { context, page } = workspace();
    const document = deferred<ReturnType<typeof wireDoc>>();
    context.getCueList.mockReturnValueOnce(document.promise);
    const pending = page.reloadNodeVersion();
    cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Newer draft' }));
    document.resolve(wireDoc(0, 'Node version'));
    await pending;
    expect(context.working.name).toBe('Newer draft');
    expect(context.saving).toBe(false);
  });

  it('does not follow a completed delete onto another node', async () => {
    const { context, page, invalidate } = workspace();
    const deleted = deferred<{ deleted: boolean }>();
    context.deleteCueList.mockReturnValueOnce(deleted.promise);
    page.requestDeleteList();
    const pending = page.confirmDeleteList();
    invalidate(b);
    deleted.resolve({ deleted: true });
    await pending;
    expect(context.deleteCueList).toHaveBeenCalledWith(a, 0);
    expect(context.getCueLists).not.toHaveBeenCalled();
    expect(context.working.checksum).toBe(42);
    expect(context.toastMsg).not.toHaveBeenCalled();
  });

  it('retains the deleted browser copy as an unsaved draft if refresh fails', async () => {
    const { context, page } = workspace();
    context.getCueLists.mockRejectedValueOnce(new Error('Refresh unavailable'));
    page.requestDeleteList();
    await page.confirmDeleteList();
    expect(context.working.name).toBe('Saved show');
    expect(context.editorSnapshot.persisted).toBeNull();
    expect(isDirty(context.editorSnapshot)).toBe(true);
    expect(context.cueLists).toEqual([]);
    expect(context.error).toBe('Refresh unavailable');
    expect(context.deleting).toBe(false);
    expect(context.deleteDialogOpen).toBe(false);
  });

  it('keeps a newer selection when a delete refresh completes late', async () => {
    const { context, page } = workspace();
    const lists = deferred<ReturnType<typeof summary>[]>();
    context.getCueLists.mockReturnValueOnce(lists.promise);
    page.requestDeleteList();
    const pending = page.confirmDeleteList();
    await settle();
    await page.doSelectList(4);
    lists.resolve([summary(3)]);
    await pending;
    expect(context.working.index).toBe(4);
    expect(context.getCueList).toHaveBeenCalledTimes(1);
  });

  it.each(['node', 'selection', 'edit'] as const)('drops file imports after a newer %s takes ownership', async (change) => {
    const { context, page, invalidate } = workspace();
    const content = deferred<string>();
    const event = { target: { files: [{ text: () => content.promise }], value: 'show.json' } } as unknown as Event;
    const pending = page.onImportFile(event);
    if (change === 'node') invalidate(b);
    if (change === 'selection') await page.doSelectList(2);
    if (change === 'edit') cueEditorStore.updateWorking((doc) => ({ ...doc, name: 'Newer draft' }));
    content.resolve(JSON.stringify(createDefaultCueList(0, 'Imported show')));
    await pending;
    expect(context.working.name).not.toBe('Imported show');
    expect(context.toastMsg).not.toHaveBeenCalled();
  });

  it('does not relabel a late run response or queued crossfade for a new session', async () => {
    const { context, page, invalidate } = workspace();
    const run = deferred<{ accepted: boolean; commandId: number }>();
    context.startCueList.mockReturnValueOnce(run.promise);
    const pending = page.runShow();
    const crossfadeOwner = page.capturePageOperation();
    invalidate(a, true);
    run.resolve({ accepted: true, commandId: 10 });
    await pending;
    await page.sendCrossfade(0.75, crossfadeOwner);
    expect(context.commandTracker.register).not.toHaveBeenCalled();
    expect(context.toastMsg).not.toHaveBeenCalled();
    expect(context.setCueCrossfade).not.toHaveBeenCalled();
  });
});
