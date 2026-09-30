import { writable, derived, get } from 'svelte/store';
import type { TimelineDocument, TimelineTrack, TimelineStep } from '../api/timeline';
import {
  getSequence,
  saveSequence,
  createDefaultTimelineDocument,
  cloneTimelineDocument,
  isTimelineDirty,
  normalizeTimelineDocument,
} from '../api/timeline';
import {
  addStep,
  updateStepLength,
  duplicateStep,
  deleteStep,
  moveStepLeft,
  moveStepRight,
  reorderStep,
  toggleTrackEnabled,
  clearTrack as clearTrackDoc,
  updateTrackInDocument,
  setDocumentBpm,
} from './document';
import {
  copyStep,
  pasteStepAfter,
  copyTrackPattern,
  pasteTrackPattern,
} from './clipboard';

export type TimelineZoomLevel = 'compact' | 'normal' | 'wide';

export const ZOOM_PIXELS_PER_BEAT: Record<TimelineZoomLevel, number> = {
  compact: 20,
  normal: 32,
  wide: 48,
};

export interface TimelineEditorState {
  nodeId: string | null;
  nodeIp: string | null;
  loadGeneration: number;

  persisted: TimelineDocument | null;
  working: TimelineDocument | null;

  selectedChannel: number | null;
  selectedStep: number | null;

  zoom: TimelineZoomLevel;

  saving: boolean;
  conflict: boolean;
  conflictChecksum: number | null;

  error: string | null;
}

const initialState: TimelineEditorState = {
  nodeId: null,
  nodeIp: null,
  loadGeneration: 0,
  persisted: null,
  working: null,
  selectedChannel: null,
  selectedStep: null,
  zoom: 'normal',
  saving: false,
  conflict: false,
  conflictChecksum: null,
  error: null,
};

