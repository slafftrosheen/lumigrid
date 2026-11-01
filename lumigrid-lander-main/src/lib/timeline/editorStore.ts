import { writable, derived, get } from 'svelte/store';
import type { TimelineDocument } from '../api/timeline';
import {
  getSequence,
  saveSequence,
  cloneTimelineDocument,
  isTimelineDirty,
} from '../api/timeline';
import { captureNodeOperation } from '../state/nodeStore';
import { nodeAuthStore } from '../auth/nodeAuth';
import { parseTimelineDocument, validateTimelineDocument } from './validation';
import {
  addStep,
  updateStepLength,
  updateStepPreset,
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
  loading: boolean;
  loaded: boolean;
  sceneSlots: Set<number> | null;

  persisted: TimelineDocument | null;
  working: TimelineDocument | null;

  selectedChannel: number | null;
  selectedStep: number | null;

  zoom: TimelineZoomLevel;

  saving: boolean;
  conflict: boolean;
  conflictChecksum: number | null;
  remoteChanged: boolean;

  error: string | null;
}

const initialState: TimelineEditorState = {
  nodeId: null,
  nodeIp: null,
  loadGeneration: 0,
  loading: false,
  loaded: false,
  sceneSlots: null,
  persisted: null,
  working: null,
  selectedChannel: null,
  selectedStep: null,
  zoom: 'normal',
  saving: false,
  conflict: false,
  conflictChecksum: null,
  remoteChanged: false,
  error: null,
};

export function createTimelineEditorStore() {
  const { subscribe, set, update } = writable<TimelineEditorState>(initialState);

  let currentLoadGen = 0;
  let currentSaveGen = 0;
  let boundSession: ReturnType<typeof nodeAuthStore.getSession> = null;
  let owner: ReturnType<typeof captureNodeOperation> | null = null;

  return {
    subscribe,

    async load(nodeId: string, nodeIp: string, preserveDraft = false) {
      const operation = captureNodeOperation(nodeIp);
      if (!operation.isCurrent()) return false;
      const gen = ++currentLoadGen;
      ++currentSaveGen;
      const session = nodeAuthStore.getSession(nodeIp);
      const previous = get({ subscribe });
      const sameOwner = previous.nodeIp === nodeIp && boundSession === session;
      boundSession = session;
      owner = operation;
      const isCurrent = () => currentLoadGen === gen && operation.isCurrent();
      update((s) => ({
        ...(sameOwner ? s : { ...initialState, zoom: s.zoom }),
        nodeId,
        nodeIp,
        loadGeneration: gen,
        loading: true,
        loaded: false,
        saving: false,
        conflict: sameOwner && !preserveDraft ? previous.conflict : false,
        conflictChecksum: sameOwner && !preserveDraft ? previous.conflictChecksum : null,
        error: null,
      }));

      try {
        const raw = await getSequence(nodeIp);
        if (!isCurrent()) return false;
        const doc = parseTimelineDocument(raw, true);
        update((s) => {
          const keepDraft = sameOwner && preserveDraft && isTimelineDirty(s.working, s.persisted);
          const conflict = keepDraft && s.persisted?.checksum !== doc.checksum;
          return {
            ...s,
            persisted: conflict ? s.persisted : cloneTimelineDocument(doc),
            working: keepDraft ? s.working : cloneTimelineDocument(doc),
            selectedChannel: keepDraft ? s.selectedChannel : null,
            selectedStep: keepDraft ? s.selectedStep : null,
            loading: false,
            loaded: true,
            conflict,
            conflictChecksum: conflict ? doc.checksum : null,
            remoteChanged: conflict,
            error: conflict ? 'The saved timeline changed while this draft was open.' : null,
          };
        });
        return true;
      } catch (err: any) {
        if (!isCurrent()) return false;
        update((s) => ({
          ...s,
          loading: false,
          loaded: false,
          error: err?.message || 'Failed to load timeline sequence',
        }));
        return false;
      }
    },

    setSceneSlots(slots: Set<number> | null) {
      update((s) => ({ ...s, sceneSlots: slots ? new Set(slots) : null }));
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

    replaceBlockScene(channelId: number, stepIndex: number, presetIndex: number) {
      update((s) => {
        if (!s.working || !s.sceneSlots?.has(presetIndex)) return s;
        const track = s.working.tracks[channelId];
        if (!track) return s;
        return {
          ...s,
          working: updateTrackInDocument(s.working, updateStepPreset(track, stepIndex, presetIndex)),
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
      if (!state.nodeIp || !state.working || !state.persisted || !state.loaded ||
          state.loading || state.saving || !owner?.isCurrent()) return false;
      if (!state.sceneSlots) {
        update((s) => ({ ...s, error: 'Load the scene library before saving this timeline.' }));
        return false;
      }
      const validation = validateTimelineDocument(state.working, state.sceneSlots);
      if (!validation.valid) {
        update((s) => ({ ...s, error: validation.errors[0] || 'Resolve missing scene references before saving.' }));
        return false;
      }
      const operation = owner;
      const gen = ++currentSaveGen;
      const loadGen = currentLoadGen;
      const isCurrent = () => gen === currentSaveGen && loadGen === currentLoadGen && operation.isCurrent();
      const submitted = cloneTimelineDocument(state.working);

      update((s) => ({ ...s, saving: true, error: null }));
      try {
        const res = await saveSequence(state.nodeIp, submitted, state.persisted.checksum);
        if (!isCurrent()) return false;
        if (res.saved !== true || !Number.isInteger(res.checksum) || res.checksum! < 0 || res.checksum! > 0xffffffff) {
          update((s) => ({ ...s, loaded: false }));
          throw new Error('Save could not be confirmed. Reload the node version before saving again.');
        }
        const updatedPersisted: TimelineDocument = {
          ...submitted,
          checksum: res.checksum!,
        };

        update((s) => ({
          ...s,
          persisted: cloneTimelineDocument(updatedPersisted),
          working: isTimelineDirty(s.working, submitted) && s.working
            ? { ...cloneTimelineDocument(s.working), checksum: updatedPersisted.checksum }
            : cloneTimelineDocument(updatedPersisted),
          saving: false,
          conflict: false,
          conflictChecksum: null,
          remoteChanged: false,
          error: null,
        }));
        return true;
      } catch (err: any) {
        if (!isCurrent()) return false;
        if (err?.status === 409 || err?.error === 'timeline_conflict') {
          update((s) => ({
            ...s,
            saving: false,
            conflict: true,
            conflictChecksum: err?.details?.currentChecksum ?? err?.currentChecksum ?? null,
            remoteChanged: true,
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
      if (!state.nodeIp || !owner?.isCurrent() || state.loading || state.saving) return;
      await this.load(state.nodeId || '', state.nodeIp);
    },

    keepDraft() {
      update((s) => ({ ...s, conflict: false }));
    },

    reviewConflict() {
      update((s) => s.remoteChanged ? { ...s, conflict: true } : s);
    },

    importDocument(imported: Partial<TimelineDocument>) {
      const state = get({ subscribe });
      if (!state.loaded || !state.working || !owner?.isCurrent()) return;
      const doc = parseTimelineDocument(imported);
      update((s) => ({
        ...s,
        working: doc,
        selectedChannel: null,
        selectedStep: null,
        error: null,
      }));
    },

    reset() {
      ++currentLoadGen;
      ++currentSaveGen;
      owner = null;
      boundSession = null;
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
