import { apiJson } from './transport';
import type { TransitionCurve } from '../runtime/curves';

export type CueRunMode =
  | 'single_shot'
  | 'loop'
  | 'ping_pong'
  | 'random'
  | 'manual';

export interface CueEntry {
  sceneIndex: number;
  holdMs: number;
  fadeMs: number;
  curve: TransitionCurve;
  label: string;
}

export interface CueListSummary {
  index: number;
  name: string;
  mode: CueRunMode;
  count: number;
  endAction: number;
}

export interface CueList {
  index: number;
  name: string;
  mode: CueRunMode;
  count?: number;
  endAction: number;
  cues: CueEntry[];
}

export type CueControlAction =
  | 'start'
  | 'stop'
  | 'pause'
  | 'resume'
  | 'next'
  | 'previous'
  | 'jump'
  | 'crossfade';

export interface CueControlRequest {
  action: CueControlAction;
  cueListIndex?: number;
  cueIndex?: number;
  crossfade?: number;
}

export interface CueStatus {
  running: boolean;
  state?: 'idle' | 'running' | 'fading' | 'holding' | 'paused' | string;
  listIndex?: number;
  cueIndex?: number;
  nextCueIndex?: number;
  mode?: CueRunMode | string;
  elapsedMs?: number;
  remainingMs?: number;
  manualFade?: number;
}

export function createDefaultCue(sceneIndex: number = 0): CueEntry {
  return {
    sceneIndex,
    holdMs: 3000,
    fadeMs: 1000,
    curve: 'easeInOut',
    label: `Cue ${sceneIndex + 1}`,
  };
}

export function createDefaultCueList(index: number = 0, name: string = `Cue List ${index + 1}`): CueList {
  return {
    index,
    name,
    mode: 'loop',
    endAction: 0,
    cues: [
      createDefaultCue(0),
      createDefaultCue(1),
    ],
  };
}

export function getCueLists(ip: string): Promise<CueListSummary[]> {
  return apiJson<CueListSummary[]>(ip, '/api/v1/cue-lists');
}

export function getCueList(ip: string, index: number): Promise<CueList> {
  return apiJson<CueList>(ip, `/api/v1/cue-list?index=${index}`);
}

export function saveCueList(
  ip: string,
  index: number,
  list: CueList
): Promise<{ saved: boolean; commandId?: number }> {
  return apiJson<{ saved: boolean; commandId?: number }>(
    ip,
    `/api/v1/cue-list?index=${index}`,
    {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ...list,
        index,
      }),
    }
  );
}

export function deleteCueList(ip: string, index: number): Promise<{ deleted: boolean }> {
  return apiJson<{ deleted: boolean }>(ip, `/api/v1/cue-list?index=${index}`, {
    method: 'DELETE',
  });
}

export function controlCue(
  ip: string,
  request: CueControlRequest
): Promise<{ accepted: boolean; commandId?: number }> {
  return apiJson<{ accepted: boolean; commandId?: number }>(
    ip,
    '/api/v1/cue/control',
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(request),
    }
  );
}

export function getCueStatus(ip: string): Promise<CueStatus> {
  return apiJson<CueStatus>(ip, '/api/v1/cue/status');
}
