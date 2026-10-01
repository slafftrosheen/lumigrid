/**
 * LG-034 Cue editor store (§64).
 *
 * Owns authoring state for the focused node's Cue List: the persisted node
 * version and the browser working draft. Runtime playback state deliberately
 * lives in nodeStore.snapshot — never here.
 */
import { get, writable } from 'svelte/store';
import {
  createDefaultCueList,
  cueListsEqual,
  duplicateCueList,
  type CueListDocument,
} from './document';

export interface CueConflict {
  /** Node-side checksum that beat our save. */
  currentChecksum: number;
}

export interface CueEditorState {
  nodeId: string | null;
  listIndex: number | null;

  persisted: CueListDocument | null;
  working: CueListDocument | null;

  selectedCue: number | null;

  saving: boolean;
  conflict: CueConflict | null;
  error: string | null;
}

const initial: CueEditorState = {
  nodeId: null,
  listIndex: null,
  persisted: null,
  working: null,
  selectedCue: null,
  saving: false,
  conflict: null,
  error: null,
};

const store = writable<CueEditorState>(initial);

/** True when the working draft differs from the persisted node version. */
export function isDirty(state: CueEditorState): boolean {
  if (!state.persisted || !state.working) return false;
  return !cueListsEqual(state.persisted, state.working);
}

/** Selector used by the UI: dirty flag derived from state. */
export function deriveDirty(state: CueEditorState): boolean {
  return isDirty(state);
}

export const cueEditorStore = {
  subscribe: store.subscribe,

  reset() {
    store.set(initial);
  },

  /** Bind the store to a node; switching nodes discards any other node's draft. */
  bindNode(nodeId: string) {
    store.update((s) => {
      if (s.nodeId === nodeId) return s;
      return { ...initial, nodeId };
    });
  },

  /** Load a persisted node version as both persisted and working baseline. */
  load(doc: CueListDocument) {
    store.update((s) => ({
      ...s,
      listIndex: doc.index,
      persisted: doc,
      working: cloneDoc(doc),
      selectedCue: null,
      conflict: null,
      error: null,
    }));
  },

  /** Browser-draft-only creation (§45): no node write until explicit save. */
  openDraft(doc: CueListDocument) {
    store.update((s) => ({
      ...s,
      listIndex: doc.index,
      persisted: null,
      working: doc,
      selectedCue: null,
      conflict: null,
      error: null,
    }));
  },

  updateWorking(mutator: (doc: CueListDocument) => CueListDocument) {
    store.update((s) => {
      if (!s.working) return s;
      return { ...s, working: mutator(cloneDoc(s.working)), conflict: null };
    });
  },

  selectCue(index: number | null) {
    store.update((s) => ({ ...s, selectedCue: index }));
  },

  setSaving(saving: boolean) {
    store.update((s) => ({ ...s, saving }));
  },

  setError(error: string | null) {
    store.update((s) => ({ ...s, error }));
  },

  /** 409 cue_list_conflict handler (§37). */
  setConflict(currentChecksum: number) {
    store.update((s) => ({ ...s, conflict: { currentChecksum } }));
  },

  clearConflict() {
    store.update((s) => ({ ...s, conflict: null }));
  },

  /** Authoritative save success: persisted becomes the saved document (§38). */
  commitSave(saved: CueListDocument) {
    store.update((s) => ({
      ...s,
      persisted: cloneDoc(saved),
      working: cloneDoc(saved),
      conflict: null,
      error: null,
    }));
  },

  /** Keep my draft after a conflict, but record the node's checksum. */
  keepDraft() {
    store.update((s) => ({ ...s, conflict: null }));
  },

  /** Reload the node version, discarding the browser draft. */
  reloadNodeVersion(doc: CueListDocument) {
    store.update((s) => ({
      ...s,
      listIndex: doc.index,
      persisted: cloneDoc(doc),
      working: cloneDoc(doc),
      conflict: null,
      error: null,
    }));
  },
};

export function duplicateIntoFreeSlot(
  source: CueListDocument,
  usedIndices: number[]
): CueListDocument | null {
  const used = new Set(usedIndices);
  for (let i = 0; i < 8; i++) {
    if (!used.has(i)) {
      return duplicateCueList(source, i);
    }
  }
  return null;
}

function cloneDoc(doc: CueListDocument): CueListDocument {
  return { ...doc, cues: doc.cues.map((cue) => ({ ...cue })) };
}

export function getCurrentCueEditorState(): CueEditorState {
  return get(store);
}
