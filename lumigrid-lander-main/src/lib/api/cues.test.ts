import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  getCueLists,
  getCueList,
  saveCueList,
  deleteCueList,
  controlCue,
  getCueStatus,
  toDocument,
  type CueList,
} from './cues';
import { createDefaultCue, createDefaultCueList } from '../cues/document';

describe('cues API and data structures', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('creates valid default cue and cue list', () => {
    const cue = createDefaultCue(2);
    expect(cue.sceneIndex).toBe(2);
    expect(cue.holdMs).toBe(3000);
    expect(cue.fadeMs).toBe(1000);
    expect(cue.curve).toBe('easeInOut');

    const list = createDefaultCueList(1, 'Show A');
    expect(list.index).toBe(1);
    expect(list.name).toBe('Show A');
    expect(list.mode).toBe('single_shot');
    expect(list.cues).toHaveLength(1);
    expect(list.cues[0].sceneIndex).toBe(0);
  });

  it('fetches cue lists correctly', async () => {
    const mockLists = [
      { index: 0, name: 'Show 1', mode: 'loop', count: 2, endAction: 0 },
    ];
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => mockLists,
      })
    );

    const lists = await getCueLists('192.168.1.50');
    expect(lists).toEqual(mockLists);
    expect(fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/cue-lists',
      expect.any(Object)
    );
  });

  it('fetches single cue list with query parameter', async () => {
    const mockList: CueList = {
      index: 2,
      name: 'Show 3',
      mode: 'ping_pong',
      count: 1,
      endAction: 0,
      cues: [createDefaultCue(0)],
    };
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => mockList,
      })
    );

    const list = await getCueList('192.168.1.50', 2);
    expect(list.name).toBe('Show 3');
    expect(fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/cue-list?index=2',
      expect.any(Object)
    );
  });

  it('saves cue list sending PUT with JSON body', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => ({ saved: true, commandId: 104 }),
      })
    );

    const res = await saveCueList('192.168.1.50', createDefaultCueList(0, 'Cue List 1'));
    expect(res.saved).toBe(true);
    expect(res.commandId).toBe(104);

    expect(fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/cue-list?index=0',
      expect.objectContaining({
        method: 'PUT',
      })
    );
  });

  it('deletes cue list sending DELETE request', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => ({ deleted: true }),
      })
    );

    const res = await deleteCueList('192.168.1.50', 1);
    expect(res.deleted).toBe(true);
    expect(fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/cue-list?index=1',
      expect.objectContaining({
        method: 'DELETE',
      })
    );
  });

  it('dispatches transport controls via POST /api/v1/cue/control', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => ({ accepted: true, commandId: 105 }),
      })
    );

    const res = await controlCue('192.168.1.50', {
      action: 'start',
      cueListIndex: 0,
    });
    expect(res.accepted).toBe(true);
    expect(res.commandId).toBe(105);

    expect(fetch).toHaveBeenCalledWith(
      'http://192.168.1.50/api/v1/cue/control',
      expect.objectContaining({
        method: 'POST',
        body: JSON.stringify({ action: 'start', cueListIndex: 0 }),
      })
    );
  });

  it('fetches live cue status from /api/v1/cue/status', async () => {
    const mockStatus = {
      running: true,
      state: 'fading',
      listIndex: 0,
      cueIndex: 1,
      nextCueIndex: 2,
      mode: 'loop',
      elapsedMs: 250,
      remainingMs: 750,
      manualFade: 0,
    };
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        json: async () => mockStatus,
      })
    );

    const status = await getCueStatus('192.168.1.50');
    expect(status.running).toBe(true);
    expect(status.state).toBe('fading');
  });
});
