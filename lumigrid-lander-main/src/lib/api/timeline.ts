import { apiJson } from './transport';

export interface SequenceStep {
  presetIndex: number;
  lengthBeats: number;
}

export interface SequenceTrack {
  channelId: number;
  numSteps: number;
  enabled: boolean;
  steps: SequenceStep[];
}

export interface Sequence {
  bpm: number;
  enabled: boolean;
  tracks: SequenceTrack[];
}

export function getSequence(ip: string): Promise<Sequence> {
  return apiJson<Sequence>(ip, '/api/v1/sequence');
}

export function saveSequence(ip: string, seq: Sequence): Promise<{ saved: true }> {
  return apiJson<{ saved: true }>(ip, '/api/v1/sequence', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(seq),
  });
}

export const TRACK_COUNT = 16;
export const STEP_COUNT = 16;
export const MIN_BPM = 10;
export const MAX_BPM = 500;
export const CHANNEL_PORTS = [
  'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8',
  'P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8',
] as const;

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

export function normalizeTimelineStep(step?: Partial<SequenceStep> | null): SequenceStep {
  const presetIndex = typeof step?.presetIndex === 'number' && Number.isFinite(step.presetIndex)
    ? Math.max(0, Math.min(31, Math.round(step.presetIndex)))
    : 0;
  const lengthBeats = typeof step?.lengthBeats === 'number' && Number.isFinite(step.lengthBeats)
    ? Math.max(1, Math.min(64, Math.round(step.lengthBeats)))
    : 4;
  return {
    presetIndex,
    lengthBeats,
  };
}

export function normalizeTimelineTrack(track?: Partial<SequenceTrack> | null, defaultChannelId = 0): SequenceTrack {
  const channelId = typeof track?.channelId === 'number' && Number.isFinite(track.channelId)
    ? Math.max(0, Math.min(15, Math.round(track.channelId)))
    : defaultChannelId;
  const numSteps = typeof track?.numSteps === 'number' && Number.isFinite(track.numSteps)
    ? Math.max(1, Math.min(STEP_COUNT, Math.round(track.numSteps)))
    : 4;
  const enabled = track?.enabled !== undefined ? Boolean(track.enabled) : true;

  const rawSteps = track?.steps || [];
  const steps: SequenceStep[] = [];
  for (let i = 0; i < STEP_COUNT; i++) {
    steps.push(normalizeTimelineStep(rawSteps[i]));
  }

  return {
    channelId,
    numSteps,
    enabled,
    steps,
  };
}

export function normalizeTimelineSequence(seq?: Partial<Sequence> | null): Sequence {
  const bpm = typeof seq?.bpm === 'number' && Number.isFinite(seq.bpm)
    ? Math.max(MIN_BPM, Math.min(MAX_BPM, Math.round(seq.bpm)))
    : 120;
  const enabled = Boolean(seq?.enabled);

  const rawTracks = seq?.tracks || [];
  const tracks: SequenceTrack[] = [];
  for (let i = 0; i < TRACK_COUNT; i++) {
    tracks.push(normalizeTimelineTrack(rawTracks[i], i));
  }

  return {
    bpm,
    enabled,
    tracks,
  };
}

export function serializeSequence(seq: Sequence): string {
  const norm = normalizeTimelineSequence(seq);
  return JSON.stringify(norm);
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
