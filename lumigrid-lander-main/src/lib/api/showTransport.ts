import { apiJson } from './transport';
import type { CueStatus } from './cues';

export type ShowTransportMode = 'scene' | 'timeline' | 'cue';

export interface ShowTransportStatus {
  mode: ShowTransportMode;
  generation: number;

  scene: {
    index: number;
  };

  timeline: {
    running: boolean;
    bpm: number;
  };

  cue: CueStatus;
}

export interface ShowControlResponse {
  accepted: boolean;
  mode: ShowTransportMode;
  generation?: number;
  commandId?: number;
  error?: string;
  leader?: string;
}

/** Fetches unified show transport runtime status from a node. */
export async function getShowTransportStatus(ip: string): Promise<ShowTransportStatus> {
  return apiJson<ShowTransportStatus>(ip, '/api/v1/show/status');
}

/** Activates a stored scene via Show Transport coordinator. */
export async function runScene(ip: string, sceneIndex: number): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'scene', sceneIndex }),
  });
}

/** Starts Timeline transport with optional BPM. */
export async function startTimeline(ip: string, bpm?: number): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'timeline', action: 'start', ...(bpm ? { bpm } : {}) }),
  });
}

/** Stops Timeline transport, returning to current held Scene transport. */
export async function stopTimeline(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'timeline', action: 'stop' }),
  });
}

/** Starts Cue List transport from a specified list and optional cue index. */
export async function startCueList(
  ip: string,
  cueListIndex: number,
  cueIndex?: number
): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({
      mode: 'cue',
      action: 'start',
      cueListIndex,
      ...(cueIndex !== undefined ? { cueIndex } : {}),
    }),
  });
}

/** Stops Cue transport, returning coordinator to Scene mode. */
export async function stopCue(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'stop' }),
  });
}

/** Pauses Cue transport. */
export async function pauseCue(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'pause' }),
  });
}

/** Resumes Cue transport. */
export async function resumeCue(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'resume' }),
  });
}

/** Advances to next cue. */
export async function nextCue(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'next' }),
  });
}

/** Steps to previous cue. */
export async function previousCue(ip: string): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'previous' }),
  });
}

/** Jumps directly to a cue index. */
export async function jumpCue(ip: string, cueIndex: number): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'jump', cueIndex }),
  });
}

/** Updates manual crossfade position for manual cue lists (Q15: 0..32768 or float 0..1). */
export async function setCueCrossfade(ip: string, crossfade: number): Promise<ShowControlResponse> {
  return apiJson<ShowControlResponse>(ip, '/api/v1/show/control', {
    method: 'POST',
    body: JSON.stringify({ mode: 'cue', action: 'crossfade', crossfade }),
  });
}
