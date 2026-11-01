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
  clampChannelValue,
  type ManualDeskChannel,
  type ManualChannelValue,
} from '$lib/api/manual';
import { ApiError } from '$lib/api/transport';
import { nodeAuthStore } from '$lib/auth/nodeAuth';

// ── Types ────────────────────────────────────────────────────────────────────

export type ManualSessionState =
  | 'idle'
  | 'acquiring'
  | 'active'
  | 'updating'
  | 'lost'
  | 'releasing'
  | 'error';

export interface ManualSessionSnapshot {
  state: ManualSessionState;
  controlledIndices: number[];
  requestedMask: number;
  leaseRemainingSec: number;
  errorMessage: string | null;
  confirmed: boolean;
  sending: boolean;
  acceptedValues: ManualChannelValue[];
  releaseUnconfirmed: boolean;
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

interface ManualOwner {
  ip: string;
  generation: number;
  session: ReturnType<typeof nodeAuthStore.getSession>;
  isCurrent: () => boolean;
}

type CaptureOwner = (ip: string) => { ip: string; isCurrent: () => boolean };

export class ManualSession {
  private _state: ManualSessionState = 'idle';
  private _controlledIndices: number[] = [];
  private _requestedMask = 0;
  private _leaseRemainingSec = 0;
  private _errorMessage: string | null = null;
  private _confirmed = false;
  private _sending = false;
  private writesPending = 0;
  private _acceptedValues: ManualChannelValue[] = [];
  private _releaseUnconfirmed = false;
  private leaseDeadline = 0;
  private generation = 0;
  private owner: ManualOwner | null = null;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private countdownTimer: ReturnType<typeof setInterval> | null = null;
  private requestTail: Promise<void> = Promise.resolve();
  private pendingEdit: ManualChannelValue[] | null = null;
  private editDrain: Promise<boolean> | null = null;
  private static readonly LEASE_MS = 15_000;

  constructor(
    private onChange?: SessionChangeCallback,
    private captureOwner: CaptureOwner = ip => ({ ip, isCurrent: () => true }),
  ) {}

  get state(): ManualSessionState { return this._state; }
  get controlledIndices(): number[] { return [...this._controlledIndices]; }
  get requestedMask(): number { return this._requestedMask; }
  get leaseRemainingSec(): number { return this._leaseRemainingSec; }
  get errorMessage(): string | null { return this._errorMessage; }
  get isActive(): boolean { return this._state === 'active'; }

  snapshot(): ManualSessionSnapshot {
    return { state: this._state, controlledIndices: [...this._controlledIndices],
      requestedMask: this._requestedMask, leaseRemainingSec: this._leaseRemainingSec,
      errorMessage: this._errorMessage, confirmed: this._confirmed, sending: this._sending,
      acceptedValues: this._acceptedValues.map(value => ({ ...value })), releaseUnconfirmed: this._releaseUnconfirmed };
  }

  private current(owner: ManualOwner): boolean {
    return owner === this.owner && owner.generation === this.generation && owner.isCurrent() &&
      nodeAuthStore.getSession(owner.ip) === owner.session;
  }

  /** One request at a time; teardown cleanup is queued before any new acquisition. */
  private enqueue<T>(owner: ManualOwner, action: () => Promise<T>, cleanup = false): Promise<T | undefined> {
    const request = this.requestTail.then(async () => {
      if (cleanup ? nodeAuthStore.getSession(owner.ip) !== owner.session : !this.current(owner)) return undefined;
      return action();
    });
    this.requestTail = request.then(() => {}, () => {});
    return request;
  }

  private stopRequests(): void {
    ++this.generation;
    this.stopHeartbeat();
    // A sent PUT may already be applying on the node. Keep its bounded request
    // ahead of cleanup instead of mistaking client abort for remote rollback.
    this.pendingEdit = null;
    this.editDrain = null;
    this.writesPending = 0;
    this._sending = false;
  }

  private clearScope(): void {
    this._controlledIndices = [];
    this._requestedMask = 0;
    this._leaseRemainingSec = 0;
    this._confirmed = false;
  }

