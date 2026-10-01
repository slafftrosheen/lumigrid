/**
 * LG-034 Cue timing helpers (§49).
 *
 * Ordinary operators edit seconds, not raw milliseconds. Internal values stay
 * exact milliseconds; 0.1 s precision is preserved on round-trip.
 */

const SECONDS_PRECISION = 1;

/** ms -> seconds string for input fields ('3.0', '1.2', '0'). */
export function msToSeconds(ms: number): string {
  const seconds = ms / 1000;
  return Number.isInteger(seconds) ? seconds.toFixed(0) : seconds.toFixed(SECONDS_PRECISION);
}

/** seconds (possibly fractional) -> exact ms. Clamps to 0..max. */
export function secondsToMs(seconds: number, maxMs: number): number {
  if (!Number.isFinite(seconds) || seconds < 0) return 0;
  const ms = Math.round(seconds * 1000);
  return Math.min(ms, maxMs);
}

/** Display form for runtime countdowns: '1.4 s', '12 s', '1m 03 s'. */
export function formatCountdown(ms: number): string {
  if (!Number.isFinite(ms) || ms < 0) return '0.0 s';
  if (ms < 60_000) {
    return `${(ms / 1000).toFixed(1)} s`;
  }
  const minutes = Math.floor(ms / 60_000);
  const seconds = Math.floor((ms % 60_000) / 1000);
  return `${minutes}m ${String(seconds).padStart(2, '0')} s`;
}

/**
 * Q15 manual fade (0..32768) -> 0..1 for T-Bar rendering (§21).
 */
export function q15ToUnit(q15: number): number {
  if (!Number.isFinite(q15) || q15 <= 0) return 0;
  return Math.min(1, q15 / 32768);
}

/** 0..1 T-Bar position -> Q15 for the wire (§17). */
export function unitToQ15(value: number): number {
  if (!Number.isFinite(value) || value <= 0) return 0;
  return Math.min(32768, Math.round(value * 32768));
}

/**
 * Interpolated elapsed display between authoritative snapshots (§58).
 * Runtime truth remains node-owned: only display timing is extrapolated.
 * Returns null when interpolation must freeze (degraded connection).
 */
export function interpolateElapsed(
  lastElapsedMs: number,
  lastRemainingMs: number,
  state: string,
  snapshotAgeMs: number,
  connectionOk: boolean
): { elapsedMs: number; remainingMs: number } | null {
  if (!connectionOk) return null;
  if (state !== 'holding' && state !== 'fading') return null;
  if (lastRemainingMs <= 0) return null;

  const cappedAge = Math.min(snapshotAgeMs, lastRemainingMs);
  return {
    elapsedMs: lastElapsedMs + cappedAge,
    remainingMs: Math.max(0, lastRemainingMs - cappedAge),
  };
}

/** 10 Hz T-Bar throttle with trailing flush (§20). */
export class ThrottledSender {
  private lastSentAt = 0;
  private pendingValue: number | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;

  constructor(
    private readonly intervalMs = 100,
    private readonly send: (value: number) => Promise<void> | void
  ) {}

  /** Call on every input event. Emits at most every intervalMs. */
  push(value: number): void {
    this.pendingValue = value;
    const now = Date.now();
    const sinceLast = now - this.lastSentAt;
    if (sinceLast >= this.intervalMs) {
      this.flush();
    } else if (this.timer === null) {
      this.timer = setTimeout(() => {
        this.timer = null;
        this.flush();
      }, this.intervalMs - sinceLast);
    }
  }

  /** Immediate send of the latest value (pointer release, §20). */
  flush(): void {
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    if (this.pendingValue === null) return;
    const value = this.pendingValue;
    this.pendingValue = null;
    this.lastSentAt = Date.now();
    void this.send(value);
  }

  cancel(): void {
    if (this.timer !== null) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    this.pendingValue = null;
  }
}
