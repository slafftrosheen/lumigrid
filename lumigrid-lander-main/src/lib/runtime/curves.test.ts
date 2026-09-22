import { describe, it, expect } from 'vitest';
import {
  applyCurveQ15,
  smoothstepQ15,
  sampleCurveNormalized,
  sampleAutomationTrack,
  curveNameToId,
  curveIdToName,
  type TransitionCurve,
} from './curves';
import curveVectors from './curve_vectors.json';

describe('Q15 curves and automation math', () => {
  it('converts curve names and IDs bidirectionally', () => {
    const curves: TransitionCurve[] = ['linear', 'easeIn', 'easeOut', 'easeInOut', 'step'];
    for (let i = 0; i < curves.length; i++) {
      expect(curveNameToId(curves[i])).toBe(i);
      expect(curveIdToName(i)).toBe(curves[i]);
    }
  });

  it('verifies exact parity against curve_vectors.json for all 25 points across all curves', () => {
    const points = curveVectors.input_t_q15;
    expect(points.length).toBe(25);

    const curves: TransitionCurve[] = ['linear', 'easeIn', 'easeOut', 'easeInOut', 'step'];

    for (const c of curves) {
      const expectedOutputs = (curveVectors.curves as any)[c].output_q15;
      expect(expectedOutputs.length).toBe(25);

      for (let i = 0; i < 25; i++) {
        const input = points[i];
        const actual = applyCurveQ15(c, input);
        const expected = expectedOutputs[i];
        expect(actual).toBe(expected);
      }
    }
  });

  it('smoothstep_q15 matches cubic Hermite boundary and center conditions', () => {
    expect(smoothstepQ15(0)).toBe(0);
    expect(smoothstepQ15(16384)).toBe(16384);
    expect(smoothstepQ15(32768)).toBe(32768);
    expect(smoothstepQ15(40000)).toBe(32768); // Clamped
  });

  it('sampleCurveNormalized maps 0..1 floating point correctly through Q15', () => {
    expect(sampleCurveNormalized('linear', 0)).toBe(0);
    expect(sampleCurveNormalized('linear', 1)).toBe(1);
    expect(sampleCurveNormalized('linear', 0.5)).toBeCloseTo(0.5, 4);

    expect(sampleCurveNormalized('step', 0.2)).toBe(0);
    expect(sampleCurveNormalized('step', 0.8)).toBe(1);

    expect(sampleCurveNormalized('easeIn', 0)).toBe(0);
    expect(sampleCurveNormalized('easeIn', 1)).toBe(1);
    expect(sampleCurveNormalized('easeIn', 0.5)).toBeLessThan(0.5);

    expect(sampleCurveNormalized('easeOut', 0.5)).toBeGreaterThan(0.5);
  });

  it('sampleAutomationTrack samples constant value when only 1 point is defined', () => {
    const track = {
      target: 0,
      curve: 0,
      periodMs: 5000,
      loop: true,
      points: [{ position: 0, value: 42000 }],
    };
    expect(sampleAutomationTrack(track, 0)).toBe(42000);
    expect(sampleAutomationTrack(track, 2500)).toBe(42000);
    expect(sampleAutomationTrack(track, 5000)).toBe(42000);
  });

  it('sampleAutomationTrack interpolates linearly between control points', () => {
    const track = {
      target: 0,
      curve: 0, // Linear
      periodMs: 1000,
      loop: false,
      points: [
        { position: 0, value: 0 },
        { position: 65535, value: 65535 },
      ],
    };

    expect(sampleAutomationTrack(track, 0)).toBe(0);
    expect(sampleAutomationTrack(track, 500)).toBeCloseTo(32768, -2);
    expect(sampleAutomationTrack(track, 1000)).toBe(65535);
  });

  it('sampleAutomationTrack handles loop wrap boundary', () => {
    const track = {
      target: 0,
      curve: 0,
      periodMs: 1000,
      loop: true,
      points: [
        { position: 10000, value: 10000 },
        { position: 50000, value: 50000 },
      ],
    };

    // Before first point and after last point, loops across the wrap boundary
    const vStart = sampleAutomationTrack(track, 0);
    expect(vStart).toBeGreaterThan(0);
  });
});
