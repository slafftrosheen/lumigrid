import type { TimelineTrack, TimelineStep } from '../api/timeline';

export interface ActiveStepResult {
  stepIndex: number;
  presetIndex: number;
  startBeat: number;
  endBeat: number;
  beatInTrack: number;
}

export interface StepBeatRange {
  startBeat: number;
  endBeat: number;
  lengthBeats: number;
}

/**
 * Calculates the total length of a track in beats.
 * Returns 0 if the track is disabled or has no steps.
 */
export function getTrackTotalBeats(track: TimelineTrack): number {
  if (!track || !track.enabled || track.numSteps === 0) {
    return 0;
  }
  const n = Math.min(track.numSteps, track.steps.length, 16);
  let total = 0;
  for (let i = 0; i < n; i++) {
    const len = track.steps[i]?.lengthBeats ?? 4;
    total += Math.max(1, len);
  }
  return total;
}

/**
 * Returns the track's loop-wrapped beat position given global elapsed beats.
 */
export function getTrackBeatPosition(track: TimelineTrack, globalBeat: number): number {
  const total = getTrackTotalBeats(track);
  if (total <= 0) return 0;
  const wrapped = Math.floor(globalBeat) % total;
  return wrapped >= 0 ? wrapped : wrapped + total;
}

/**
 * Determines which step is active on the given track at the specified global beat.
 * Exactly matches firmware TimelineEvaluator algorithm.
 */
export function getActiveStepAtBeat(track: TimelineTrack, globalBeat: number): ActiveStepResult | null {
  if (!track || !track.enabled || track.numSteps === 0) {
    return null;
  }
  const totalBeats = getTrackTotalBeats(track);
  if (totalBeats <= 0) {
    return null;
  }

  const beatInTrack = getTrackBeatPosition(track, globalBeat);
  const n = Math.min(track.numSteps, track.steps.length, 16);

  let currentBeat = 0;
  for (let i = 0; i < n; i++) {
    const step = track.steps[i];
    const len = Math.max(1, step?.lengthBeats ?? 4);
    const nextBeat = currentBeat + len;
    if (beatInTrack >= currentBeat && beatInTrack < nextBeat) {
      return {
        stepIndex: i,
        presetIndex: step?.presetIndex ?? 0,
        startBeat: currentBeat,
        endBeat: nextBeat,
        beatInTrack,
      };
    }
    currentBeat = nextBeat;
  }

  return null;
}

/**
 * Returns the start and end beat within the track's loop for a given step.
 */
export function getStepBeatRange(track: TimelineTrack, stepIndex: number): StepBeatRange | null {
  if (!track || stepIndex < 0 || stepIndex >= track.numSteps || stepIndex >= track.steps.length) {
    return null;
  }
  const n = Math.min(track.numSteps, track.steps.length, 16);
  let currentBeat = 0;
  for (let i = 0; i < n; i++) {
    const step = track.steps[i];
    const len = Math.max(1, step?.lengthBeats ?? 4);
    const nextBeat = currentBeat + len;
    if (i === stepIndex) {
      return {
        startBeat: currentBeat,
        endBeat: nextBeat,
        lengthBeats: len,
      };
    }
    currentBeat = nextBeat;
  }
  return null;
}

/**
 * Formats beat duration in seconds at a given BPM.
 */
export function formatDurationSeconds(beats: number, bpm: number): string {
  const validBpm = Math.max(10, bpm || 120);
  const seconds = (beats * 60) / validBpm;
  return `${seconds.toFixed(2)}s`;
}
