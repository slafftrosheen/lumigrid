import { describe, expect, it, vi } from 'vitest';
import { loadPageFunctions } from '../testing/pageScriptHarness';
import { createDefaultPreset, clonePreset, getUtf8ByteLength, isSceneDocumentDirty } from '../api/scenes';
import { createDefaultSceneMotion } from '../api/sceneMotion';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

function document(index = 0, name = 'Submitted scene') {
  return { scene: { ...createDefaultPreset(index, name), checksum: 11 },
    motion: { ...createDefaultSceneMotion(index), checksum: 12 },
    sceneChecksum: 11, motionChecksum: 12 };
}

function workspace() {
  let epoch = 0;
  const context: Record<string, any> = {
    nodeIp: '192.168.1.50', loadGen: 1, selectionGen: 1, disposed: false, libraryRequest: 0,
    workspaceOwner: null, workspaceSession: null, workspaceBound: false, toastTimer: null,
    pendingAction: null, showUnsavedModal: false, toast: '', nodeAuthStore: { getSession: vi.fn(() => null) },
    libraryLoaded: true, documentLoading: false, loading: false, saving: false, activating: false,
    error: '', isFollower: false, isActive: false, snapshot: null,
    selectedIdx: 0, selectedChannelIndices: [0], presets: [document().scene], effects: [], palettes: [], hwChannels: [],
    workingDocument: document(), persistedDocument: document(),
    showApplyChangesBanner: false, showConflictModal: false, showNewSceneModal: false,
    newSceneSlot: 1, newSceneMode: 'blank', freeSlots: [1],
    deleteOwner: null, deleteRequest: 0, deleteTargetIdx: null, deleteReferences: null,
    checkingReferences: false, deletingScene: false, showDeleteModal: false, isSceneBlockedFromDelete: false,
    MAX_SCENE_NAME_BYTES: 23, $state: { snapshot: (value: unknown) => value },
    clonePreset, createDefaultPreset, createDefaultSceneMotion, getUtf8ByteLength, isSceneDocumentDirty,
    toastMsg: vi.fn(), requestNavigation: vi.fn((action: () => void) => action()),
    captureNodeOperation: (ip: string) => {
      const captured = epoch;
      return { ip, isCurrent: () => epoch === captured && context.nodeIp === ip };
    },
    getPresets: vi.fn(async () => [document().scene]),
    getPreset: vi.fn(async (_ip: string, index: number) => document(index).scene),
    getSceneMotion: vi.fn(async (_ip: string, index: number) => document(index).motion),
    getEffectsCached: vi.fn(async () => []), getPalettesCached: vi.fn(async () => []), getChannels: vi.fn(async () => []),
    saveSceneDocument: vi.fn(async () => ({ saved: true, checksum: 21, motionChecksum: 22 })),
    activatePreset: vi.fn(async () => ({ accepted: true, commandId: 5, generation: 8 })),
    commandTracker: { register: vi.fn(), waitFor: vi.fn(async () => undefined) },
    getSceneReferences: vi.fn(), deletePreset: vi.fn(async () => undefined),
    validateImportedSceneDocument: vi.fn(),
  };
  const page = loadPageFunctions(new URL('../../routes/dashboard/scenes/+page.svelte', import.meta.url), [
    'captureWorkspaceOperation', 'captureSceneOperation', 'loadData', 'selectScene', 'saveDocument',
    'doActivate', 'openNewSceneModal', 'confirmNewScene', 'openDeleteModal', 'confirmDeleteScene',
    'handleImportFile', 'keepDraft', 'reloadNodeVersion', 'syncFocusedNode',
  ], context);
  const changeNode = (ip = '192.168.1.60') => {
    ++epoch; ++context.loadGen; ++context.selectionGen; context.nodeIp = ip;
  };
  return { context, page, changeNode, reconnect: () => { ++epoch; } };
}

