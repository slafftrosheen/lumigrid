/**
 * ManualSession — Extracted session state machine for Live Desk manual control.
 *
 * This module owns the lifecycle of a manual override lease:
 * idle → acquiring → active → (lost | releasing | error) → idle
 *
 * The Svelte page renders state and forwards user events.
 * All ownership decisions are server-authoritative via telemetry.
 */

import {
  putManualState,
  releaseManualState,
  buildManualSnapshot,
  channelsToMask,
  type ManualDeskChannel,
  type ManualChannelValue,
} from '$lib/api/manual';
import { ApiError } from '$lib/api/transport';

// ── Types ────────────────────────────────────────────────────────────────────

export type ManualSessionState =
  | 'idle'
  | 'acquiring'
  | 'active'
  | 'lost'
  | 'releasing'
  | 'error';

export interface ManualSessionSnapshot {
  state: ManualSessionState;
  controlledIndices: number[];
  requestedMask: number;
  leaseRemainingSec: number;
  errorMessage: string | null;
}

export interface ManualTelemetry {
  active: boolean;
  channelMask: number;
  leaseRemainingMs: number;
}

export type SessionChangeCallback = (snapshot: ManualSessionSnapshot) => void;

// ── Error classification ─────────────────────────────────────────────────────

export type ManualErrorKind =
  | 'disconnected'
  | 'timeout'
  | 'validation'        // 400
  | 'authorization'     // 401/403
  | 'server'            // 5xx
  | 'mask_mismatch'
  | 'rejected'
  | 'network'
  | 'lease_lost';

export interface ManualSessionError {
  kind: ManualErrorKind;
  message: string;
}

export function classifyError(err: unknown): ManualSessionError {
  if (err instanceof ApiError) {
    if (err.status === 400) return { kind: 'validation', message: err.message };
    if (err.status === 401 || err.status === 403) return { kind: 'authorization', message: err.message };
    if (err.status >= 500) return { kind: 'server', message: err.message };
    return { kind: 'network', message: err.message };
  }
  if (err instanceof Error) {
    const msg = err.message.toLowerCase();
    if (msg.includes('abort') || msg.includes('timeout')) return { kind: 'timeout', message: err.message };
    if (msg.includes('fetch') || msg.includes('network')) return { kind: 'disconnected', message: err.message };
    return { kind: 'network', message: err.message };
  }
  return { kind: 'network', message: String(err) };
}

// ── ManualSession ────────────────────────────────────────────────────────────

export class ManualSession {
  private _state: ManualSessionState = 'idle';
  private _controlledIndices: number[] = [];
  private _requestedMask = 0;
  private _leaseRemainingSec = 0;
  private _errorMessage: string | null = null;

  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private countdownTimer: ReturnType<typeof setInterval> | null = null;
  private onChange: SessionChangeCallback | null = null;

  /** Minimum heartbeat repeat interval (ms). */
  private static readonly HEARTBEAT_MS = 5000;
  /** Default lease duration (ms). */
  private static readonly LEASE_MS = 15_000;

  constructor(onChange?: SessionChangeCallback) {
    this.onChange = onChange ?? null;
  }

  // ── Public accessors ─────────────────────────────────────────────────────

  get state(): ManualSessionState { return this._state; }
  get controlledIndices(): number[] { return [...this._controlledIndices]; }
  get requestedMask(): number { return this._requestedMask; }
  get leaseRemainingSec(): number { return this._leaseRemainingSec; }
  get errorMessage(): string | null { return this._errorMessage; }
  get isActive(): boolean { return this._state === 'active'; }

  snapshot(): ManualSessionSnapshot {
    return {
      state: this._state,
      controlledIndices: [...this._controlledIndices],
      requestedMask: this._requestedMask,
      leaseRemainingSec: this._leaseRemainingSec,
      errorMessage: this._errorMessage,
    };
  }

  // ── Acquire ──────────────────────────────────────────────────────────────

