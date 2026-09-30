import { apiJson } from './transport';

export interface TimelineStep {
  presetIndex: number;
  lengthBeats: number;
}

export interface TimelineTrack {
  channelId: number;
  numSteps: number;
  enabled: boolean;
  steps: TimelineStep[];
}

export interface TimelineDocument {
  schema: 2;
  checksum: number;
  bpm: number;
  enabled?: boolean;
  tracks: TimelineTrack[];
}

// Backward-compatible aliases for existing components/tests
export type SequenceStep = TimelineStep;
export type SequenceTrack = TimelineTrack;
export interface Sequence {
  schema?: 2;
  checksum?: number;
  bpm: number;
  enabled?: boolean;
  tracks: SequenceTrack[];
}

export interface SaveTimelineResponse {
  saved: true;
  checksum?: number;
  commandId?: number;
}

export interface TimelineConflictError {
  error: 'timeline_conflict';
  currentChecksum: number;
}

export interface TimelineSceneMissingError {
  error: 'timeline_scene_missing';
  channel: number;
  step: number;
  presetIndex: number;
}

export function getSequence(ip: string): Promise<TimelineDocument> {
  return apiJson<TimelineDocument>(ip, '/api/v1/sequence');
}

export async function saveSequence(
  ip: string,
  seq: Partial<TimelineDocument | Sequence>,
  expectedChecksum?: number
): Promise<SaveTimelineResponse> {
  const bodyObj: Record<string, unknown> = { ...seq };
  if (expectedChecksum !== undefined && expectedChecksum !== 0) {
    bodyObj.expectedChecksum = expectedChecksum;
  }

  return apiJson<SaveTimelineResponse>(ip, '/api/v1/sequence', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(bodyObj),
  });
}

export const TRACK_COUNT = 16;
export const MAX_STEPS_PER_TRACK = 16;
export const STEP_COUNT = 16;
export const MIN_BPM = 10;
export const MAX_BPM = 500;

export const CHANNEL_PORTS = [
  'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7', 'A8',
  'P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7', 'P8',
] as const;

export function getChannelPort(channelId: number): string {
  if (channelId >= 0 && channelId < CHANNEL_PORTS.length) {
    return CHANNEL_PORTS[channelId];
  }
  return `CH${channelId}`;
}

export function isPwmChannel(channelId: number): boolean {
  return channelId >= 8 && channelId < 16;
}

export function createDefaultStep(presetIndex = 0, lengthBeats = 4): TimelineStep {
  return {
    presetIndex: Math.max(0, Math.min(31, Math.round(presetIndex))),
    lengthBeats: Math.max(1, Math.min(64, Math.round(lengthBeats))),
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

export function createEmptyTrack(channelId: number): TimelineTrack {
  return {
    channelId,
    numSteps: 0,
    enabled: false,
    steps: [],
  };
}

export function createDefaultTimelineDocument(bpm = 120): TimelineDocument {
  return {
    schema: 2,
    checksum: 0,
    bpm: Math.max(MIN_BPM, Math.min(MAX_BPM, Math.round(bpm))),
    tracks: Array.from({ length: TRACK_COUNT }, (_, i) => createEmptyTrack(i)),
  };
}

export function createDefaultSequence(): Sequence {
  return {
    bpm: 120,
    enabled: false,
    tracks: Array.from({ length: TRACK_COUNT }, (_, i) => createDefaultTrack(i)),
  };
}

export function cloneTimelineDocument(doc: TimelineDocument): TimelineDocument {
  return structuredClone(doc);
}

export function cloneSequence(seq: Sequence): Sequence {
  return structuredClone(seq);
}

export function normalizeTimelineStep(step?: Partial<TimelineStep> | null): TimelineStep {
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

export function normalizeTimelineDocument(doc?: Partial<TimelineDocument | Sequence> | null): TimelineDocument {
  const bpm = typeof doc?.bpm === 'number' && Number.isFinite(doc.bpm)
    ? Math.max(MIN_BPM, Math.min(MAX_BPM, Math.round(doc.bpm)))
    : 120;
  const checksum = typeof doc?.checksum === 'number' && Number.isFinite(doc.checksum)
    ? doc.checksum
    : 0;

  const rawTracks = doc?.tracks || [];
  const trackMap = new Map<number, Partial<TimelineTrack>>();
  for (const t of rawTracks) {
    if (typeof t?.channelId === 'number' && t.channelId >= 0 && t.channelId < TRACK_COUNT) {
      if (!trackMap.has(t.channelId)) {
        trackMap.set(t.channelId, t);
      }
    }
  }

  const tracks: TimelineTrack[] = [];
  for (let i = 0; i < TRACK_COUNT; i++) {
    const rawTrack = trackMap.get(i) || rawTracks[i];
    const numSteps = typeof rawTrack?.numSteps === 'number' && Number.isFinite(rawTrack.numSteps)
      ? Math.max(0, Math.min(MAX_STEPS_PER_TRACK, Math.round(rawTrack.numSteps)))
      : (rawTrack?.steps ? Math.min(MAX_STEPS_PER_TRACK, rawTrack.steps.length) : 0);
    const enabled = rawTrack?.enabled !== undefined ? Boolean(rawTrack.enabled) : (numSteps > 0);
    const steps: TimelineStep[] = [];
    const rawSteps = rawTrack?.steps || [];
    for (let s = 0; s < numSteps; s++) {
      steps.push(normalizeTimelineStep(rawSteps[s]));
    }
    tracks.push({
      channelId: i,
      numSteps,
      enabled,
      steps,
    });
  }

  return {
    schema: 2,
    checksum,
    bpm,
    tracks,
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

export function serializeTimelineDocument(doc: TimelineDocument): string {
  const norm = normalizeTimelineDocument(doc);
  return JSON.stringify({
    schema: 2,
    bpm: norm.bpm,
    tracks: norm.tracks.map((t) => ({
      channelId: t.channelId,
      numSteps: t.numSteps,
      enabled: t.enabled,
      steps: t.steps.slice(0, t.numSteps).map((s) => ({
        presetIndex: s.presetIndex,
        lengthBeats: s.lengthBeats,
      })),
    })),
  });
}

export function serializeSequence(seq: Sequence): string {
  const norm = normalizeTimelineSequence(seq);
  return JSON.stringify(norm);
}

export function isTimelineDirty(current: TimelineDocument | null, saved: TimelineDocument | null): boolean {
  if (!current && !saved) return false;
  if (!current || !saved) return true;
  return serializeTimelineDocument(current) !== serializeTimelineDocument(saved);
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
