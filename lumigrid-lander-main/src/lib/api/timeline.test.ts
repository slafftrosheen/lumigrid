import { describe, it, expect, vi } from 'vitest';
import { apiJson } from './transport';
import {
  createDefaultTimelineDocument,
  saveSequence,
  createDefaultStep,
  createDefaultTrack,
  createDefaultSequence,
  cloneSequence,
  serializeSequence,
  isSequenceDirty,
  stepDurationSeconds,
  normalizeTimelineStep,
  normalizeTimelineTrack,
  normalizeTimelineSequence,
  TRACK_COUNT,
  STEP_COUNT,
  MIN_BPM,
  MAX_BPM,
} from './timeline';

describe('timeline model & serialization', () => {
  it('creates default steps, tracks, and 16-track sequence', () => {
    const step = createDefaultStep();
    expect(step.presetIndex).toBe(0);
    expect(step.lengthBeats).toBe(4);

    const track = createDefaultTrack(2);
    expect(track.channelId).toBe(2);
    expect(track.numSteps).toBe(4);
    expect(track.enabled).toBe(true);
    expect(track.steps).toHaveLength(STEP_COUNT);

    const seq = createDefaultSequence();
    expect(seq.bpm).toBe(120);
    expect(seq.enabled).toBe(false);
    expect(TRACK_COUNT).toBe(16);
    expect(seq.tracks).toHaveLength(16);
    expect(seq.tracks[15].channelId).toBe(15);
  });

  it('clones sequence deeply without mutating the source', () => {
    const original = createDefaultSequence();
    const clone = cloneSequence(original);
    clone.bpm = 140;
    clone.tracks[0].steps[0].presetIndex = 5;

    expect(original.bpm).toBe(120);
    expect(original.tracks[0].steps[0].presetIndex).toBe(0);
    expect(clone.bpm).toBe(140);
    expect(clone.tracks[0].steps[0].presetIndex).toBe(5);
  });

  it('serializes sequence deterministically and clamps BPM', () => {
    const seq = createDefaultSequence();
    seq.bpm = 999; // clamped to 500
    const str = serializeSequence(seq);
    const parsed = JSON.parse(str);
    expect(parsed.bpm).toBe(500);
    expect(parsed.tracks).toHaveLength(TRACK_COUNT);
    expect(parsed.tracks[0].steps).toHaveLength(STEP_COUNT);
  });

  it('detects dirty changes between sequences', () => {
    const s1 = createDefaultSequence();
    const s2 = cloneSequence(s1);

    expect(isSequenceDirty(s1, s2)).toBe(false);

    s2.bpm = 135;
    expect(isSequenceDirty(s2, s1)).toBe(true);

    const s3 = cloneSequence(s1);
    s3.tracks[1].steps[2].lengthBeats = 8;
    expect(isSequenceDirty(s3, s1)).toBe(true);
  });

  it('calculates step duration in seconds based on beats and BPM', () => {
    // 4 beats at 120 BPM = 4 * 60 / 120 = 2.0 seconds
    expect(stepDurationSeconds(4, 120)).toBe(2.0);
    // 2 beats at 60 BPM = 2.0 seconds
    expect(stepDurationSeconds(2, 60)).toBe(2.0);
    // 8 beats at 240 BPM = 2.0 seconds
    expect(stepDurationSeconds(8, 240)).toBe(2.0);
  });

  it('normalizes malformed timeline sequence with boundary values', () => {
    const malformed = {
      bpm: -100, // clamped to MIN_BPM (10)
      enabled: 1 as any,
      tracks: [
        {
          channelId: 99, // clamped to 15
          numSteps: 0, // clamped to 1
          enabled: false,
          steps: [
            {
              presetIndex: 99, // clamped to 31
              lengthBeats: 500, // clamped to 64
            },
            {
              presetIndex: -5, // clamped to 0
              lengthBeats: -10, // clamped to 1
            },
          ],
        },
      ],
    };

    const norm = normalizeTimelineSequence(malformed as any);
    expect(norm.bpm).toBe(MIN_BPM);
    expect(norm.enabled).toBe(true);
    expect(norm.tracks).toHaveLength(TRACK_COUNT);
    expect(norm.tracks[0].channelId).toBe(15);
    expect(norm.tracks[0].numSteps).toBe(1);
    expect(norm.tracks[0].enabled).toBe(false);
    expect(norm.tracks[0].steps).toHaveLength(STEP_COUNT);
    expect(norm.tracks[0].steps[0].presetIndex).toBe(31);
    expect(norm.tracks[0].steps[0].lengthBeats).toBe(64);
    expect(norm.tracks[0].steps[1].presetIndex).toBe(0);
    expect(norm.tracks[0].steps[1].lengthBeats).toBe(1);
  });

  it('preserves non-sequential channel IDs and does not confuse them with lane index', () => {
    const seq = createDefaultSequence();
    const nonSequential = [15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0];
    seq.tracks.forEach((t, i) => {
      t.channelId = nonSequential[i];
    });

    const normalized = normalizeTimelineSequence(seq);
    normalized.tracks.forEach((t, i) => {
      expect(t.channelId).toBe(nonSequential[i]);
    });
  });

  it('preserves all 16 allocated steps and maintains round-trip determinism', () => {
    const seq = createDefaultSequence();
    seq.bpm = 145;
    seq.enabled = true;
    seq.tracks[2].numSteps = 6;
    // Set a step beyond numSteps to test that inactive steps are preserved in storage
    seq.tracks[2].steps[10] = { presetIndex: 12, lengthBeats: 8 };

    const serialized1 = serializeSequence(seq);
    const parsed = JSON.parse(serialized1);
    const normalized = normalizeTimelineSequence(parsed);
    const serialized2 = serializeSequence(normalized);

    expect(serialized1).toBe(serialized2);
    expect(normalized.tracks[2].numSteps).toBe(6);
    expect(normalized.tracks[2].steps[10]).toEqual({ presetIndex: 12, lengthBeats: 8 });
  });
});

vi.mock('./transport', () => ({ apiJson: vi.fn(async () => ({ saved: true, checksum: 1 })) }));

it('includes a known zero checksum in optimistic timeline saves', async () => {
  await saveSequence('node-a', createDefaultTimelineDocument(), 0);
  const options = vi.mocked(apiJson).mock.calls.at(-1)?.[2];
  expect(JSON.parse(String(options?.body)).expectedChecksum).toBe(0);
});
