/**
 * LG-034 Cue clipboard + reorder helpers (§51–53).
 *
 * Editor-local clipboard: no persistence, no global state beyond the store.
 */

import type { CueEntry } from './document';

export interface CueClipboard {
  cue: CueEntry;
}

let clipboard: CueClipboard | null = null;

export function copyCue(cue: CueEntry): void {
  clipboard = { cue: { ...cue } };
}

export function peekClipboard(): CueEntry | null {
  return clipboard ? { ...clipboard.cue } : null;
}

export function clearClipboard(): void {
  clipboard = null;
}

/** Paste after the given index; returns the new cues array or null when empty. */
export function pasteAfter(cues: CueEntry[], afterIndex: number, maxCount: number): CueEntry[] | null {
  if (!clipboard) return null;
  if (cues.length >= maxCount) return null;
  const next = [...cues];
  next.splice(afterIndex + 1, 0, { ...clipboard.cue });
  return next;
}

/** Reorder with bounds checking; returns null when the move is a no-op. */
export function moveCue(cues: CueEntry[], from: number, to: number): CueEntry[] | null {
  if (to < 0 || to >= cues.length) return null;
  if (from === to) return null;
  const next = [...cues];
  const [item] = next.splice(from, 1);
  next.splice(to, 0, item);
  return next;
}
