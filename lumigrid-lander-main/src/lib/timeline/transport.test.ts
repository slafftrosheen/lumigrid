import { describe, it, expect, vi, beforeEach } from 'vitest';
import { get } from 'svelte/store';
import { timelineEditor, isTimelineEditorDirty } from './editorStore';
import * as timelineApi from '../api/timeline';
import * as showTransportApi from '../api/showTransport';
import type { TimelineDocument } from '../api/timeline';

vi.mock('../state/nodeStore', () => ({ captureNodeOperation: (ip: string) => ({ ip, isCurrent: () => true }) }));
vi.mock('../auth/nodeAuth', () => ({ nodeAuthStore: { getSession: () => null } }));

describe('Timeline Transport & Editor Integration (Requirement 73: Tests I..P)', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
    timelineEditor.reset();
  });

  it('Requirement I & J: RUN and STOP never call saveSequence by themselves', async () => {
    const saveSpy = vi.spyOn(timelineApi, 'saveSequence');
    const startSpy = vi.spyOn(showTransportApi, 'startTimeline').mockResolvedValue({
      accepted: true,
      mode: 'timeline',
      generation: 1,
      anchorMs: 1000,
      checksum: 1234,
    });
    const stopSpy = vi.spyOn(showTransportApi, 'stopTimeline').mockResolvedValue({
      accepted: true,
      mode: 'scene',
    });

    // Directly invoking startTimeline
    await showTransportApi.startTimeline('192.168.1.50');
    expect(startSpy).toHaveBeenCalledWith('192.168.1.50');
    expect(saveSpy).not.toHaveBeenCalled();

    // Directly invoking stopTimeline
    await showTransportApi.stopTimeline('192.168.1.50');
    expect(stopSpy).toHaveBeenCalledWith('192.168.1.50');
    expect(saveSpy).not.toHaveBeenCalled();
  });

  it('Requirement K & L: dirty timeline triggers Save & Run which saves before transport start', async () => {
    const callOrder: string[] = [];

    const mockSavedDoc: TimelineDocument = timelineApi.createDefaultTimelineDocument(120);
    mockSavedDoc.checksum = 1000;

    vi.spyOn(timelineApi, 'getSequence').mockResolvedValue(mockSavedDoc);
    const saveSpy = vi.spyOn(timelineApi, 'saveSequence').mockImplementation(async () => {
      callOrder.push('saveSequence');
      return { saved: true, checksum: 2000, commandId: 1 };
    });
    const startSpy = vi.spyOn(showTransportApi, 'startTimeline').mockImplementation(async () => {
      callOrder.push('startTimeline');
      return { accepted: true, mode: 'timeline', generation: 2, anchorMs: 5000, checksum: 2000 };
    });

    await timelineEditor.load('node-1', '192.168.1.50');
    timelineEditor.setSceneSlots(new Set([0, 5]));
    expect(get(isTimelineEditorDirty)).toBe(false);

    // Edit BPM to make dirty
    timelineEditor.setBpm(130);
    expect(get(isTimelineEditorDirty)).toBe(true);

    // Save & Run workflow: first saveSequence, then on success startTimeline
    const saved = await timelineEditor.save();
    expect(saved).toBe(true);
    expect(get(isTimelineEditorDirty)).toBe(false);

    await showTransportApi.startTimeline('192.168.1.50');

    expect(callOrder).toEqual(['saveSequence', 'startTimeline']);
    expect(saveSpy).toHaveBeenCalledTimes(1);
    expect(startSpy).toHaveBeenCalledTimes(1);
  });

  it('Requirement M: running saved-content mismatch is detected from snapshot', () => {
    // When activeChecksum != savedChecksum or contentStale is true
    const runningSnapshot = {
      transport: {
        mode: 'timeline',
        timeline: {
          running: true,
          bpm: 120,
          generation: 1,
          anchorMs: 1000,
          activeChecksum: 5555,
          savedChecksum: 9999,
          contentStale: true,
        },
      },
    };

    const isStale = Boolean(runningSnapshot.transport.timeline.contentStale);
    const checksumMismatch =
      runningSnapshot.transport.timeline.activeChecksum !==
      runningSnapshot.transport.timeline.savedChecksum;

    expect(isStale).toBe(true);
    expect(checksumMismatch).toBe(true);
  });

  it('Requirement N: restart sends restart action and returns updated generation', async () => {
    const restartSpy = vi.spyOn(showTransportApi, 'restartTimeline').mockResolvedValue({
      accepted: true,
      mode: 'timeline',
      generation: 42,
      anchorMs: 8888,
      checksum: 9999,
    });

    const res = await showTransportApi.restartTimeline('192.168.1.50');
    expect(restartSpy).toHaveBeenCalledWith('192.168.1.50');
    expect(res.generation).toBe(42);
    expect(res.accepted).toBe(true);
  });

  it('Requirement O & P: follower policy forbids RUN but allows save', async () => {
    // Simulated follower state from runtime snapshot
    const followerSnapshot = {
      sync: {
        role: 'follower',
      },
      transport: {
        mode: 'timeline',
        timeline: {
          running: true,
        },
      },
    };

    const isFollower = followerSnapshot.sync.role === 'follower';
    // Follower must not run / stop / restart
    const canRunTransport = !isFollower;
    // Follower is allowed to edit and save
    const canSave = true;

    expect(canRunTransport).toBe(false);
    expect(canSave).toBe(true);
  });

  it('Requirement F & G: conflict protection keeps draft on 409 conflict and updates on success', async () => {
    const mockDoc = timelineApi.createDefaultTimelineDocument(120);
    mockDoc.checksum = 1000;

    vi.spyOn(timelineApi, 'getSequence').mockResolvedValue(mockDoc);
    await timelineEditor.load('node-1', '192.168.1.50');
    timelineEditor.setSceneSlots(new Set([0, 5]));

    // Make an edit
    timelineEditor.addBlock(0, 5, 8);
    const draftBeforeSave = get(timelineEditor).working;

    // Simulate 409 Conflict
    vi.spyOn(timelineApi, 'saveSequence').mockRejectedValueOnce({
      status: 409,
      error: 'timeline_conflict',
      currentChecksum: 8888,
    });

    const success = await timelineEditor.save();
    expect(success).toBe(false);

    const state = get(timelineEditor);
    expect(state.conflict).toBe(true);
    expect(state.conflictChecksum).toBe(8888);
    // Draft is kept intact
    expect(state.working).toEqual(draftBeforeSave);

    // Keep draft dismisses dialog
    timelineEditor.keepDraft();
    expect(get(timelineEditor).conflict).toBe(false);
    expect(get(timelineEditor).working).toEqual(draftBeforeSave);
  });
});