describe('Scene Studio asynchronous ownership', () => {
  it('does not activate or publish an old save after A → B → A', async () => {
    const { context: c, page, changeNode } = workspace();
    const pending = deferred<any>(); c.saveSceneDocument.mockReturnValueOnce(pending.promise);
    const operation = page.saveDocument(true);
    changeNode(); changeNode('192.168.1.50');
    c.workingDocument = c.persistedDocument = document(2, 'Current document');
    c.saving = true; // A newer operation owns the current busy indicator.
    pending.resolve({ saved: true, checksum: 99, motionChecksum: 100 });
    await operation;
    expect(c.activatePreset).not.toHaveBeenCalled();
    expect(c.persistedDocument.scene.name).toBe('Current document');
    expect(c.saving).toBe(true);
    expect(c.toastMsg).not.toHaveBeenCalled();
  });

  it('keeps edits made during a save dirty against the accepted submitted copy', async () => {
    const { context: c, page } = workspace();
    const pending = deferred<any>(); c.saveSceneDocument.mockReturnValueOnce(pending.promise);
    const operation = page.saveDocument();
    c.workingDocument.scene.name = 'Newer edit';
    c.workingDocument.motion.transitionCurve = 'linear';
    pending.resolve({ saved: true, checksum: 21, motionChecksum: 22 }); await operation;
    expect(c.saveSceneDocument.mock.calls[0][1].scene.name).toBe('Submitted scene');
    expect(c.persistedDocument.scene.name).toBe('Submitted scene');
    expect(c.workingDocument.scene.name).toBe('Newer edit');
    expect(c.workingDocument.sceneChecksum).toBe(21);
    expect(c.workingDocument.motionChecksum).toBe(22);
    expect(isSceneDocumentDirty(c.workingDocument, c.persistedDocument)).toBe(true);
  });

  it('does not register an activation receipt under a newly selected node', async () => {
    const { context: c, page, changeNode } = workspace();
    const pending = deferred<any>(); c.activatePreset.mockReturnValueOnce(pending.promise);
    const operation = page.doActivate(0); changeNode();
    pending.resolve({ accepted: true, commandId: 5, generation: 8 }); await operation;
    expect(c.commandTracker.register).not.toHaveBeenCalled();
    expect(c.toastMsg).not.toHaveBeenCalled();
  });

  it('keeps the original node on accepted command tracking', async () => {
    const { context: c, page } = workspace(); await page.doActivate(0);
    expect(c.commandTracker.register).toHaveBeenCalledWith(expect.objectContaining({ nodeIp: '192.168.1.50', generation: 8 }));
    expect(c.toastMsg).toHaveBeenCalledOnce();
  });

  it('does not let a slower scene selection overwrite the newer selection', async () => {
    const { context: c, page } = workspace();
    c.presets.push(document(1, 'Second scene').scene);
    const pending = deferred<any>(); c.getSceneMotion.mockReturnValueOnce(pending.promise);
    const first = page.selectScene(0); await page.selectScene(1);
    pending.resolve(document(0).motion); await first;
    expect(c.selectedIdx).toBe(1); expect(c.workingDocument.scene.name).toBe('Second scene');
  });

  it('preserves the loaded document when its replacement motion cannot be read', async () => {
    const { context: c, page } = workspace();
    c.getSceneMotion.mockRejectedValueOnce(new Error('connection lost'));
    await page.selectScene(1);
    expect(c.selectedIdx).toBe(0); expect(c.workingDocument.scene.name).toBe('Submitted scene');
    expect(c.error).toContain('Could not load scene'); expect(c.documentLoading).toBe(false);
  });

  it('preserves edits made to the visible scene while another scene is loading', async () => {
    const { context: c, page } = workspace();
    c.presets.push(document(1, 'Second scene').scene);
    const pending = deferred<any>(); c.getSceneMotion.mockReturnValueOnce(pending.promise);
    const selection = page.selectScene(1);
    c.workingDocument.scene.name = 'New edit while loading';
    pending.resolve(document(1).motion); await selection;
    expect(c.selectedIdx).toBe(0); expect(c.workingDocument.scene.name).toBe('New edit while loading');
    expect(c.error).toContain('draft changed'); expect(c.documentLoading).toBe(false);
  });

  it('preserves saved library data on failure and blocks writes until reload', async () => {
    const { context: c, page } = workspace();
    c.getPresets.mockRejectedValueOnce(new Error('read failed'));
    await page.loadData(c.nodeIp, c.loadGen); await page.saveDocument(); page.openNewSceneModal();
    expect(c.presets).toHaveLength(1); expect(c.workingDocument.scene.name).toBe('Submitted scene');
    expect(c.libraryLoaded).toBe(false); expect(c.saveSceneDocument).not.toHaveBeenCalled();
    expect(c.showNewSceneModal).toBe(false);
  });

  it('does not treat failed dependency reads as permission to delete', async () => {
    const { context: c, page } = workspace();
    c.getSceneReferences.mockRejectedValueOnce(new Error('read failed'));
    await page.openDeleteModal(0); await page.confirmDeleteScene();
    expect(c.deletePreset).not.toHaveBeenCalled(); expect(c.deleteReferences).toBeNull();
    expect(c.error).toContain('dependencies');
  });

  it('does not replace a newer draft in the deleted slot after a delayed delete', async () => {
    const { context: c, page } = workspace();
    c.deleteOwner = page.captureWorkspaceOperation(); c.deleteTargetIdx = 0; c.deleteReferences = {};
    const pending = deferred<void>(); c.deletePreset.mockReturnValueOnce(pending.promise);
    const removal = page.confirmDeleteScene();
    ++c.selectionGen; c.workingDocument = document(0, 'New imported draft'); c.persistedDocument = null;
    pending.resolve(); await removal;
    expect(c.workingDocument.scene.name).toBe('New imported draft'); expect(c.persistedDocument).toBeNull();
    expect(c.presets).toHaveLength(0); expect(c.getSceneMotion).not.toHaveBeenCalled();
  });

  it('keeps edits made during deletion as an unsaved recoverable draft', async () => {
    const { context: c, page } = workspace();
    c.deleteOwner = page.captureWorkspaceOperation(); c.deleteTargetIdx = 0; c.deleteReferences = {};
    const pending = deferred<void>(); c.deletePreset.mockReturnValueOnce(pending.promise);
    const removal = page.confirmDeleteScene(); c.workingDocument.scene.name = 'New edit';
    pending.resolve(); await removal;
    expect(c.workingDocument.scene.name).toBe('New edit'); expect(c.persistedDocument).toBeNull();
    expect(c.workingDocument.sceneChecksum).toBeUndefined();
    expect(isSceneDocumentDirty(c.workingDocument, c.persistedDocument)).toBe(true);
  });

  it('discards a file import whose original editor context was replaced', async () => {
    const { context: c, page, changeNode } = workspace();
    const pending = deferred<string>(); const input = { files: [{ text: () => pending.promise }], value: 'file.json' };
    const operation = page.handleImportFile({ target: input }); changeNode();
    pending.resolve('{}'); await operation;
    expect(c.validateImportedSceneDocument).not.toHaveBeenCalled(); expect(input.value).toBe('');
  });

  it('keeping a draft retains its conflict checksums', () => {
    const { context: c, page } = workspace(); c.showConflictModal = true; page.keepDraft();
    expect(c.showConflictModal).toBe(false); expect(c.persistedDocument.sceneChecksum).toBe(11);
    expect(c.persistedDocument.motionChecksum).toBe(12);
  });

  it('conflict reload fetches the current scene and motion instead of the library cache', async () => {
    const { context: c, page } = workspace();
    const fresh = document(0, 'New server version'); fresh.scene.checksum = 31; fresh.motion.checksum = 32;
    c.getPreset.mockResolvedValueOnce(fresh.scene); c.getSceneMotion.mockResolvedValueOnce(fresh.motion);
    c.showConflictModal = true;
    await page.reloadNodeVersion();
    expect(c.getPreset).toHaveBeenCalledWith(c.nodeIp, 0);
    expect(c.workingDocument.scene.name).toBe('New server version');
    expect(c.persistedDocument.sceneChecksum).toBe(31);
    expect(c.persistedDocument.motionChecksum).toBe(32);
    expect(c.presets[0].name).toBe('New server version');
  });

  it('refreshes metadata without replacing the current dirty scene', async () => {
    const { context: c, page } = workspace();
    c.workingDocument.scene.name = 'Unsaved edit';
    await page.loadData(c.nodeIp, c.loadGen);
    expect(c.workingDocument.scene.name).toBe('Unsaved edit');
    expect(c.persistedDocument.scene.name).toBe('Submitted scene');
    expect(c.getSceneMotion).not.toHaveBeenCalled();
    expect(c.libraryLoaded).toBe(true);
  });

  it('preserves a draft across same-session reconnect while cancelling its old save', async () => {
    const { context: c, page, reconnect } = workspace();
    c.workspaceBound = true;
    c.workspaceOwner = c.captureNodeOperation(c.nodeIp);
    const pending = deferred<any>(); c.saveSceneDocument.mockReturnValueOnce(pending.promise);
    const save = page.saveDocument(true);
    c.workingDocument.scene.name = 'Still editing';
    reconnect(); page.syncFocusedNode(c.nodeIp);
    pending.resolve({ saved: true, checksum: 99, motionChecksum: 100 }); await save;
    expect(c.workingDocument.scene.name).toBe('Still editing');
    expect(c.persistedDocument.sceneChecksum).toBe(11);
    expect(c.activatePreset).not.toHaveBeenCalled();
    expect(c.saving).toBe(false);
  });

  it('clears a previous session draft when credentials change at the same address', () => {
    const { context: c, page } = workspace();
    c.workspaceBound = true;
    c.workspaceOwner = c.captureNodeOperation(c.nodeIp);
    c.nodeAuthStore.getSession.mockReturnValue({ token: 'new-session' });
    c.getPresets.mockReturnValue(new Promise(() => {}));
    page.syncFocusedNode(c.nodeIp);
    expect(c.workingDocument).toBeNull(); expect(c.persistedDocument).toBeNull();
    expect(c.libraryLoaded).toBe(false);
  });
});