  /**
   * Attempt to acquire manual control for exactly the given `selectedIndices`.
   *
   * Pre-conditions:
   *   - `nodeIp` is non-empty
   *   - At least one selected channel is enabled
   *
   * On success the session enters 'active' and heartbeat starts.
   * On failure the session enters 'error', heartbeat never starts.
   */
  async acquire(
    nodeIp: string,
    channels: ManualDeskChannel[],
    selectedIndices: number[],
  ): Promise<boolean> {
    if (!nodeIp) {
      this.transitionError('disconnected', 'No node connected.');
      return false;
    }

    const payload = buildManualSnapshot(channels, selectedIndices);
    if (payload.length === 0) {
      this.transitionError('validation', 'No enabled channels selected.');
      return false;
    }

    const mask = channelsToMask(payload.map(ch => ch.index));
    this.transition('acquiring');

    try {
      const res = await putManualState(nodeIp, payload, ManualSession.LEASE_MS);

      if (!res.accepted) {
        this.transitionError('rejected', 'Manual control request was rejected by the node.');
        await this.bestEffortRelease(nodeIp);
        return false;
      }

      if (res.channelMask !== mask) {
        this.transitionError(
          'mask_mismatch',
          `Channel mask mismatch: requested 0x${mask.toString(16)}, got 0x${res.channelMask.toString(16)}.`,
        );
        await this.bestEffortRelease(nodeIp);
        return false;
      }

      // Acquisition confirmed — enter active state
      this._controlledIndices = payload.map(ch => ch.index);
      this._requestedMask = mask;
      this._leaseRemainingSec = Math.ceil(ManualSession.LEASE_MS / 1000);
      this.transition('active');
      this.startHeartbeat(nodeIp, channels);
      return true;
    } catch (err) {
      const classified = classifyError(err);
      this.transitionError(classified.kind, classified.message);
      await this.bestEffortRelease(nodeIp);
      return false;
    }
  }

  // ── Release ──────────────────────────────────────────────────────────────

  async release(nodeIp: string): Promise<void> {
    this.stopHeartbeat();
    this.transition('releasing');

    try {
      if (nodeIp) {
        await releaseManualState(nodeIp);
      }
    } catch {
      // Best-effort on release — don't block transition
    }

    this._controlledIndices = [];
    this._requestedMask = 0;
    this._leaseRemainingSec = 0;
    this._errorMessage = null;
    this.transition('idle');
  }

  // ── Update Targets (explicit scope change while active) ──────────────────

  /**
   * Replace the owned channel mask with a new set of selected indices.
   *
   * This sends a complete replacement snapshot because PUT is replacement
   * semantics.  An empty target set triggers release instead of an invalid
   * empty PUT.
   */
  async updateTargets(
    nodeIp: string,
    channels: ManualDeskChannel[],
    newIndices: number[],
  ): Promise<boolean> {
    if (this._state !== 'active') return false;

    const payload = buildManualSnapshot(channels, newIndices);
    if (payload.length === 0) {
      // Empty target set → release
      await this.release(nodeIp);
      return true;
    }

    const mask = channelsToMask(payload.map(ch => ch.index));

    try {
      const res = await putManualState(nodeIp, payload, ManualSession.LEASE_MS);

      if (!res.accepted) {
        this.failClosed(nodeIp, 'rejected', 'Target update rejected by the node.');
        return false;
      }

      if (res.channelMask !== mask) {
        this.failClosed(
          nodeIp,
          'mask_mismatch',
          `Channel mask mismatch on update: requested 0x${mask.toString(16)}, got 0x${res.channelMask.toString(16)}.`,
        );
        return false;
      }

      this._controlledIndices = payload.map(ch => ch.index);
      this._requestedMask = mask;
      this._leaseRemainingSec = Math.ceil(ManualSession.LEASE_MS / 1000);
      this.notify();
      return true;
    } catch (err) {
      const classified = classifyError(err);
      this.failClosed(nodeIp, classified.kind, classified.message);
      return false;
    }
  }

  // ── Telemetry feedback ───────────────────────────────────────────────────

