import type { TimelineDocument, TimelineTrack, TimelineStep } from '../api/timeline';
import { MAX_STEPS_PER_TRACK, normalizeTimelineStep } from '../api/timeline';

/**
 * Appends a step to a track up to MAX_STEPS_PER_TRACK (16).
 * Automatically enables the track if it was disabled and empty.
 */
export function addStep(
  track: TimelineTrack,
  presetIndex: number,
  lengthBeats = 4
): TimelineTrack {
  const steps = [...track.steps];
  if (steps.length >= MAX_STEPS_PER_TRACK) {
    return track;
  }
  const newStep = normalizeTimelineStep({ presetIndex, lengthBeats });
  steps.push(newStep);
  return {
    ...track,
    steps,
    numSteps: steps.length,
    enabled: true,
  };
}

/**
 * Updates the beat length of a specific step (1..64 beats).
 */
export function updateStepLength(
  track: TimelineTrack,
  stepIndex: number,
  lengthBeats: number
): TimelineTrack {
  if (stepIndex < 0 || stepIndex >= track.steps.length) return track;
  const clamped = Math.max(1, Math.min(64, Math.round(lengthBeats)));
  const steps = track.steps.map((s, idx) =>
    idx === stepIndex ? { ...s, lengthBeats: clamped } : s
  );
  return { ...track, steps };
}

/**
 * Updates the preset (scene) index of a specific step (0..31).
 */
export function updateStepPreset(
  track: TimelineTrack,
  stepIndex: number,
  presetIndex: number
): TimelineTrack {
  if (stepIndex < 0 || stepIndex >= track.steps.length) return track;
  const clamped = Math.max(0, Math.min(31, Math.round(presetIndex)));
  const steps = track.steps.map((s, idx) =>
    idx === stepIndex ? { ...s, presetIndex: clamped } : s
  );
  return { ...track, steps };
}

/**
 * Duplicates a step immediately adjacent to itself, up to 16 steps max.
 */
export function duplicateStep(
  track: TimelineTrack,
  stepIndex: number
): TimelineTrack {
  if (stepIndex < 0 || stepIndex >= track.steps.length) return track;
  if (track.steps.length >= MAX_STEPS_PER_TRACK) return track;

  const target = track.steps[stepIndex];
  const steps = [...track.steps];
  steps.splice(stepIndex + 1, 0, { ...target });

  return {
    ...track,
    steps,
    numSteps: steps.length,
  };
}

/**
 * Deletes a step and compacts the remaining steps without gaps.
 */
export function deleteStep(
  track: TimelineTrack,
  stepIndex: number
): TimelineTrack {
  if (stepIndex < 0 || stepIndex >= track.steps.length) return track;
  const steps = track.steps.filter((_, idx) => idx !== stepIndex);
  return {
    ...track,
    steps,
    numSteps: steps.length,
    enabled: steps.length > 0 ? track.enabled : false,
  };
}

/**
 * Moves a step left (earlier in track order).
 */
export function moveStepLeft(
  track: TimelineTrack,
  stepIndex: number
): TimelineTrack {
  if (stepIndex <= 0 || stepIndex >= track.steps.length) return track;
  return reorderStep(track, stepIndex, stepIndex - 1);
}

/**
 * Moves a step right (later in track order).
 */
export function moveStepRight(
  track: TimelineTrack,
  stepIndex: number
): TimelineTrack {
  if (stepIndex < 0 || stepIndex >= track.steps.length - 1) return track;
  return reorderStep(track, stepIndex, stepIndex + 1);
}

/**
 * Reorders a step within a track.
 */
export function reorderStep(
  track: TimelineTrack,
  fromIndex: number,
  toIndex: number
): TimelineTrack {
  if (
    fromIndex < 0 ||
    fromIndex >= track.steps.length ||
    toIndex < 0 ||
    toIndex >= track.steps.length ||
    fromIndex === toIndex
  ) {
    return track;
  }
  const steps = [...track.steps];
  const [removed] = steps.splice(fromIndex, 1);
  steps.splice(toIndex, 0, removed);
  return { ...track, steps };
}

/**
 * Toggles a track's active/muted state.
 */
export function toggleTrackEnabled(track: TimelineTrack): TimelineTrack {
  return {
    ...track,
    enabled: !track.enabled,
  };
}

/**
 * Clears all steps from a track, setting numSteps to 0 and enabled to false.
 */
export function clearTrack(track: TimelineTrack): TimelineTrack {
  return {
    ...track,
    numSteps: 0,
    enabled: false,
    steps: [],
  };
}

/**
 * Immutably updates a single track within the document.
 */
export function updateTrackInDocument(
  doc: TimelineDocument,
  updatedTrack: TimelineTrack
): TimelineDocument {
  const tracks = doc.tracks.map((t) =>
    t.channelId === updatedTrack.channelId ? updatedTrack : t
  );
  return { ...doc, tracks };
}

/**
 * Immutably sets document BPM (10..500).
 */
export function setDocumentBpm(
  doc: TimelineDocument,
  bpm: number
): TimelineDocument {
  const clamped = Math.max(10, Math.min(500, Math.round(bpm)));
  return { ...doc, bpm: clamped };
}
