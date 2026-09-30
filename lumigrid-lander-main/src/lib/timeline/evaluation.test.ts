import { describe, it, expect } from 'vitest';
import type { TimelineTrack } from '../api/timeline';
import {
  getTrackTotalBeats,
  getTrackBeatPosition,
  getActiveStepAtBeat,
  getStepBeatRange,
  formatDurationSeconds,
} from './evaluation';

describe('Timeline Pure Evaluator (LG-033 Requirements Q..U & AO..AV)', () => {
  // Requirement Q / AO: One 4-beat step
  it('evaluates single 4-beat step accurately and tracks beat width', () => {
    const track: TimelineTrack = {
      channelId: 0,
      enabled: true,
      numSteps: 1,
      steps: [{ presetIndex: 3, lengthBeats: 4 }],
    };

    expect(getTrackTotalBeats(track)).toBe(4);

    // At beat 0
    const step0 = getActiveStepAtBeat(track, 0);
    expect(step0).not.toBeNull();
    expect(step0?.stepIndex).toBe(0);
    expect(step0?.presetIndex).toBe(3);
    expect(step0?.startBeat).toBe(0);
    expect(step0?.endBeat).toBe(4);
    expect(step0?.beatInTrack).toBe(0);

    // At beat 2
    const step2 = getActiveStepAtBeat(track, 2);
    expect(step2?.stepIndex).toBe(0);
    expect(step2?.beatInTrack).toBe(2);

    // At beat 3
    const step3 = getActiveStepAtBeat(track, 3);
    expect(step3?.stepIndex).toBe(0);
    expect(step3?.beatInTrack).toBe(3);

    // Wraps at beat 4
    const step4 = getActiveStepAtBeat(track, 4);
    expect(step4?.stepIndex).toBe(0);
    expect(step4?.beatInTrack).toBe(0);
  });

  // Requirement R / AP: 4+8+2 pattern total = 14 beats
  it('evaluates 4+8+2 beat pattern with total = 14 beats', () => {
    const track: TimelineTrack = {
      channelId: 1,
      enabled: true,
      numSteps: 3,
      steps: [
        { presetIndex: 10, lengthBeats: 4 },
        { presetIndex: 11, lengthBeats: 8 },
        { presetIndex: 12, lengthBeats: 2 },
      ],
    };

    expect(getTrackTotalBeats(track)).toBe(14);

    // Requirement S / AQ: Exact boundary selects next step
    // Beat 0..3: Step 0 [0, 4)
    expect(getActiveStepAtBeat(track, 0)?.stepIndex).toBe(0);
    expect(getActiveStepAtBeat(track, 3)?.stepIndex).toBe(0);

    // Beat 4: Exact boundary moves to Step 1 [4, 12)
    const b4 = getActiveStepAtBeat(track, 4);
    expect(b4?.stepIndex).toBe(1);
    expect(b4?.startBeat).toBe(4);
    expect(b4?.endBeat).toBe(12);

    // Beat 11: Still Step 1
    expect(getActiveStepAtBeat(track, 11)?.stepIndex).toBe(1);

    // Beat 12: Exact boundary moves to Step 2 [12, 14)
    const b12 = getActiveStepAtBeat(track, 12);
    expect(b12?.stepIndex).toBe(2);
    expect(b12?.startBeat).toBe(12);
    expect(b12?.endBeat).toBe(14);

    expect(getActiveStepAtBeat(track, 13)?.stepIndex).toBe(2);

    // Requirement T / AR: Pattern wraps at beat 14
    const b14 = getActiveStepAtBeat(track, 14);
    expect(b14?.stepIndex).toBe(0);
    expect(b14?.beatInTrack).toBe(0);

    // Beat 18 (18 % 14 = 4) -> Step 1
    const b18 = getActiveStepAtBeat(track, 18);
    expect(b18?.stepIndex).toBe(1);
    expect(b18?.beatInTrack).toBe(4);
  });

  // Requirement AS: Zero-step track ignored
  it('returns null for zero-step or disabled track', () => {
    const emptyTrack: TimelineTrack = {
      channelId: 2,
      enabled: true,
      numSteps: 0,
      steps: [],
    };
    expect(getTrackTotalBeats(emptyTrack)).toBe(0);
    expect(getActiveStepAtBeat(emptyTrack, 4)).toBeNull();

    // Requirement AT: Muted track ignored
    const mutedTrack: TimelineTrack = {
      channelId: 3,
      enabled: false,
      numSteps: 1,
      steps: [{ presetIndex: 0, lengthBeats: 4 }],
    };
    expect(getTrackTotalBeats(mutedTrack)).toBe(0);
    expect(getActiveStepAtBeat(mutedTrack, 2)).toBeNull();
  });

  // Requirement U / AU: Different tracks show independent loop position
  it('supports independent track loops (polyrhythmic loops)', () => {
    const track4: TimelineTrack = {
      channelId: 0,
      enabled: true,
      numSteps: 1,
      steps: [{ presetIndex: 1, lengthBeats: 4 }],
    };

    const track6: TimelineTrack = {
      channelId: 1,
      enabled: true,
      numSteps: 2,
      steps: [
        { presetIndex: 2, lengthBeats: 3 },
        { presetIndex: 3, lengthBeats: 3 },
      ],
    };

    expect(getTrackTotalBeats(track4)).toBe(4);
    expect(getTrackTotalBeats(track6)).toBe(6);

    // At global beat 4:
    // track4 has completed 1 full loop (4 % 4 = 0) -> step 0, beat 0
    // track6 is at beat 4 (4 % 6 = 4) -> step 1, beat 4
    expect(getTrackBeatPosition(track4, 4)).toBe(0);
    expect(getTrackBeatPosition(track6, 4)).toBe(4);
    expect(getActiveStepAtBeat(track4, 4)?.stepIndex).toBe(0);
    expect(getActiveStepAtBeat(track6, 4)?.stepIndex).toBe(1);
  });

  it('computes step beat range correctly', () => {
    const track: TimelineTrack = {
      channelId: 0,
      enabled: true,
      numSteps: 2,
      steps: [
        { presetIndex: 0, lengthBeats: 8 },
        { presetIndex: 1, lengthBeats: 16 },
      ],
    };

    expect(getStepBeatRange(track, 0)).toEqual({
      startBeat: 0,
      endBeat: 8,
      lengthBeats: 8,
    });
    expect(getStepBeatRange(track, 1)).toEqual({
      startBeat: 8,
      endBeat: 24,
      lengthBeats: 16,
    });
    expect(getStepBeatRange(track, 2)).toBeNull();
  });

  it('formats duration in seconds', () => {
    expect(formatDurationSeconds(4, 120)).toBe('2.00s');
    expect(formatDurationSeconds(8, 120)).toBe('4.00s');
    expect(formatDurationSeconds(4, 60)).toBe('4.00s');
  });
});
