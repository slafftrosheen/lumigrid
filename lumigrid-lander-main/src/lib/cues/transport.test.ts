import { describe, it, expect, vi, beforeEach } from 'vitest';
import * as transport from '$lib/api/transport';
import {
  startCueList,
  stopCue,
  pauseCue,
  resumeCue,
  nextCue,
  previousCue,
  jumpCue,
  restartCueList,
  setCueCrossfade,
} from '$lib/api/showTransport';
import { saveCueList, CueListConflictError } from '$lib/api/cues';
import { createDefaultCueList } from './document';

describe('cue transport + save workflow (LG-034 §82 H..N, §14, §37)', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('H/I. Save persists the draft with expectedChecksum via PUT', async () => {
    const spy = vi
      .spyOn(transport, 'apiJson')
      .mockResolvedValue({ saved: true, checksum: 987654, commandId: 1 });

    const doc = createDefaultCueList(2, 'Show');
    const res = await saveCueList('192.168.1.10', doc, 123456);

    expect(res.saved).toBe(true);
    expect(res.checksum).toBe(987654);
    expect(spy).toHaveBeenCalledWith(
      '192.168.1.10',
      '/api/v1/cue-list?index=2',
      expect.objectContaining({
        method: 'PUT',
        body: expect.any(String),
      })
    );
    const body = JSON.parse((spy.mock.calls[0][2] as RequestInit).body as string);
    expect(body.expectedChecksum).toBe(123456);
    expect(body.endAction).toBe('hold_last');
    expect(body.endActionValue).toBe(0);
    expect(body.mode).toBe('single_shot');
  });

  it('RUN uses the canonical /api/v1/show/control path', async () => {
    const spy = vi
      .spyOn(transport, 'apiJson')
      .mockResolvedValue({ accepted: true, mode: 'cue', generation: 5, commandId: 2 });

    await startCueList('192.168.1.10', 2, 0);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'start', cueListIndex: 2, cueIndex: 0 }),
    });
  });

  it('K. restart uses the canonical restart action', async () => {
    const spy = vi
      .spyOn(transport, 'apiJson')
      .mockResolvedValue({ accepted: true, mode: 'cue', generation: 6, commandId: 3 });

    await restartCueList('192.168.1.10', 2, 0);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'restart', cueListIndex: 2, cueIndex: 0 }),
    });
  });

  it('transport helpers all target /api/v1/show/control', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true });

    await stopCue('n');
    await pauseCue('n');
    await resumeCue('n');
    await nextCue('n');
    await previousCue('n');
    await jumpCue('n', 3);
    await setCueCrossfade('n', 0.5);

    for (const call of spy.mock.calls) {
      expect(call[1]).toBe('/api/v1/show/control');
    }
    const bodies = spy.mock.calls.map((c) => JSON.parse((c[2] as RequestInit).body as string));
    expect(bodies[0]).toEqual({ mode: 'cue', action: 'stop' });
    expect(bodies[1]).toEqual({ mode: 'cue', action: 'pause' });
    expect(bodies[2]).toEqual({ mode: 'cue', action: 'resume' });
    expect(bodies[3]).toEqual({ mode: 'cue', action: 'next' });
    expect(bodies[4]).toEqual({ mode: 'cue', action: 'previous' });
    expect(bodies[5]).toEqual({ mode: 'cue', action: 'jump', cueIndex: 3 });
    expect(bodies[6]).toEqual({ mode: 'cue', action: 'crossfade', crossfade: 0.5 });
  });

  it('crossfade sends strict 0..1 values', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true });
    await setCueCrossfade('n', 0.73);
    const body = JSON.parse((spy.mock.calls[0][2] as RequestInit).body as string);
    expect(body.crossfade).toBe(0.73);
  });

  it('N. checksum conflict raises CueListConflictError', async () => {
    vi.spyOn(transport, 'apiJson').mockImplementation((() => {
      const err = new transport.ApiError(
        409,
        '/api/v1/cue-list?index=2',
        '192.168.1.10',
        'Conflict: cue_list_conflict'
      );
      return Promise.reject(err);
    }) as never);

    const doc = createDefaultCueList(2, 'Show');
    await expect(saveCueList('192.168.1.10', doc, 123456)).rejects.toThrow(CueListConflictError);
  });

  it('rejected transport ops surface errors (no silent acceptance)', async () => {
    vi.spyOn(transport, 'apiJson').mockRejectedValue(
      new transport.ApiError(409, '/api/v1/show/control', 'n', 'Conflict: pause_invalid_state')
    );
    await expect(pauseCue('192.168.1.10')).rejects.toThrow(/409/);
  });
});