function createTimelineEditorStore() {
  const { subscribe, set, update } = writable<TimelineEditorState>(initialState);

  let currentLoadGen = 0;

  return {
    subscribe,

    async load(nodeId: string, nodeIp: string) {
      const gen = ++currentLoadGen;
      update((s) => ({
        ...s,
        nodeId,
        nodeIp,
        loadGeneration: gen,
        saving: false,
        conflict: false,
        conflictChecksum: null,
        error: null,
      }));

      try {
        const raw = await getSequence(nodeIp);
        // Node-change race guard: ensure response matches active loadGeneration
        if (currentLoadGen !== gen) return;

        const doc = normalizeTimelineDocument(raw);
        update((s) => ({
          ...s,
          persisted: cloneTimelineDocument(doc),
          working: cloneTimelineDocument(doc),
          selectedChannel: null,
          selectedStep: null,
          error: null,
        }));
      } catch (err: any) {
        if (currentLoadGen !== gen) return;
        update((s) => ({
          ...s,
          error: err?.message || 'Failed to load timeline sequence',
        }));
      }
    },

    setZoom(zoom: TimelineZoomLevel) {
      update((s) => ({ ...s, zoom }));
    },

    selectStep(channelId: number | null, stepIndex: number | null) {
      update((s) => ({ ...s, selectedChannel: channelId, selectedStep: stepIndex }));
    },

    setBpm(bpm: number) {
      update((s) => {
        if (!s.working) return s;
        return {
          ...s,
          working: setDocumentBpm(s.working, bpm),
        };
      });
    },

    addBlock(channelId: number, presetIndex: number, lengthBeats = 4) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = addStep(track, presetIndex, lengthBeats);
        const working = updateTrackInDocument(s.working, updatedTrack);
        return {
          ...s,
          working,
          selectedChannel: channelId,
          selectedStep: updatedTrack.steps.length - 1,
        };
      });
    },

    resizeBlock(channelId: number, stepIndex: number, lengthBeats: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = updateStepLength(track, stepIndex, lengthBeats);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
        };
      });
    },

    reorderBlock(channelId: number, fromIndex: number, toIndex: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = reorderStep(track, fromIndex, toIndex);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: channelId,
          selectedStep: toIndex,
        };
      });
    },

    moveBlockLeft(channelId: number, stepIndex: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = moveStepLeft(track, stepIndex);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: channelId,
          selectedStep: Math.max(0, stepIndex - 1),
        };
      });
    },

    moveBlockRight(channelId: number, stepIndex: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = moveStepRight(track, stepIndex);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: channelId,
          selectedStep: Math.min(track.steps.length - 1, stepIndex + 1),
        };
      });
    },

    duplicateBlock(channelId: number, stepIndex: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = duplicateStep(track, stepIndex);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: channelId,
          selectedStep: stepIndex + 1,
        };
      });
    },

    deleteBlock(channelId: number, stepIndex: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = deleteStep(track, stepIndex);
        const newSelectedStep = updatedTrack.steps.length === 0
          ? null
          : Math.min(stepIndex, updatedTrack.steps.length - 1);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: newSelectedStep !== null ? channelId : null,
          selectedStep: newSelectedStep,
        };
      });
    },

    toggleTrack(channelId: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = toggleTrackEnabled(track);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
        };
      });
    },

    clearTrack(channelId: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = clearTrackDoc(track);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: s.selectedChannel === channelId ? null : s.selectedChannel,
          selectedStep: s.selectedChannel === channelId ? null : s.selectedStep,
        };
      });
    },

    copyBlock(channelId: number, stepIndex: number) {
      const state = get({ subscribe });
      if (!state.working || channelId < 0 || channelId >= state.working.tracks.length) return;
      const step = state.working.tracks[channelId].steps[stepIndex];
      if (step) {
        copyStep(step);
      }
    },

    pasteBlock(channelId: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = pasteStepAfter(track, s.selectedChannel === channelId ? (s.selectedStep ?? undefined) : undefined);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
          selectedChannel: channelId,
          selectedStep: updatedTrack.steps.length - 1,
        };
      });
    },

    copyTrack(channelId: number) {
      const state = get({ subscribe });
      if (!state.working || channelId < 0 || channelId >= state.working.tracks.length) return;
      const track = state.working.tracks[channelId];
      if (track) {
        copyTrackPattern(track);
      }
    },

    pasteTrack(channelId: number) {
      update((s) => {
        if (!s.working || channelId < 0 || channelId >= s.working.tracks.length) return s;
        const track = s.working.tracks[channelId];
        const updatedTrack = pasteTrackPattern(track);
        return {
          ...s,
          working: updateTrackInDocument(s.working, updatedTrack),
        };
      });
    },

    async save(): Promise<boolean> {
      const state = get({ subscribe });
      if (!state.nodeIp || !state.working) return false;

      update((s) => ({ ...s, saving: true, error: null }));
      try {
        const expectedChecksum = state.persisted?.checksum ?? 0;
        const res = await saveSequence(state.nodeIp, state.working, expectedChecksum);

        // Success: update persisted checksum and state
        const updatedPersisted: TimelineDocument = {
          ...cloneTimelineDocument(state.working),
          checksum: res.checksum,
        };

        update((s) => ({
          ...s,
          persisted: cloneTimelineDocument(updatedPersisted),
          working: cloneTimelineDocument(updatedPersisted),
          saving: false,
          conflict: false,
          conflictChecksum: null,
          error: null,
        }));
        return true;
      } catch (err: any) {
        if (err?.status === 409 || err?.error === 'timeline_conflict') {
          update((s) => ({
            ...s,
            saving: false,
            conflict: true,
            conflictChecksum: err?.currentChecksum ?? null,
            error: 'Timeline was modified on the node by another session.',
          }));
        } else {
          update((s) => ({
            ...s,
            saving: false,
            error: err?.message || 'Failed to save timeline sequence',
          }));
        }
        return false;
      }
    },

    async reloadNodeVersion() {
      const state = get({ subscribe });
      if (!state.nodeIp) return;
      await this.load(state.nodeId || '', state.nodeIp);
    },

    keepDraft() {
      update((s) => ({ ...s, conflict: false }));
    },

    importDocument(imported: Partial<TimelineDocument>) {
      const doc = normalizeTimelineDocument(imported);
      update((s) => ({
        ...s,
        working: doc,
        selectedChannel: null,
        selectedStep: null,
        error: null,
      }));
    },

    reset() {
      set(initialState);
    },
  };
}

export const timelineEditor = createTimelineEditorStore();

export const isTimelineEditorDirty = derived(timelineEditor, ($s) => {
  return isTimelineDirty($s.working, $s.persisted);
});

export const timelinePixelsPerBeat = derived(timelineEditor, ($s) => {
  return ZOOM_PIXELS_PER_BEAT[$s.zoom] ?? 32;
});
