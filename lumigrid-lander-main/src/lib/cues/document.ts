/**
 * LG-034 Cue List document model (schema 2).
 *
 * The document is the browser-side authoring model. Checksums are the node's
 * authority: they are surfaced on documents but never count toward dirty
 * comparison, and never leave the browser inside exports.
 */
import type { TransitionCurve } from '../runtime/curves';

export type CueRunMode =
  | 'single_shot'
  | 'loop'
  | 'ping_pong'
  | 'random'
  | 'manual';

export type CueEndAction = 'hold_last' | 'blackout' | 'restore_fallback';

/** Firmware limits (SignageTypes.hpp): char[32] and char[24] incl. NUL. */
export const MAX_CUE_LIST_NAME_BYTES = 31;
export const MAX_CUE_LABEL_BYTES = 23;
export const MAX_CUES_PER_LIST = 16;
export const MAX_CUE_LISTS = 8;
export const MAX_HOLD_MS = 3_600_000;
export const MAX_FADE_MS = 60_000;

/** Numeric wire-compatibility form of CueEndAction (firmware enum values). */
export const CUE_END_ACTION_NUMERIC: Record<CueEndAction, 0 | 1 | 2> = {
  hold_last: 0,
  blackout: 1,
  restore_fallback: 2,
};

export function cueEndActionFromNumeric(value: number): CueEndAction {
  switch (value) {
    case 1: return 'blackout';
    case 2: return 'restore_fallback';
    case 0:
    default: return 'hold_last';
  }
}

export interface CueEntry {
  sceneIndex: number;
  holdMs: number;
  fadeMs: number;
  curve: TransitionCurve;
  label: string;
}

export interface CueListDocument {
  schema: 2;
  index: number;
  /** Node-owned stored checksum. Excluded from dirty comparison. */
  checksum: number | null;
  name: string;
  mode: CueRunMode;
  endAction: CueEndAction;
  cues: CueEntry[];
}

/** UTF-8 byte length (§35 — not .length, which counts UTF-16 code units). */
export function utf8ByteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

export function createDefaultCue(sceneIndex = 0): CueEntry {
  return {
    sceneIndex,
    holdMs: 3000,
    fadeMs: 1000,
    curve: 'easeInOut',
    label: '',
  };
}

export function createDefaultCueList(index: number, name: string, firstScene: number | null = null): CueListDocument {
  return {
    schema: 2,
    index,
    checksum: null,
    name,
    mode: 'single_shot',
    endAction: 'hold_last',
    cues: [createDefaultCue(firstScene ?? 0)],
  };
}

/** Duplicate a document into a free slot with a sensible name suffix (§46). */
export function duplicateCueList(source: CueListDocument, freeIndex: number): CueListDocument {
  const baseName = source.name.length > 0 ? source.name : `Cue List ${source.index + 1}`;
  let name = `${baseName} copy`;
  if (utf8ByteLength(name) > MAX_CUE_LIST_NAME_BYTES) {
    name = name.slice(0, Math.max(1, MAX_CUE_LIST_NAME_BYTES - 5)) + ' copy';
  }
  return {
    schema: 2,
    index: freeIndex,
    checksum: null,
    name,
    mode: source.mode,
    endAction: source.endAction,
    cues: source.cues.map((cue) => ({ ...cue })),
  };
}

/**
 * Dirty comparison (§66): structural equality ignoring the document checksum.
 * Order-sensitive (cue order is show semantics).
 */
export function cueListsEqual(a: CueListDocument, b: CueListDocument): boolean {
  if (a.name !== b.name) return false;
  if (a.mode !== b.mode) return false;
  if (a.endAction !== b.endAction) return false;
  if (a.cues.length !== b.cues.length) return false;
  for (let i = 0; i < a.cues.length; i++) {
    const ca = a.cues[i];
    const cb = b.cues[i];
    if (
      ca.sceneIndex !== cb.sceneIndex ||
      ca.holdMs !== cb.holdMs ||
      ca.fadeMs !== cb.fadeMs ||
      ca.curve !== cb.curve ||
      ca.label !== cb.label
    ) {
      return false;
    }
  }
  return true;
}