  async acquire(nodeIp: string, channels: ManualDeskChannel[], selectedIndices: number[]): Promise<boolean> {
    if (this._state === 'acquiring' || this._state === 'updating' || this._state === 'releasing' || this._state === 'active') return false;
    if (!nodeIp) { this._state = 'error'; this._errorMessage = 'No node selected.'; this.notify(); return false; }
    const captured = this.captureOwner(nodeIp);
    if (!captured.isCurrent()) return false;
    const payload = buildManualSnapshot(channels, selectedIndices);
    if (!payload.length) { this._state = 'error'; this._errorMessage = 'No enabled channels selected.'; this.notify(); return false; }
    this.stopRequests();
    const owner: ManualOwner = { ...captured, generation: this.generation, session: nodeAuthStore.getSession(nodeIp) };
    this.owner = owner;
    this._errorMessage = null;
    this._releaseUnconfirmed = false;
    this._acceptedValues = [];
    this._confirmed = false;
    this.transition('acquiring');
    return this.submit(owner, payload, true);
  }

  /** The receipt confirms a submitted lease, not the actual electrical output. */
  private async submit(owner: ManualOwner, payload: ManualChannelValue[], setTargets = false, context = 'Manual update'): Promise<boolean> {
    payload = payload.map(clampChannelValue);
    const mask = channelsToMask(payload.map(value => value.index));
    ++this.writesPending;
    this._sending = true; this.notify();
    try {
      const response = await this.enqueue(owner, () => putManualState(owner.ip, payload, ManualSession.LEASE_MS));
      if (!this.current(owner) || !response) return false;
      if (response.accepted !== true) throw new Error(`${context} rejected by the node.`);
      if (response.channelMask !== mask) throw new Error(`Channel mask mismatch: requested 0x${mask.toString(16)}, received ${String(response.channelMask)}.`);
      if (!Number.isFinite(response.leaseMs) || response.leaseMs < 1000 || response.leaseMs > 60000) throw new Error('The node returned an invalid manual lease duration.');
      if (setTargets) {
        this._controlledIndices = payload.map(value => value.index);
        this._requestedMask = mask;
        this._confirmed = false;
      }
      this._acceptedValues = payload.map(value => ({ ...value }));
      this.leaseDeadline = Date.now() + response.leaseMs;
      this._leaseRemainingSec = Math.ceil(response.leaseMs / 1000);
      if (setTargets || this._state !== 'updating') {
        this.transition('active');
        this.startHeartbeat(owner, response.leaseMs);
      } else this.notify();
      return true;
    } catch (error) {
      if (!this.current(owner)) return false;
      const classified = classifyError(error);
      await this.failClosed(owner, 'error', `${context} failed: ${classified.message}`);
      return false;
    } finally {
      if (this.current(owner)) {
        this.writesPending = Math.max(0, this.writesPending - 1);
        this._sending = this.writesPending > 0;
        this.notify();
      }
    }
  }

  async updateTargets(nodeIp: string, channels: ManualDeskChannel[], newIndices: number[]): Promise<boolean> {
    const owner = this.owner;
    if (this._state !== 'active' || !owner || owner.ip !== nodeIp || !this.current(owner)) return false;
    const payload = buildManualSnapshot(channels, newIndices);
    if (!payload.length) return this.release(nodeIp);
    this.pendingEdit = null;
    this.stopHeartbeat();
    this.transition('updating');
    return this.submit(owner, payload, true, 'Target update');
  }

  /** Coalesce edits while one PUT is in flight; never enqueue an old draft per pointer event. */
  sendValues(channels: ManualDeskChannel[]): Promise<boolean> {
    const owner = this.owner;
    if (this._state !== 'active' || !owner || !this.current(owner)) return Promise.resolve(false);
    const payload = buildManualSnapshot(channels, this._controlledIndices);
    if (!payload.length) return this.failClosed(owner, 'error', 'Controlled channels are unavailable.').then(() => false);
    this.pendingEdit = payload;
    if (this.editDrain) return this.editDrain;
    const drain = this.drainEdits(owner);
    this.editDrain = drain;
    void drain.finally(() => { if (this.editDrain === drain) this.editDrain = null; });
    return drain;
  }

  private async drainEdits(owner: ManualOwner): Promise<boolean> {
    let accepted = false;
    while (this.current(owner) && this._state === 'active' && this.pendingEdit) {
      const payload = this.pendingEdit;
      this.pendingEdit = null;
      accepted = await this.submit(owner, payload);
      if (!accepted) return false;
    }
    return accepted;
  }

