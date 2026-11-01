import type { TimelineDocument } from '../api/timeline';
import { TRACK_COUNT, MAX_STEPS_PER_TRACK, MIN_BPM, MAX_BPM, normalizeTimelineDocument } from '../api/timeline';

export interface MissingSceneReference {
  channel: number;
  step: number;
  presetIndex: number;
}

export interface TimelineValidationResult {
  valid: boolean;
  errors: string[];
  missingScenes: MissingSceneReference[];
}

export function validateTimelineDocument(
  doc: TimelineDocument,
  existingSceneSlots?: Set<number>
): TimelineValidationResult {
  const errors: string[] = [];
  const missingScenes: MissingSceneReference[] = [];

  if (!doc) {
    return { valid: false, errors: ['Document is null or undefined'], missingScenes: [] };
  }

  if (!Number.isInteger(doc.bpm) || doc.bpm < MIN_BPM || doc.bpm > MAX_BPM) {
    errors.push(`BPM must be between ${MIN_BPM} and ${MAX_BPM} (got ${doc.bpm})`);
  }

  if (!Array.isArray(doc.tracks) || doc.tracks.length !== TRACK_COUNT) {
    errors.push(`Timeline must have exactly ${TRACK_COUNT} physical tracks (got ${doc.tracks?.length})`);
  } else {
    for (let i = 0; i < TRACK_COUNT; i++) {
      const track = doc.tracks[i];
      if (!track || typeof track !== 'object') {
        errors.push(`Track ${i} is missing`);
        continue;
      }

      if (track.channelId !== i) {
        errors.push(`Track ${i} has mismatched channelId ${track.channelId} (expected ${i})`);
      }

      const numSteps = track.numSteps;
      if (!Number.isInteger(numSteps) || numSteps < 0 || numSteps > MAX_STEPS_PER_TRACK) {
        errors.push(`Track ${i} step count must be 0..${MAX_STEPS_PER_TRACK} (got ${numSteps})`);
      }
      if (typeof track.enabled !== 'boolean') errors.push(`Track ${i} enabled must be true or false`);
      if (!Array.isArray(track.steps)) {
        errors.push(`Track ${i} steps must be an array`);
        continue;
      }

      const steps = track.steps || [];
      for (let s = 0; s < Math.min(numSteps, MAX_STEPS_PER_TRACK); s++) {
        const step = steps[s];
        if (!step) {
          errors.push(`Track ${i} step ${s} is undefined`);
          continue;
        }

        if (!Number.isInteger(step.presetIndex) || step.presetIndex < 0 || step.presetIndex > 31) {
          errors.push(`Track ${i} step ${s} presetIndex must be 0..31 (got ${step.presetIndex})`);
        } else if (existingSceneSlots && !existingSceneSlots.has(step.presetIndex)) {
          missingScenes.push({
            channel: i,
            step: s,
            presetIndex: step.presetIndex,
          });
        }

        if (!Number.isInteger(step.lengthBeats) || step.lengthBeats < 1 || step.lengthBeats > 64) {
          errors.push(`Track ${i} step ${s} lengthBeats must be 1..64 (got ${step.lengthBeats})`);
        }
      }
    }
  }

  return {
    valid: errors.length === 0 && missingScenes.length === 0,
    errors,
    missingScenes,
  };
}

/** Validate the source before normalization can fill in an empty document. */
export function parseTimelineDocument(input: unknown, requireChecksum = false): TimelineDocument {
  if (!input || typeof input !== 'object' || Array.isArray(input)) {
    throw new Error('Expected a timeline document object.');
  }
  const raw = input as TimelineDocument & { type?: unknown };
  if (raw.schema !== 2 || (raw.type !== undefined && raw.type !== 'lumigrid-timeline')) {
    throw new Error('Expected a schema 2 Lumigrid timeline document.');
  }
  if ((requireChecksum || raw.checksum !== undefined) &&
      (!Number.isInteger(raw.checksum) || raw.checksum < 0 || raw.checksum > 0xffffffff)) {
    throw new Error('The timeline document has no valid saved checksum.');
  }
  const validation = validateTimelineDocument(raw);
  if (!validation.valid) throw new Error(validation.errors.join(' '));
  return normalizeTimelineDocument(raw);
}
