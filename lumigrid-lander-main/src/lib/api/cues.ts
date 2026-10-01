/**
 * Cue List persistence API (LG-034 schema 2).
 *
 * NOTE (§85): this module is the designated compatibility surface. It keeps
 * `/api/v1/cue-list` document persistence (which has no canonical replacement)
 * and legacy `/api/v1/cue/status` + `/api/v1/cue/control` helpers. Normal
 * dashboard routes must use `./showTransport` for transport control and the
 * RuntimeSnapshot via nodeStore for telemetry.
 */
import { apiJson, ApiError } from './transport';
import type { TransitionCurve } from '../runtime/curves';
import {
  cueEndActionFromNumeric,
  toPutBody,
  type CueEndAction,
  type CueListDocument,
} from '../cues/document';

export type CueRunMode =
  | 'single_shot'
  | 'loop'
  | 'ping_pong'
  | 'random'
  | 'manual';

export type { CueEndAction } from '../cues/document';

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
  /** Numeric backward-compat form. */
  endAction: number;
  endActionName?: CueEndAction;
  /** Present since LG-034 schema 2. */
  checksum?: number;
}

/** Server document (schema 2 since LG-034; schema-less from older firmware). */
export interface CueList {
  index: number;
  name: string;
  mode: CueRunMode;
  count?: number;
  endAction: number;
  endActionName?: CueEndAction;
  checksum?: number;
  cues: CueEntry[];
}

/** Map a server document to the schema-2 editor document. */
export function toDocument(list: CueList): CueListDocument {
  return {
    schema: 2,
    index: list.index,
    checksum: list.checksum ?? null,
    name: list.name,
    mode: list.mode,
    endAction: list.endActionName ?? cueEndActionFromNumeric(list.endAction),
    cues: list.cues.map((c) => ({ ...c })),
  };
}

export interface SaveResult {
  saved: boolean;
  /** Authoritative node checksum after save (§38). */
  checksum?: number;
  commandId?: number;
}

export class CueListConflictError extends Error {
  constructor(public currentChecksum: number) {
    super(`Cue list changed on the node (checksum ${currentChecksum})`);
    this.name = 'CueListConflictError';
  }
}

/** Transport-independent conflict detection for save flows (§37). */
export function isConflictError(err: unknown): number | null {
  if (err instanceof CueListConflictError) return err.currentChecksum;
  if (err instanceof ApiError && err.status === 409 && err.serverMessage.includes('cue_list_conflict')) {
    // Server body: {"error":"cue_list_conflict","currentChecksum":N}.
    // apiJson keeps only the flattened message; the checksum is parsed when
    // present, otherwise the UI reloads the node version to obtain it.
    const match = err.serverMessage.match(/currentChecksum[":\s]+(\d+)/i);
    return match ? Number(match[1]) : 0;
  }
  return null;
}

export function getCueLists(ip: string): Promise<CueListSummary[]> {
  return apiJson<CueListSummary[]>(ip, '/api/v1/cue-lists');
}

export function getCueList(ip: string, index: number): Promise<CueList> {
  return apiJson<CueList>(ip, `/api/v1/cue-list?index=${index}`);
}

export function saveCueList(
  ip: string,
  doc: CueListDocument,
  expectedChecksum?: number | null
): Promise<SaveResult> {
  const body = toPutBody(doc, expectedChecksum);
  return apiJson<SaveResult>(ip, `/api/v1/cue-list?index=${doc.index}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }).catch((err: unknown) => {
    const conflictChecksum = isConflictError(err);
    if (conflictChecksum !== null) {
      throw new CueListConflictError(conflictChecksum);
    }
    throw err;
  });
}

export function deleteCueList(ip: string, index: number): Promise<{ deleted: boolean }> {
  return apiJson<{ deleted: boolean }>(ip, `/api/v1/cue-list?index=${index}`, {
    method: 'DELETE',
  });
}

// ── Deprecated transport helpers (compat only, §85) ──────────────────────────

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
  pausedFrom?: string;
  listIndex?: number;
  cueIndex?: number;
  nextCueIndex?: number;
  mode?: CueRunMode | string;
  elapsedMs?: number;
  remainingMs?: number;
  manualFade?: number;
  activeChecksum?: number;
  savedChecksum?: number;
  contentStale?: boolean;
  remoteDriven?: boolean;
}

/** Deprecated: use startCueList()/stopCue() from showTransport.ts instead. */
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

/** Deprecated: consume transport.cue from the RuntimeSnapshot instead. */
export function getCueStatus(ip: string): Promise<CueStatus> {
  return apiJson<CueStatus>(ip, '/api/v1/cue/status');
}
