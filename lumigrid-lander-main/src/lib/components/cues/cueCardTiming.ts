/** Timing limits shared by cue card editors (§33 firmware bounds). */
export const MAX_TIMING_LIMITS = {
  holdMs: 3_600_000,
  fadeMs: 60_000,
} as const;

export function msToSeconds(ms: number): string {
  const seconds = ms / 1000;
  return Number.isInteger(seconds) ? seconds.toFixed(0) : seconds.toFixed(1);
}

export function secondsToMs(seconds: number, maxMs: number): number {
  if (!Number.isFinite(seconds) || seconds < 0) return 0;
  return Math.min(Math.round(seconds * 1000), maxMs);
}
