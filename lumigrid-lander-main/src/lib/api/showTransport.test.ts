import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  getShowTransportStatus,
  runScene,
  startTimeline,
  stopTimeline,
  startCueList,
  stopCue,
  pauseCue,
  resumeCue,
  nextCue,
  previousCue,
  jumpCue,
  setCueCrossfade,
} from './showTransport';
import * as transport from './transport';

describe('showTransport API', () => {
  beforeEach(() => {
    vi.restoreAllMocks();
  });

  it('fetches show transport status', async () => {
    const mockStatus = {
      mode: 'cue',
      generation: 54,
      scene: { index: 7 },
      timeline: { running: false, bpm: 120 },
      cue: {
        running: true,
        state: 'holding',
        listIndex: 2,
        cueIndex: 4,
        nextCueIndex: 5,
        elapsedMs: 1240,
        remainingMs: 1760,
        manualFade: 0,
        generation: 19,
      },
    };
    vi.spyOn(transport, 'apiJson').mockResolvedValue(mockStatus as any);

    const res = await getShowTransportStatus('192.168.1.10');
    expect(res.mode).toBe('cue');
    expect(res.generation).toBe(54);
    expect(res.cue.cueIndex).toBe(4);
  });

  it('sends runScene control command', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'scene', generation: 41 });

    const res = await runScene('192.168.1.10', 7);
    expect(res.accepted).toBe(true);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'scene', sceneIndex: 7 }),
    });
  });

  it('sends startTimeline control command', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'timeline', generation: 42 });

    const res = await startTimeline('192.168.1.10', 128);
    expect(res.accepted).toBe(true);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'timeline', action: 'start', bpm: 128 }),
    });
  });

  it('sends stopTimeline control command', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'timeline', generation: 43 });

    await stopTimeline('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'timeline', action: 'stop' }),
    });
  });

  it('sends startCueList control command', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'cue', generation: 44 });

    await startCueList('192.168.1.10', 2, 0);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'start', cueListIndex: 2, cueIndex: 0 }),
    });
  });

  it('sends stopCue and pauseCue commands', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'cue', generation: 45 });

    await stopCue('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'stop' }),
    });

    await pauseCue('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'pause' }),
    });
  });

  it('sends resumeCue, nextCue, previousCue, jumpCue, and setCueCrossfade commands', async () => {
    const spy = vi.spyOn(transport, 'apiJson').mockResolvedValue({ accepted: true, mode: 'cue', generation: 46 });

    await resumeCue('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'resume' }),
    });

    await nextCue('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'next' }),
    });

    await previousCue('192.168.1.10');
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'previous' }),
    });

    await jumpCue('192.168.1.10', 3);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'jump', cueIndex: 3 }),
    });

    await setCueCrossfade('192.168.1.10', 16384);
    expect(spy).toHaveBeenCalledWith('192.168.1.10', '/api/v1/show/control', {
      method: 'POST',
      body: JSON.stringify({ mode: 'cue', action: 'crossfade', crossfade: 16384 }),
    });
  });
});
