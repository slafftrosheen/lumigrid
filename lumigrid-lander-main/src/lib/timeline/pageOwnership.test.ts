import { afterEach, describe, expect, it, vi } from 'vitest';
import { loadPageFunctions } from '../testing/pageScriptHarness';
import { createDefaultTimelineDocument } from '../api/timeline';
import { parseTimelineDocument, validateTimelineDocument } from './validation';

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(yes => { resolve = yes; });
  return { promise, resolve };
}

function snapshot(generation = 7, running = false) {
  return { node: { bootSessionId: 1 }, transport: { generation, mode: running ? 'timeline' : 'scene',
    timeline: { running, bpm: 100, generation: 500, activeChecksum: 22 } } };
}

function toolbar() {
  let epoch = 0;
  let session = {};
  const c: Record<string, any> = {
    $nodeStore: { nodeIp: 'node-a' }, $timelineEditor: { loadGeneration: 1, persisted: { bpm: 100, checksum: 22 } },
    $isTimelineEditorDirty: true, canSave: true, canRunSaved: true, canControl: true,
    working: { bpm: 140 }, actionRequest: 0, disposed: false, actionOwner: null,
    actionInProgress: false, actionError: '', actionFeedback: '', pendingTransport: null, confirmationTimer: null,
    snapshot: snapshot(), statusFresh: true, isFollower: false,
    timelineEditor: { save: vi.fn(async () => true) },
    startTimeline: vi.fn(async () => ({ accepted: true, generation: 8, checksum: 22 })),
    restartTimeline: vi.fn(async () => ({ accepted: true, generation: 8, checksum: 22 })),
    stopTimeline: vi.fn(async () => ({ accepted: true, generation: 8 })),
    captureNodeOperation: (ip: string) => {
      const captured = epoch; const capturedSession = session;
      return { ip, isCurrent: () => captured === epoch && capturedSession === session && c.$nodeStore.nodeIp === ip };
    },
  };
  Object.defineProperty(c, 'persisted', { get: () => c.$timelineEditor.persisted });
  const page = loadPageFunctions(new URL('../components/timeline/TimelineToolbar.svelte', import.meta.url), [
    'captureToolbarOperation', 'finishConfirmation', 'observeTransport', 'requestTransport',
    'handleRun', 'handleStop', 'handleRestartWithSaved', 'handleSaveAndRun', 'handleSaveOnly',
  ], c);
  return { c, page, changeNode: (ip: string) => { ++epoch; c.$nodeStore.nodeIp = ip; }, changeSession: () => { session = {}; } };
}

afterEach(() => { vi.clearAllTimers(); vi.useRealTimers(); });

