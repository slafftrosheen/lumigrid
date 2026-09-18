/**
 * Fixed-point Q15 curves and automation track sampling matching firmware algorithms in:
 * - SceneTransition.cpp (smoothstep_q15, apply_curve_q15)
 * - AutomationEngine.cpp (sample_track)
 */

export type TransitionCurve = 'linear' | 'easeIn' | 'easeOut' | 'easeInOut' | 'step';

export const TRANSITION_CURVES: TransitionCurve[] = [
  'linear',
  'easeIn',
  'easeOut',
  'easeInOut',
  'step',
];

export function curveNameToId(name: TransitionCurve): number {
  switch (name) {
    case 'linear': return 0;
    case 'easeIn': return 1;
    case 'easeOut': return 2;
    case 'easeInOut': return 3;
    case 'step': return 4;
    default: return 0;
  }
}

export function curveIdToName(id: number): TransitionCurve {
  switch (id) {
    case 0: return 'linear';
    case 1: return 'easeIn';
    case 2: return 'easeOut';
    case 3: return 'easeInOut';
    case 4: return 'step';
    default: return 'linear';
  }
}

/**
 * Firmware smoothstep_q15 equivalent.
 * t: 0..32768
 * returns: 0..32768
 */
export function smoothstepQ15(t: number): number {
  if (t >= 32768) return 32768;
  if (t <= 0) return 0;
  const x = Math.floor(t);
  const x2 = Math.floor((x * x) / 32768);
  const x3 = Math.floor((x2 * x) / 32768);
  return Math.min(32768, Math.max(0, 3 * x2 - 2 * x3));
}

/**
 * Firmware apply_curve_q15 equivalent.
 * t_q15: 0..32768
 * returns: 0..32768
 */
export function applyCurveQ15(curve: TransitionCurve | number, t_q15: number): number {
  if (t_q15 >= 32768) return 32768;
  if (t_q15 <= 0) return 0;

  const curveName = typeof curve === 'number' ? curveIdToName(curve) : curve;

  switch (curveName) {
    case 'linear':
      return t_q15;
    case 'easeIn': {
      const x = Math.floor(t_q15);
      return Math.floor((x * x) / 32768);
    }
    case 'easeOut': {
      const inv = 32768 - Math.floor(t_q15);
      const inv2 = Math.floor((inv * inv) / 32768);
      return 32768 - inv2;
    }
    case 'easeInOut':
      return smoothstepQ15(t_q15);
    case 'step':
      return t_q15 < 16384 ? 0 : 32768;
    default:
      return t_q15;
  }
}

/**
 * Normalized 0..1 curve evaluation mapped directly through Q15 math
 * ensuring exact parity between UI and firmware.
 */
export function sampleCurveNormalized(curve: TransitionCurve | number, t: number): number {
  const clamped = Math.max(0, Math.min(1, t));
  const t_q15 = Math.round(clamped * 32768);
  const out_q15 = applyCurveQ15(curve, t_q15);
  return out_q15 / 32768;
}

export interface AutomationPoint {
  position: number; // 0..65535
  value: number;    // 0..65535
}

export interface AutomationTrack {
  target: number;   // 0..4
  curve: number;    // TransitionCurve ID 0..4
  periodMs: number;
  loop: boolean;
  points: AutomationPoint[];
}

/**
 * Firmware AutomationEngine::sample_track equivalent in TypeScript.
 */
export function sampleAutomationTrack(track: AutomationTrack, elapsedMs: number): number {
  if (!track.points || track.points.length === 0) return 0;
  if (track.points.length === 1) return track.points[0].value;

  const period = track.periodMs === 0 ? 5000 : track.periodMs;
  const elapsedInPeriod = track.loop ? (elapsedMs % period) : Math.min(elapsedMs, period);
  const posNorm = Math.floor((elapsedInPeriod * 65535) / period);

  const points = track.points.slice(0, 6);
  const count = points.length;

  if (posNorm <= points[0].position) {
    if (!track.loop || points[count - 1].position <= points[0].position) {
      return points[0].value;
    }
    const wrapSpan = (65535 - points[count - 1].position) + points[0].position;
    if (wrapSpan === 0) return points[0].value;
    const offset = (65535 - points[count - 1].position) + posNorm;
    const t_q15 = Math.floor((offset * 32768) / wrapSpan);
    const curved_t = applyCurveQ15(track.curve, t_q15);
    return Math.floor(points[count - 1].value + ((points[0].value - points[count - 1].value) * curved_t) / 32768);
  }

  if (posNorm >= points[count - 1].position) {
    if (!track.loop || points[count - 1].position >= 65535) {
      return points[count - 1].value;
    }
    const wrapSpan = (65535 - points[count - 1].position) + points[0].position;
    if (wrapSpan === 0) return points[count - 1].value;
    const offset = posNorm - points[count - 1].position;
    const t_q15 = Math.floor((offset * 32768) / wrapSpan);
    const curved_t = applyCurveQ15(track.curve, t_q15);
    return Math.floor(points[count - 1].value + ((points[0].value - points[count - 1].value) * curved_t) / 32768);
  }

  for (let i = 0; i < count - 1; ++i) {
    if (posNorm >= points[i].position && posNorm <= points[i + 1].position) {
      const span = points[i + 1].position - points[i].position;
      if (span === 0) return points[i].value;
      const offset = posNorm - points[i].position;
      const t_q15 = Math.floor((offset * 32768) / span);
      const curved_t = applyCurveQ15(track.curve, t_q15);
      return Math.floor(points[i].value + ((points[i + 1].value - points[i].value) * curved_t) / 32768);
    }
  }

  return points[count - 1].value;
}
