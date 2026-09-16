import { describe, it, expect } from 'vitest';
import {
  createDefaultStep,
  createDefaultTrack,
  createDefaultSequence,
  cloneSequence,
  serializeSequence,
  isSequenceDirty,
  stepDurationSeconds,
  TRACK_COUNT,
  STEP_COUNT,
} from './timeline';

describe('timeline model & serialization', () => {
  it('creates default steps, tracks, and 8-track sequence', () => {
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
    expect(seq.tracks).toHaveLength(TRACK_COUNT);
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
});