describe('Timeline toolbar production workflows', () => {
  it('Run Saved uses the persisted BPM and awaits a fresh authoritative generation', async () => {
    vi.useFakeTimers();
    const { c, page } = toolbar();
    await page.handleRun();
    expect(c.startTimeline).toHaveBeenCalledWith('node-a', 100);
    expect(c.actionInProgress).toBe(true);
    expect(c.actionFeedback).toMatch(/Accepted.*waiting/);
    page.observeTransport();
    expect(c.actionInProgress).toBe(true);
    c.snapshot = snapshot(8, true); page.observeTransport();
    expect(c.actionInProgress).toBe(false);
    expect(c.actionFeedback).toMatch(/confirmed by the node/);
    expect(c.snapshot.transport.timeline.generation).toBe(500); // Receipt matches unified transport generation.
  });

  it('does not confirm from the same pre-request snapshot, stale status, or different saved content', async () => {
    vi.useFakeTimers();
    const { c, page } = toolbar(); c.snapshot = snapshot(8, true);
    await page.handleRun(); page.observeTransport();
    expect(c.pendingTransport).not.toBeNull();
    c.snapshot = snapshot(8, true); c.statusFresh = false; page.observeTransport();
    expect(c.pendingTransport).not.toBeNull();
    c.statusFresh = true; c.snapshot.transport.timeline.activeChecksum = 23; page.observeTransport();
    expect(c.pendingTransport).not.toBeNull();
    c.snapshot = snapshot(8, true); page.observeTransport();
    expect(c.pendingTransport).toBeNull();
  });

  it('shows a denied request as failure and never as accepted or confirmed', async () => {
    const { c, page } = toolbar();
    c.stopTimeline.mockResolvedValue({ accepted: false, error: 'Follower controlled by leader' });
    await page.handleStop();
    expect(c.actionInProgress).toBe(false);
    expect(c.actionError).toBe('Follower controlled by leader');
    expect(c.actionFeedback).toBe('');
    expect(c.pendingTransport).toBeNull();
  });

  it('ends an unconfirmed accepted request with an explicit deadline error', async () => {
    vi.useFakeTimers();
    const { c, page } = toolbar(); await page.handleRestartWithSaved();
    await vi.advanceTimersByTimeAsync(12000);
    expect(c.actionInProgress).toBe(false);
    expect(c.pendingTransport).toBeNull();
    expect(c.actionError).toMatch(/accepted, but no matching status/);
  });

  it('does not continue Save & Run after A → B → A or clear a newer busy state', async () => {
    const { c, page, changeNode } = toolbar();
    const pending = deferred<boolean>(); c.timelineEditor.save.mockReturnValueOnce(pending.promise);
    const save = page.handleSaveAndRun();
    changeNode('node-b'); changeNode('node-a');
    c.actionInProgress = true; c.actionFeedback = 'Newer request';
    pending.resolve(true); await save;
    expect(c.startTimeline).not.toHaveBeenCalled();
    expect(c.actionInProgress).toBe(true);
    expect(c.actionFeedback).toBe('Newer request');
  });

  it('does not continue Save & Run after credentials change on the same host', async () => {
    const { c, page, changeSession } = toolbar();
    const pending = deferred<boolean>(); c.timelineEditor.save.mockReturnValueOnce(pending.promise);
    const save = page.handleSaveAndRun(); changeSession();
    pending.resolve(true); await save;
    expect(c.startTimeline).not.toHaveBeenCalled();
  });

  it('keeps newer edits as a draft and requires an explicit decision before running', async () => {
    const { c, page } = toolbar();
    await page.handleSaveAndRun(); // The submitted copy saved, but another edit remains dirty.
    expect(c.startTimeline).not.toHaveBeenCalled();
    expect(c.actionFeedback).toMatch(/Newer edits/);
    expect(c.actionInProgress).toBe(false);
  });

  it('starts the exact saved version after save succeeds and the draft still matches', async () => {
    vi.useFakeTimers();
    const { c, page } = toolbar();
    c.timelineEditor.save.mockImplementation(async () => {
      c.$isTimelineEditorDirty = false;
      c.$timelineEditor.persisted = { bpm: 140, checksum: 22 };
      return true;
    });
    await page.handleSaveAndRun();
    expect(c.startTimeline).toHaveBeenCalledWith('node-a', 140);
    expect(c.pendingTransport).not.toBeNull();
  });

  it('does not publish a transport response after document reload invalidates its owner', async () => {
    const { c, page } = toolbar(); const pending = deferred<any>();
    c.startTimeline.mockReturnValueOnce(pending.promise);
    const run = page.handleRun(); ++c.$timelineEditor.loadGeneration;
    c.actionFeedback = 'Current document loading';
    pending.resolve({ accepted: true, generation: 8 }); await run;
    expect(c.pendingTransport).toBeNull();
    expect(c.actionFeedback).toBe('Current document loading');
  });
});

