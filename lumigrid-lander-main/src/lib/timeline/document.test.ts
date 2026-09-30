import { describe, it, expect } from 'vitest';
import type { TimelineDocument, TimelineTrack } from '../api/timeline';
import {
  createDefaultTimelineDocument,
  cloneTimelineDocument,
  isTimelineDirty,
  serializeTimelineDocument,
  MAX_STEPS_PER_TRACK,
} from '../api/timeline';
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
  clearTrack,
  updateTrackInDocument,
  setDocumentBpm,
} from './document';
import {
  copyStep,
  pasteStepAfter,
  copyTrackPattern,
  pasteTrackPattern,
  clearClipboard,
} from './clipboard';
import { validateTimelineDocument } from './validation';

describe('Timeline Document Operations & Dirty State (Requirements 72 & 75)', () => {
  it('Requirement A: dirty BPM marks document edited', () => {
    const doc = createDefaultTimelineDocument(120);
    const saved = cloneTimelineDocument(doc);
    expect(isTimelineDirty(doc, saved)).toBe(false);

    const edited = setDocumentBpm(doc, 132);
    expect(isTimelineDirty(edited, saved)).toBe(true);
  });

  it('Requirement B: block add marks dirty', () => {
    const doc = createDefaultTimelineDocument(120);
    const saved = cloneTimelineDocument(doc);

    const track0 = addStep(doc.tracks[0], 4, 8);
    const edited = updateTrackInDocument(doc, track0);

    expect(isTimelineDirty(edited, saved)).toBe(true);
    expect(edited.tracks[0].numSteps).toBe(1);
    expect(edited.tracks[0].steps[0]).toEqual({ presetIndex: 4, lengthBeats: 8 });
  });

  it('Requirement C & V: reorder preserves Scene + beat length and marks dirty', () => {
    let track: TimelineTrack = {
      channelId: 0,
      enabled: true,
      numSteps: 0,
      steps: [],
    };
    track = addStep(track, 1, 4); // Step 0: S1, 4b
    track = addStep(track, 2, 8); // Step 1: S2, 8b
    track = addStep(track, 3, 2); // Step 2: S3, 2b

    const doc = createDefaultTimelineDocument(120);
    const docWithSteps = updateTrackInDocument(doc, track);
    const saved = cloneTimelineDocument(docWithSteps);

    // Reorder step 0 to index 2
    const reorderedTrack = reorderStep(track, 0, 2);
    expect(reorderedTrack.steps[0]).toEqual({ presetIndex: 2, lengthBeats: 8 });
    expect(reorderedTrack.steps[1]).toEqual({ presetIndex: 3, lengthBeats: 2 });
    expect(reorderedTrack.steps[2]).toEqual({ presetIndex: 1, lengthBeats: 4 });

    const editedDoc = updateTrackInDocument(doc, reorderedTrack);
    expect(isTimelineDirty(editedDoc, saved)).toBe(true);
  });

  it('Requirement D & X & Y: resize snaps to integer beat, clamps 1..64, and marks dirty', () => {
    let track: TimelineTrack = { channelId: 0, enabled: true, numSteps: 0, steps: [] };
    track = addStep(track, 5, 4);

    const doc = createDefaultTimelineDocument(120);
    const initialDoc = updateTrackInDocument(doc, track);
    const saved = cloneTimelineDocument(initialDoc);

    // Update length
    const resized = updateStepLength(track, 0, 16);
    expect(resized.steps[0].lengthBeats).toBe(16);

    const editedDoc = updateTrackInDocument(doc, resized);
    expect(isTimelineDirty(editedDoc, saved)).toBe(true);

    // Clamps to 1 and 64
    const clampedMin = updateStepLength(track, 0, 0);
    expect(clampedMin.steps[0].lengthBeats).toBe(1);

    const clampedMax = updateStepLength(track, 0, 999);
    expect(clampedMax.steps[0].lengthBeats).toBe(64);
  });

  it('Requirement E: mute/active toggle marks dirty', () => {
    let track: TimelineTrack = { channelId: 0, enabled: true, numSteps: 1, steps: [{ presetIndex: 0, lengthBeats: 4 }] };
    const doc = createDefaultTimelineDocument(120);
    const initialDoc = updateTrackInDocument(doc, track);
    const saved = cloneTimelineDocument(initialDoc);

    const muted = toggleTrackEnabled(track);
    expect(muted.enabled).toBe(false);

    const editedDoc = updateTrackInDocument(doc, muted);
    expect(isTimelineDirty(editedDoc, saved)).toBe(true);
  });

  it('Requirement W: Move Left accessibility action matches drag reorder', () => {
    let track: TimelineTrack = { channelId: 0, enabled: true, numSteps: 0, steps: [] };
    track = addStep(track, 10, 4);
    track = addStep(track, 20, 8);

    const moved = moveStepLeft(track, 1);
    expect(moved.steps[0]).toEqual({ presetIndex: 20, lengthBeats: 8 });
    expect(moved.steps[1]).toEqual({ presetIndex: 10, lengthBeats: 4 });

    const movedRight = moveStepRight(moved, 0);
    expect(movedRight.steps[0]).toEqual({ presetIndex: 10, lengthBeats: 4 });
    expect(movedRight.steps[1]).toEqual({ presetIndex: 20, lengthBeats: 8 });
  });

  it('Requirement Z: duplicate inserts adjacent block preserving beats', () => {
    let track: TimelineTrack = { channelId: 0, enabled: true, numSteps: 0, steps: [] };
    track = addStep(track, 3, 12);
    track = addStep(track, 4, 6);

    const dup = duplicateStep(track, 0);
    expect(dup.numSteps).toBe(3);
    expect(dup.steps[0]).toEqual({ presetIndex: 3, lengthBeats: 12 });
    expect(dup.steps[1]).toEqual({ presetIndex: 3, lengthBeats: 12 }); // Duplicate adjacent
    expect(dup.steps[2]).toEqual({ presetIndex: 4, lengthBeats: 6 });
  });

  it('Requirement AA: delete compacts sequence without gaps', () => {
    let track: TimelineTrack = { channelId: 0, enabled: true, numSteps: 0, steps: [] };
    track = addStep(track, 1, 4);
    track = addStep(track, 2, 8);
    track = addStep(track, 3, 2);

    // Delete middle step (index 1)
    const del = deleteStep(track, 1);
    expect(del.numSteps).toBe(2);
    expect(del.steps[0]).toEqual({ presetIndex: 1, lengthBeats: 4 });
    expect(del.steps[1]).toEqual({ presetIndex: 3, lengthBeats: 2 });
  });

  it('Requirement AB: copy/paste across channel preserves slot + beats', () => {
    clearClipboard();
    let trackA1: TimelineTrack = { channelId: 0, enabled: true, numSteps: 0, steps: [] };
    trackA1 = addStep(trackA1, 7, 16);

    copyStep(trackA1.steps[0]);

    let trackP4: TimelineTrack = { channelId: 11, enabled: false, numSteps: 0, steps: [] };
    const pastedP4 = pasteStepAfter(trackP4);

    expect(pastedP4.channelId).toBe(11); // destination channel
    expect(pastedP4.numSteps).toBe(1);
    expect(pastedP4.steps[0]).toEqual({ presetIndex: 7, lengthBeats: 16 });
  });

  it('Requirement AC: track paste keeps destination physical channel identity', () => {
    clearClipboard();
    let sourceTrack: TimelineTrack = {
      channelId: 2, // A3
      enabled: true,
      numSteps: 2,
      steps: [
        { presetIndex: 5, lengthBeats: 4 },
        { presetIndex: 6, lengthBeats: 8 },
      ],
    };

    copyTrackPattern(sourceTrack);

    let destTrack: TimelineTrack = {
      channelId: 9, // P2
      enabled: false,
      numSteps: 0,
      steps: [],
    };

    const result = pasteTrackPattern(destTrack);
    expect(result.channelId).toBe(9); // Strictly P2!
    expect(result.enabled).toBe(true);
    expect(result.numSteps).toBe(2);
    expect(result.steps).toEqual([
      { presetIndex: 5, lengthBeats: 4 },
      { presetIndex: 6, lengthBeats: 8 },
    ]);
  });

  it('Requirement AD: empty track serializes honestly as 0 steps', () => {
    const doc = createDefaultTimelineDocument(120);
    const serialized = serializeTimelineDocument(doc);
    const parsed = JSON.parse(serialized);

    expect(parsed.tracks).toHaveLength(16);
    for (let i = 0; i < 16; i++) {
      expect(parsed.tracks[i].channelId).toBe(i);
      expect(parsed.tracks[i].numSteps).toBe(0);
      expect(parsed.tracks[i].steps).toEqual([]);
    }
  });

  it('Requirement H: missing Scene reference prevents save', () => {
    const doc = createDefaultTimelineDocument(120);
    doc.tracks[0] = addStep(doc.tracks[0], 14, 4); // references Scene 14

    const existingSlots = new Set([0, 1, 2]); // Scene 14 does NOT exist
    const validation = validateTimelineDocument(doc, existingSlots);

    expect(validation.valid).toBe(false);
    expect(validation.missingScenes).toHaveLength(1);
    expect(validation.missingScenes[0]).toEqual({
      channel: 0,
      step: 0,
      presetIndex: 14,
    });
  });
});
