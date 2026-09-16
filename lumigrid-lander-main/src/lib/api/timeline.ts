import type { Sequence, SequenceTrack, SequenceStep } from './node';

export const TRACK_COUNT = 8;
export const STEP_COUNT = 16;
export const MIN_BPM = 10;
export const MAX_BPM = 500;

export function createDefaultStep(): SequenceStep {
  return {
    presetIndex: 0,
    lengthBeats: 4,
  };
}

export function createDefaultTrack(channelId: number): SequenceTrack {
  return {
    channelId,
    numSteps: 4,
    enabled: true,
    steps: Array.from({ length: STEP_COUNT }, () => createDefaultStep()),
  };
}

export function createDefaultSequence(): Sequence {
  return {
    bpm: 120,
    enabled: false,
    tracks: Array.from({ length: TRACK_COUNT }, (_, i) => createDefaultTrack(i)),
  };
}

export function cloneSequence(seq: Sequence): Sequence {
  return structuredClone(seq);
}

export function serializeSequence(seq: Sequence): string {
  const bpm = Math.max(MIN_BPM, Math.min(MAX_BPM, Math.round(seq.bpm || 120)));
  const enabled = Boolean(seq.enabled);

  const tracks = (seq.tracks || []).slice(0, TRACK_COUNT).map((t, idx) => {
    const channelId = typeof t.channelId === 'number' ? t.channelId : idx;
    const numSteps = Math.max(1, Math.min(STEP_COUNT, Math.round(t.numSteps || 4)));
    const trackEnabled = Boolean(t.enabled);
    const steps = (t.steps || []).slice(0, STEP_COUNT).map(s => ({
      presetIndex: Math.max(0, Math.round(s.presetIndex || 0)),
      lengthBeats: Math.max(1, Math.min(64, Math.round(s.lengthBeats || 4))),
    }));

    // Pad if fewer than STEP_COUNT
    while (steps.length < STEP_COUNT) {
      steps.push(createDefaultStep());
    }

    return {
      channelId,
      numSteps,
      enabled: trackEnabled,
      steps,
    };
  });

  // Pad if fewer than TRACK_COUNT
  while (tracks.length < TRACK_COUNT) {
    tracks.push(createDefaultTrack(tracks.length));
  }

  return JSON.stringify({
    bpm,
    enabled,
    tracks,
  });
}

export function isSequenceDirty(current: Sequence | null, saved: Sequence | null): boolean {
  if (!current && !saved) return false;
  if (!current || !saved) return true;
  return serializeSequence(current) !== serializeSequence(saved);
}

export function stepDurationSeconds(beats: number, bpm: number): number {
  const validBpm = Math.max(MIN_BPM, bpm || 120);
  const validBeats = Math.max(1, beats || 1);
  return (validBeats * 60) / validBpm;
}