describe('Timeline page production continuations', () => {
  function workspace() {
    let epoch = 0;
    const c: Record<string, any> = {
      activeNodeIp: 'node-a', workspaceGeneration: 1, libraryRequest: 0, disposed: false,
      $nodeStore: { nodeId: 'node-a-id' }, scenes: new Map(), existingSceneSlots: new Set(),
      scenesLoading: false, scenesLoaded: false, scenesError: '',
      timelineEditor: { load: vi.fn(async () => true), setSceneSlots: vi.fn(), reset: vi.fn(),
        replaceBlockScene: vi.fn(), addBlock: vi.fn(), importDocument: vi.fn() },
      getPresets: vi.fn(), pendingDestination: '/dashboard/cues?view=all#cue-2', showUnsavedModal: true,
      goto: vi.fn(), isScenePickerOpen: true, scenePickerChannel: 2, scenePickerTargetStep: 1,
      isImportDialogOpen: true, dialogOwner: null,
      captureNodeOperation: (ip: string) => {
        const generation = epoch;
        return { ip, isCurrent: () => generation === epoch && c.activeNodeIp === ip };
      },
    };
    const page = loadPageFunctions(new URL('../../routes/dashboard/timeline/+page.svelte', import.meta.url), [
      'captureWorkspaceOperation', 'loadNodeData', 'handleScenePicked', 'handleImportDocument',
      'confirmDiscardNavigation', 'cancelDiscardNavigation',
    ], c);
    c.dialogOwner = page.captureWorkspaceOperation();
    return { c, page, changeNode: () => { ++epoch; ++c.workspaceGeneration; c.activeNodeIp = 'node-b'; } };
  }

  it('ignores a late scene library and does not load a timeline after switching nodes', async () => {
    const { c, page, changeNode } = workspace();
    const pending = deferred<any[]>(); c.getPresets.mockReturnValueOnce(pending.promise);
    const load = page.loadNodeData(); changeNode(); c.scenesLoading = true;
    pending.resolve([{ index: 5, name: 'Old node scene' }]); await load;
    expect(c.scenes.size).toBe(0);
    expect(c.scenesLoading).toBe(true);
    expect(c.timelineEditor.load).toHaveBeenCalledTimes(1);
    expect(c.timelineEditor.load).toHaveBeenCalledWith('node-a-id', 'node-a', true);
    expect(c.timelineEditor.setSceneSlots).toHaveBeenCalledTimes(1); // Only initial invalidation.
  });

  it('retains a failed scene read as unavailable instead of a valid empty library', async () => {
    const { c, page } = workspace(); c.getPresets.mockRejectedValueOnce(new Error('Library offline'));
    await page.loadNodeData();
    expect(c.scenesLoaded).toBe(false);
    expect(c.scenesError).toBe('Library offline');
    expect(c.timelineEditor.setSceneSlots).toHaveBeenCalledWith(null);
  });

  it('owns scene replacement and import to the dialog source node', () => {
    const { c, page, changeNode } = workspace(); c.scenesLoaded = true; c.existingSceneSlots.add(5);
    page.handleScenePicked(5);
    expect(c.timelineEditor.replaceBlockScene).toHaveBeenCalledWith(2, 1, 5);
    expect(c.timelineEditor.addBlock).not.toHaveBeenCalled();
    changeNode(); page.handleScenePicked(5); page.handleImportDocument(createDefaultTimelineDocument());
    expect(c.timelineEditor.replaceBlockScene).toHaveBeenCalledTimes(1);
    expect(c.timelineEditor.importDocument).not.toHaveBeenCalled();
  });

  it('Stay keeps the editor and Discard resumes the exact destination including query and hash', () => {
    const { c, page } = workspace(); page.cancelDiscardNavigation();
    expect(c.timelineEditor.reset).not.toHaveBeenCalled();
    expect(c.pendingDestination).toBeNull();
    c.pendingDestination = '/dashboard/cues?view=all#cue-2';
    page.confirmDiscardNavigation();
    expect(c.timelineEditor.reset).toHaveBeenCalledTimes(1);
    expect(c.goto).toHaveBeenCalledWith('/dashboard/cues?view=all#cue-2');
    expect(c.pendingDestination).toBeNull();
  });
});

describe('Timeline import production validation', () => {
  it('rejects arbitrary JSON and copies a valid reactive draft through the snapshot boundary', () => {
    const c: Record<string, any> = {
      jsonText: '{}', validationErrors: [], missingScenes: [], parsedDoc: null, existingSceneSlots: new Set(),
      reading: false, disposed: false, parseTimelineDocument, validateTimelineDocument,
      $state: { snapshot: vi.fn(value => structuredClone(value)) },
      onImportSuccess: vi.fn(), onClose: vi.fn(),
    };
    const page = loadPageFunctions(new URL('../components/timeline/TimelineImportDialog.svelte', import.meta.url), [
      'validateInput', 'handleConfirmImport',
    ], c);
    page.validateInput(); page.handleConfirmImport();
    expect(c.parsedDoc).toBeNull(); expect(c.onImportSuccess).not.toHaveBeenCalled();
    c.jsonText = JSON.stringify(createDefaultTimelineDocument(140));
    page.validateInput(); page.handleConfirmImport();
    expect(c.$state.snapshot).toHaveBeenCalledWith(c.parsedDoc);
    expect(c.onImportSuccess).toHaveBeenCalledWith(expect.objectContaining({ bpm: 140 }));
  });

  it('does not let a delayed file read overwrite newer pasted text', () => {
    const readers: any[] = [];
    const c: Record<string, any> = {
      jsonText: '', validationErrors: [], missingScenes: [], parsedDoc: null, reading: false,
      reader: null, readGeneration: 0, disposed: false, validateInput: vi.fn(),
      FileReader: class { onload: any; onerror: any; result = ''; abort = vi.fn(); readAsText = vi.fn(); constructor() { readers.push(this); } },
    };
    const page = loadPageFunctions(new URL('../components/timeline/TimelineImportDialog.svelte', import.meta.url), [
      'cancelRead', 'handleFileSelected', 'handleTextInput',
    ], c);
    page.handleFileSelected({ target: { files: [{}] } });
    c.jsonText = 'new pasted timeline'; page.handleTextInput();
    readers[0].result = 'old selected file'; readers[0].onload();
    expect(c.jsonText).toBe('new pasted timeline');
    expect(readers[0].abort).toHaveBeenCalled();
    expect(c.validateInput).toHaveBeenCalledTimes(1);
  });
});