export interface CueListIssues {
  nameTooLong: boolean;
  nameEmpty: boolean;
  tooManyCues: boolean;
  noCues: boolean;
  /** cue index -> referenced scene does not exist on the node */
  missingScenes: Array<{ cueIndex: number; sceneIndex: number }>;
  overlongLabels: number[];
  invalidTiming: Array<{ cueIndex: number; field: 'holdMs' | 'fadeMs' }>;
}

/** Fail-closed client-side validation mirroring firmware PUT validation (§32/§33/§35/§68). */
export function validateCueList(
  doc: CueListDocument,
  sceneExists: (sceneIndex: number) => boolean
): CueListIssues {
  const issues: CueListIssues = {
    nameTooLong: utf8ByteLength(doc.name) > MAX_CUE_LIST_NAME_BYTES,
    nameEmpty: doc.name.length === 0,
    tooManyCues: doc.cues.length > MAX_CUES_PER_LIST,
    noCues: doc.cues.length === 0,
    missingScenes: [],
    overlongLabels: [],
    invalidTiming: [],
  };

  doc.cues.forEach((cue, cueIndex) => {
    if (!sceneExists(cue.sceneIndex)) {
      issues.missingScenes.push({ cueIndex, sceneIndex: cue.sceneIndex });
    }
    if (utf8ByteLength(cue.label) > MAX_CUE_LABEL_BYTES) {
      issues.overlongLabels.push(cueIndex);
    }
    if (!Number.isFinite(cue.holdMs) || cue.holdMs < 0 || cue.holdMs > MAX_HOLD_MS) {
      issues.invalidTiming.push({ cueIndex, field: 'holdMs' });
    }
    if (!Number.isFinite(cue.fadeMs) || cue.fadeMs < 0 || cue.fadeMs > MAX_FADE_MS) {
      issues.invalidTiming.push({ cueIndex, field: 'fadeMs' });
    }
  });

  return issues;
}

export function hasBlockingIssues(issues: CueListIssues): boolean {
  return (
    issues.nameTooLong ||
    issues.nameEmpty ||
    issues.tooManyCues ||
    issues.noCues ||
    issues.missingScenes.length > 0 ||
    issues.overlongLabels.length > 0 ||
    issues.invalidTiming.length > 0
  );
}

/** Nominal total duration for automatic modes (§44/§48). */
export function cueListNominalDurationMs(doc: CueListDocument): number {
  return doc.cues.reduce((total, cue) => total + cue.holdMs + cue.fadeMs, 0);
}

export function formatDuration(ms: number): string {
  if (!Number.isFinite(ms) || ms <= 0) return '0 s';
  const seconds = ms / 1000;
  if (seconds < 60) {
    return `${seconds % 1 === 0 ? seconds.toFixed(0) : seconds.toFixed(1)} s`;
  }
  const minutes = Math.floor(seconds / 60);
  const remainder = Math.round(seconds % 60);
  return `${minutes}m ${remainder}s`;
}

// ── Wire serialization (API boundary) ────────────────────────────────────────

/** PUT /api/v1/cue-list body. Sends strict string enums + numeric compatibility. */
export interface CueListPutBody {
  index: number;
  name: string;
  mode: CueRunMode;
  endAction: CueEndAction;
  /** Numeric alias kept for backward compatibility with older firmware. */
  endActionValue: 0 | 1 | 2;
  cues: CueEntry[];
  /** Optimistic conflict token: only save if this still matches the node. */
  expectedChecksum?: number;
}

export function toPutBody(doc: CueListDocument, expectedChecksum?: number | null): CueListPutBody {
  const body: CueListPutBody = {
    index: doc.index,
    name: doc.name,
    mode: doc.mode,
    endAction: doc.endAction,
    endActionValue: CUE_END_ACTION_NUMERIC[doc.endAction],
    cues: doc.cues.map((cue) => ({ ...cue })),
  };
  if (expectedChecksum != null && expectedChecksum > 0) {
    body.expectedChecksum = expectedChecksum;
  }
  return body;
}

