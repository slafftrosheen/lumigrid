import { describe, it, expect } from 'vitest';
import { msToSeconds, secondsToMs, formatCountdown } from './timing';
import { MAX_TIMING_LIMITS } from '$lib/components/cues/cueCardTiming';

describe('cue seconds editing (LG-034 §49)', () => {
  it('edits seconds, serializes exact ms', () => {
    expect(msToSeconds(3000)).toBe('3');
    expect(msToSeconds(1200)).toBe('1.2');
    expect(msToSeconds(0)).toBe('0');
    expect(secondsToMs(3, MAX_TIMING_LIMITS.holdMs)).toBe(3000);
    expect(secondsToMs(1.2, MAX_TIMING_LIMITS.fadeMs)).toBe(1200);
    expect(secondsToMs(0.1, MAX_TIMING_LIMITS.holdMs)).toBe(100);
    // 0.1s precision preserved
    expect(secondsToMs(2.9, MAX_TIMING_LIMITS.holdMs)).toBe(2900);
  });

  it('clamps to firmware bounds', () => {
    expect(secondsToMs(-5, MAX_TIMING_LIMITS.holdMs)).toBe(0);
    expect(secondsToMs(99999, MAX_TIMING_LIMITS.holdMs)).toBe(MAX_TIMING_LIMITS.holdMs);
    expect(secondsToMs(999, MAX_TIMING_LIMITS.fadeMs)).toBe(MAX_TIMING_LIMITS.fadeMs);
    expect(secondsToMs(NaN, 1000)).toBe(0);
  });

  it('formats countdowns readably', () => {
    expect(formatCountdown(0)).toBe('0.0 s');
    expect(formatCountdown(1420)).toBe('1.4 s');
    expect(formatCountdown(61000)).toBe('1m 01 s');
  });
});