  /**
   * Called by the Svelte page whenever new telemetry arrives.
   *
   * Server state wins.  If firmware reports manual inactive while we believe
   * we are active, the session transitions to 'lost'.
   * It does NOT automatically re-acquire.
   */
  onTelemetry(telemetry: ManualTelemetry | undefined | null): void {
    if (!telemetry) return;

    if (this._state === 'active') {
      if (telemetry.active) {
        // Update lease countdown from authoritative source
        this._leaseRemainingSec = Math.max(0, Math.ceil(telemetry.leaseRemainingMs / 1000));
        this.notify();
      } else {
        // Server says manual is no longer active — lease lost
        this.stopHeartbeat();
        this._controlledIndices = [];
        this._requestedMask = 0;
        this._leaseRemainingSec = 0;
        this._errorMessage = 'Manual control expired or was released.';
        this.transition('lost');
      }
    }
  }

  // ── Component teardown ───────────────────────────────────────────────────

  /**
   * Stop timers and best-effort release on component destroy or node change.
   */
  destroy(nodeIp: string): void {
    this.stopHeartbeat();
    if ((this._state === 'active' || this._state === 'acquiring') && nodeIp) {
      releaseManualState(nodeIp).catch(() => {});
    }
    this._state = 'idle';
    this._controlledIndices = [];
    this._requestedMask = 0;
    this._leaseRemainingSec = 0;
    this._errorMessage = null;
  }

  // ── Heartbeat ────────────────────────────────────────────────────────────

  private startHeartbeat(nodeIp: string, channels: ManualDeskChannel[]): void {
    this.stopHeartbeat();

    this.heartbeatTimer = setInterval(() => {
      if (this._state !== 'active' || !nodeIp) return;
      this.heartbeatTick(nodeIp, channels);
    }, ManualSession.HEARTBEAT_MS);

    this.countdownTimer = setInterval(() => {
      if (this._state === 'active' && this._leaseRemainingSec > 0) {
        this._leaseRemainingSec -= 1;
        this.notify();
      }
    }, 1000);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    if (this.countdownTimer) {
      clearInterval(this.countdownTimer);
      this.countdownTimer = null;
    }
  }

  /**
   * Heartbeat tick: repeats PUT with the SAME controlledIndices — not the
   * current UI selection.  Any failure is treated as ownership-uncertain
   * and fails closed.
   */
  private async heartbeatTick(
    nodeIp: string,
    channels: ManualDeskChannel[],
  ): Promise<void> {
    if (this._state !== 'active') return;

    const payload = buildManualSnapshot(channels, this._controlledIndices);
    if (payload.length === 0) {
      this.failClosed(nodeIp, 'validation', 'Controlled channels became disabled during heartbeat.');
      return;
    }

    try {
      const res = await putManualState(nodeIp, payload, ManualSession.LEASE_MS);

      if (!res.accepted) {
        this.failClosed(nodeIp, 'rejected', 'Heartbeat renewal rejected.');
        return;
      }

      this._leaseRemainingSec = Math.ceil(ManualSession.LEASE_MS / 1000);
      this.notify();
    } catch (err) {
      const classified = classifyError(err);
      this.failClosed(nodeIp, classified.kind, `Heartbeat failed: ${classified.message}`);
    }
  }

  // ── Internal state transitions ───────────────────────────────────────────

  private transition(state: ManualSessionState): void {
    this._state = state;
    this.notify();
  }

  private transitionError(kind: ManualErrorKind, message: string): void {
    this.stopHeartbeat();
    this._state = 'error';
    this._errorMessage = message;
    this._controlledIndices = [];
    this._requestedMask = 0;
    this._leaseRemainingSec = 0;
    this.notify();
  }

  /**
   * Fail closed: stop heartbeat, mark session as lost/error, clear ownership
   * assumption, best-effort DELETE.
   */
  private failClosed(nodeIp: string, kind: ManualErrorKind, message: string): void {
    this.stopHeartbeat();
    this._state = kind === 'lease_lost' ? 'lost' : 'error';
    this._errorMessage = message;
    this._controlledIndices = [];
    this._requestedMask = 0;
    this._leaseRemainingSec = 0;
    this.notify();
    this.bestEffortRelease(nodeIp);
  }

  private async bestEffortRelease(nodeIp: string): Promise<void> {
    if (!nodeIp) return;
    try {
      await releaseManualState(nodeIp);
    } catch {
      // Best effort — swallow
    }
  }

  private notify(): void {
    if (this.onChange) {
      this.onChange(this.snapshot());
    }
  }
}