// ── Import / export (§67) ────────────────────────────────────────────────────

export interface CueListExport {
  schema: 2;
  type: 'lumigrid-cue-list';
  name: string;
  mode: CueRunMode;
  endAction: CueEndAction;
  cues: CueEntry[];
}

export function exportCueList(doc: CueListDocument): CueListExport {
  // Runtime state, checksums, node identity, auth and network details never
  // leave the browser inside an export.
  return {
    schema: 2,
    type: 'lumigrid-cue-list',
    name: doc.name,
    mode: doc.mode,
    endAction: doc.endAction,
    cues: doc.cues.map((cue) => ({ ...cue })),
  };
}

export type ImportResult =
  | { ok: true; doc: CueListDocument }
  | { ok: false; error: string };

export function importCueList(json: unknown, targetIndex: number): ImportResult {
  if (typeof json !== 'object' || json === null) {
    return { ok: false, error: 'Not a JSON object' };
  }
  const raw = json as Record<string, unknown>;
  if (raw.type !== undefined && raw.type !== 'lumigrid-cue-list') {
    return { ok: false, error: 'Not a LumiGrid cue list export' };
  }
  if (typeof raw.name !== 'string' || raw.name.length === 0) {
    return { ok: false, error: 'Missing name' };
  }
  if (utf8ByteLength(raw.name) > MAX_CUE_LIST_NAME_BYTES) {
    return { ok: false, error: `Name exceeds ${MAX_CUE_LIST_NAME_BYTES} UTF-8 bytes` };
  }
  const mode = raw.mode;
  if (mode !== 'single_shot' && mode !== 'loop' && mode !== 'ping_pong' && mode !== 'random' && mode !== 'manual') {
    return { ok: false, error: 'Invalid run mode' };
  }
  const endActionRaw = raw.endAction;
  const endAction =
    endActionRaw === 'blackout' || endActionRaw === 'restore_fallback' || endActionRaw === 'hold_last'
      ? endActionRaw
      : typeof endActionRaw === 'number'
        ? cueEndActionFromNumeric(endActionRaw)
        : null;
  if (!endAction) {
    return { ok: false, error: 'Invalid end action' };
  }
  if (!Array.isArray(raw.cues) || raw.cues.length === 0) {
    return { ok: false, error: 'Cue list must contain at least one cue' };
  }
  if (raw.cues.length > MAX_CUES_PER_LIST) {
    return { ok: false, error: `Maximum ${MAX_CUES_PER_LIST} cues per list` };
  }

  const cues: CueEntry[] = [];
  for (const item of raw.cues) {
    if (typeof item !== 'object' || item === null) {
      return { ok: false, error: 'Invalid cue entry' };
    }
    const c = item as Record<string, unknown>;
    const sceneIndex = c.sceneIndex;
    const holdMs = c.holdMs;
    const fadeMs = c.fadeMs;
    if (typeof sceneIndex !== 'number' || !Number.isInteger(sceneIndex) || sceneIndex < 0 || sceneIndex > 31) {
      return { ok: false, error: 'Cue scene index must be an integer 0..31' };
    }
    if (typeof holdMs !== 'number' || holdMs < 0 || holdMs > MAX_HOLD_MS) {
      return { ok: false, error: 'Cue holdMs must be between 0 and 3600000' };
    }
    if (typeof fadeMs !== 'number' || fadeMs < 0 || fadeMs > MAX_FADE_MS) {
      return { ok: false, error: 'Cue fadeMs must be between 0 and 60000' };
    }
    cues.push({
      sceneIndex,
      holdMs,
      fadeMs,
      curve: (typeof c.curve === 'string' ? c.curve : 'easeInOut') as TransitionCurve,
      label: typeof c.label === 'string' ? c.label : '',
    });
  }

  return {
    ok: true,
    doc: {
      schema: 2,
      index: targetIndex,
      checksum: null,
      name: raw.name,
      mode,
      endAction,
      cues,
    },
  };
}

export function downloadJson(filename: string, data: CueListExport): void {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}