  async release(nodeIp: string): Promise<boolean> {
    const previous = this.owner;
    if (!previous || previous.ip !== nodeIp || !this.current(previous)) return false;
    this.stopRequests();
    const owner = { ...previous, generation: this.generation };
    this.owner = owner;
    this.transition('releasing');
    try {
      const response = await this.enqueue(owner, () => releaseManualState(owner.ip));
      if (!this.current(owner)) return false;
      if (response?.released !== true) throw new Error('The node did not acknowledge manual release.');
      this.clearScope();
      this._acceptedValues = [];
      this._errorMessage = null;
      this._releaseUnconfirmed = false;
      this.transition('idle');
      return true;
    } catch (error) {
      if (!this.current(owner)) return false;
      this.clearScope();
      this._releaseUnconfirmed = true;
      this._errorMessage = `Release not confirmed. Renewal stopped; the last values may remain until the lease expires. ${classifyError(error).message}`;
      this.transition('error');
      return false;
    }
  }

  onTelemetry(telemetry: ManualTelemetry | undefined | null): void {
    const owner = this.owner;
    if (!telemetry || this._state !== 'active' || !owner || !this.current(owner)) return;
    if (!telemetry.active || telemetry.leaseRemainingMs <= 0) {
      void this.failClosed(owner, 'lost', 'Manual control expired or was released.', false);
    } else if (telemetry.channelMask !== this._requestedMask) {
      // A different operator may now own the global manual frame. Do not DELETE it.
      void this.failClosed(owner, 'lost', 'Manual targets changed on the node. Renewal stopped; re-acquire explicitly.', false);
    } else {
      this._confirmed = true;
      this.leaseDeadline = Date.now() + telemetry.leaseRemainingMs;
      this._leaseRemainingSec = Math.ceil(telemetry.leaseRemainingMs / 1000);
      this.notify();
    }
  }

  cancel(reason: string): void {
    if (this.owner && ['active', 'acquiring', 'updating'].includes(this._state)) {
      void this.failClosed(this.owner, 'lost', `${reason} Renewal stopped; re-acquire explicitly.`);
    }
  }

  destroy(_nodeIp: string): void {
    const previous = this.owner;
    const mayHoldLease = this._releaseUnconfirmed || ['active', 'acquiring', 'updating', 'releasing'].includes(this._state);
    this.stopRequests();
    this.owner = null;
    this.clearScope();
    this._acceptedValues = [];
    this._errorMessage = null;
    this._releaseUnconfirmed = false;
    this._state = 'idle';
    this.notify();
    if (previous && mayHoldLease) void this.bestEffortRelease(previous);
  }

  private startHeartbeat(owner: ManualOwner, leaseMs: number): void {
    this.stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (!this.current(owner) || this._state !== 'active' || this._sending || this.pendingEdit || this.editDrain) return;
      void this.submit(owner, this._acceptedValues.map(value => ({ ...value })), false, 'Heartbeat');
    }, Math.min(5000, Math.max(250, Math.floor(leaseMs / 3))));
    this.countdownTimer = setInterval(() => {
      if (!this.current(owner) || this._state !== 'active') return;
      this._leaseRemainingSec = Math.max(0, Math.ceil((this.leaseDeadline - Date.now()) / 1000));
      if (this._leaseRemainingSec === 0) {
        void this.failClosed(owner, 'lost', 'Manual lease expired. Re-acquire explicitly.', false);
      } else this.notify();
    }, 1000);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    if (this.countdownTimer) clearInterval(this.countdownTimer);
    this.heartbeatTimer = this.countdownTimer = null;
  }

  private async failClosed(owner: ManualOwner, state: 'lost' | 'error', message: string, cleanup = true): Promise<void> {
    this.stopRequests();
    // Keep a stopped owner only while its cleanup can still be reported/retried
    // in this workspace. Its generation cannot accept any canceled PUT result.
    const stoppedOwner = { ...owner, generation: this.generation };
    this.owner = cleanup ? stoppedOwner : null;
    this.clearScope();
    this._releaseUnconfirmed = false;
    this._errorMessage = cleanup
      ? `${message} Release confirmation pending; the last values may remain until the lease expires.`
      : message;
    this.transition(state);
    if (!cleanup) return;
    const released = await this.bestEffortRelease(stoppedOwner);
    if (!this.current(stoppedOwner)) return;
    this._releaseUnconfirmed = !released;
    this._errorMessage = released
      ? `${message} Manual release acknowledged.`
      : `${message} Release not confirmed. The last values may remain until the lease expires.`;
    if (released) {
      this.owner = null;
      this._acceptedValues = [];
    }
    this.notify();
  }

  private async bestEffortRelease(owner: ManualOwner): Promise<boolean> {
    try {
      const response = await this.enqueue(owner, () => releaseManualState(owner.ip), true);
      return response?.released === true;
    } catch { return false; }
  }

  private transition(state: ManualSessionState): void { this._state = state; this.notify(); }
  private notify(): void { this.onChange?.(this.snapshot()); }
}
