import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  ManualSession,
  classifyError,
  type ManualSessionSnapshot,
} from './manualSession';
import type { ManualDeskChannel } from '$lib/api/manual';
import * as transport from '$lib/api/transport';
import { ApiError } from '$lib/api/transport';

function makeChannels(): ManualDeskChannel[] {
  const channels: ManualDeskChannel[] = [];
  for (let i = 0; i < 8; i++) {
    channels.push({
      index: i,
      kind: 'addressable',
      enabled: true,
      r: 255,
      g: 64,
      b: 32,
      brightness: 200,
      pwm: 0,
    });
  }
  for (let i = 8; i < 16; i++) {
    channels.push({
      index: i,
      kind: 'pwm',
      enabled: true,
      r: 0,
      g: 0,
      b: 0,
      brightness: 255,
      pwm: 2048,
    });
  }
  return channels;
}

describe('ManualSession state machine', () => {
  const NODE_IP = '192.168.1.100';

  beforeEach(() => {
    vi.restoreAllMocks();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('select A1 -> Take -> only A1 sent in snapshot payload', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    const ok = await session.acquire(NODE_IP, channels, [0]);

    expect(ok).toBe(true);
    expect(session.state).toBe('active');
    expect(session.controlledIndices).toEqual([0]);
    expect(session.requestedMask).toBe(0x0001);

    expect(apiJsonSpy).toHaveBeenCalledTimes(1);
    const callBody = JSON.parse((apiJsonSpy.mock.calls[0][2] as { body: string }).body);
    expect(callBody.channels).toHaveLength(1);
    expect(callBody.channels[0].index).toBe(0);
    expect(callBody.channels[0].r).toBe(255);
    expect('pwm' in callBody.channels[0]).toBe(false);

    session.destroy(NODE_IP);
  });

  it('select group A1+A2+P1 -> exact three-channel snapshot with mask 0x0103', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0103,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    const ok = await session.acquire(NODE_IP, channels, [0, 1, 8]);

    expect(ok).toBe(true);
    expect(session.state).toBe('active');
    expect(session.controlledIndices).toEqual([0, 1, 8]);
    expect(session.requestedMask).toBe(0x0103);

    const callBody = JSON.parse((apiJsonSpy.mock.calls[0][2] as { body: string }).body);
    expect(callBody.channels).toHaveLength(3);
    expect(callBody.channels.map((c: { index: number }) => c.index)).toEqual([0, 1, 8]);
    expect(callBody.channels[2].pwm).toBe(2048);

    session.destroy(NODE_IP);
  });

  it('successful API response -> heartbeat starts and repeats after 5s', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(apiJsonSpy).toHaveBeenCalledTimes(1);

    // Advance 5 seconds — heartbeat triggers
    await vi.advanceTimersByTimeAsync(5000);
    expect(apiJsonSpy).toHaveBeenCalledTimes(2);

    // Advance another 5 seconds
    await vi.advanceTimersByTimeAsync(5000);
    expect(apiJsonSpy).toHaveBeenCalledTimes(3);

    session.destroy(NODE_IP);
  });

  it('failed acquire -> heartbeat never starts', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockRejectedValue(
      new ApiError(500, '/api/v1/manual', NODE_IP, 'Internal Server Error'),
    );

    const channels = makeChannels();
    const session = new ManualSession();

    const ok = await session.acquire(NODE_IP, channels, [0]);

    expect(ok).toBe(false);
    expect(session.state).toBe('error');
    expect(session.controlledIndices).toEqual([]);

    // Reset spy count (ignoring best-effort DELETE)
    apiJsonSpy.mockClear();

    // Advance 15 seconds — heartbeat should NEVER trigger
    await vi.advanceTimersByTimeAsync(15000);
    expect(apiJsonSpy).not.toHaveBeenCalled();

    session.destroy(NODE_IP);
  });

  it('firmware telemetry reports lease inactive -> heartbeat stops and transitions to lost', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(session.state).toBe('active');

    apiJsonSpy.mockClear();

    // Telemetry reports lease has ended on the device
    session.onTelemetry({
      active: false,
      channelMask: 0,
      leaseRemainingMs: 0,
    });

    expect(session.state).toBe('lost');
    expect(session.controlledIndices).toEqual([]);
    expect(session.errorMessage).toContain('expired or was released');

    // Advance timers — no heartbeat should run
    await vi.advanceTimersByTimeAsync(10000);
    expect(apiJsonSpy).not.toHaveBeenCalled();

    session.destroy(NODE_IP);
  });

  it('lost lease never auto-reacquires', async () => {
    vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);

    // Force lost state via telemetry
    session.onTelemetry({ active: false, channelMask: 0, leaseRemainingMs: 0 });
    expect(session.state).toBe('lost');

    // Subsequent telemetry arrivals must not cause re-acquisition
    session.onTelemetry({ active: false, channelMask: 0, leaseRemainingMs: 0 });
    await vi.advanceTimersByTimeAsync(30000);

    expect(session.state).toBe('lost');
    expect(session.controlledIndices).toEqual([]);

    session.destroy(NODE_IP);
  });

  it('heartbeat repeats same controlled channel mask even if UI state would differ', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0101, // A1 + P1
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0, 8]);
    expect(session.controlledIndices).toEqual([0, 8]);

    apiJsonSpy.mockClear();

    // Heartbeat tick
    await vi.advanceTimersByTimeAsync(5000);

    expect(apiJsonSpy).toHaveBeenCalledTimes(1);
    const body = JSON.parse((apiJsonSpy.mock.calls[0][2] as { body: string }).body);
    expect(body.channels.map((c: { index: number }) => c.index)).toEqual([0, 8]);

    session.destroy(NODE_IP);
  });

  it('editing selection does not silently expand ownership', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001, // A1 only
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(session.controlledIndices).toEqual([0]);

    apiJsonSpy.mockClear();

    // Operator in UI selects A2 and P1, but has NOT clicked Update Targets.
    // The heartbeat must still only renew A1.
    await vi.advanceTimersByTimeAsync(5000);

    expect(apiJsonSpy).toHaveBeenCalledTimes(1);
    const body = JSON.parse((apiJsonSpy.mock.calls[0][2] as { body: string }).body);
    expect(body.channels.map((c: { index: number }) => c.index)).toEqual([0]);

    session.destroy(NODE_IP);
  });

  it('explicit target update replaces mask and controlled indices', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      const opts = options as { method?: string; body?: string };
      if (opts.method === 'PUT') {
        const body = JSON.parse(opts.body || '{}');
        const indices: number[] = (body.channels || []).map((c: { index: number }) => c.index);
        let mask = 0;
        for (const idx of indices) mask |= (1 << idx);
        return { accepted: true, channelMask: mask, leaseMs: 15000 };
      }
      return { released: true };
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(session.controlledIndices).toEqual([0]);
    expect(session.requestedMask).toBe(0x0001);

    // Operator explicitly clicks Update Targets for [1, 8]
    const updated = await session.updateTargets(NODE_IP, channels, [1, 8]);
    expect(updated).toBe(true);
    expect(session.controlledIndices).toEqual([1, 8]);
    expect(session.requestedMask).toBe(0x0102);

    // Next heartbeat repeats the new targets [1, 8]
    apiJsonSpy.mockClear();
    await vi.advanceTimersByTimeAsync(5000);

    expect(apiJsonSpy).toHaveBeenCalledTimes(1);
    const body = JSON.parse((apiJsonSpy.mock.calls[0][2] as { body: string }).body);
    expect(body.channels.map((c: { index: number }) => c.index)).toEqual([1, 8]);

    session.destroy(NODE_IP);
  });

  it('updating targets with empty set triggers clean release', async () => {
    vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(session.state).toBe('active');

    // Updating to empty set releases session
    const ok = await session.updateTargets(NODE_IP, channels, []);
    expect(ok).toBe(true);
    expect(session.state).toBe('idle');
    expect(session.controlledIndices).toEqual([]);
  });

  it('network failure during heartbeat transitions to error and fails closed', async () => {
    let callCount = 0;
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      const opts = options as { method?: string };
      if (opts.method === 'DELETE') {
        return { released: true };
      }
      callCount++;
      if (callCount === 1) {
        return { accepted: true, channelMask: 0x0001, leaseMs: 15000 };
      }
      throw new Error('Network request failed');
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0]);
    expect(session.state).toBe('active');

    // Heartbeat triggers failure
    await vi.advanceTimersByTimeAsync(5000);

    expect(session.state).toBe('error');
    expect(session.controlledIndices).toEqual([]);
    expect(session.errorMessage).toContain('Heartbeat failed');

    // Best-effort release was attempted
    const deleteCalls = apiJsonSpy.mock.calls.filter(
      call => (call[2] as { method?: string })?.method === 'DELETE',
    );
    expect(deleteCalls.length).toBeGreaterThanOrEqual(1);

    session.destroy(NODE_IP);
  });

  it('returned API channel mask mismatch fails closed immediately', async () => {
    const apiJsonSpy = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      const opts = options as { method?: string };
      if (opts.method === 'DELETE') return { released: true };
      // Operator asked for [0, 1] (0x0003), node only returns 0x0001
      return { accepted: true, channelMask: 0x0001, leaseMs: 15000 };
    });

    const channels = makeChannels();
    const session = new ManualSession();

    const ok = await session.acquire(NODE_IP, channels, [0, 1]);

    expect(ok).toBe(false);
    expect(session.state).toBe('error');
    expect(session.errorMessage).toContain('mask mismatch');
    expect(session.controlledIndices).toEqual([]);

    // Best effort release issued
    const deleteCalls = apiJsonSpy.mock.calls.filter(
      call => (call[2] as { method?: string })?.method === 'DELETE',
    );
    expect(deleteCalls.length).toBe(1);

    session.destroy(NODE_IP);
  });

  it('DMX preemption does not falsely mark manual lease expired', async () => {
    vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0003,
      leaseMs: 15000,
    });

    const channels = makeChannels();
    const session = new ManualSession();

    await session.acquire(NODE_IP, channels, [0, 1]);
    expect(session.state).toBe('active');

    // Device telemetry shows manual is still active with 9.5s remaining,
    // even though DMX preemption is happening on the outputs.
    session.onTelemetry({
      active: true,
      channelMask: 0x0003,
      leaseRemainingMs: 9500,
    });

    expect(session.state).toBe('active');
    expect(session.leaseRemainingSec).toBe(10);
    expect(session.controlledIndices).toEqual([0, 1]);

    session.destroy(NODE_IP);
  });

  it('notifies subscribers via callback on state transitions', async () => {
    vi.spyOn(transport, 'apiJson').mockResolvedValue({
      accepted: true,
      channelMask: 0x0001,
      leaseMs: 15000,
    });

    const snapshots: ManualSessionSnapshot[] = [];
    const session = new ManualSession((snap) => snapshots.push(snap));

    const channels = makeChannels();
    await session.acquire(NODE_IP, channels, [0]);

    // Should have transitioned: acquiring -> active
    const states = snapshots.map(s => s.state);
    expect(states).toContain('acquiring');
    expect(states).toContain('active');

    await session.release(NODE_IP);
    expect(snapshots[snapshots.length - 1].state).toBe('idle');
  });
});

describe('classifyError helper', () => {
  it('classifies ApiError 400 as validation', () => {
    const err = new ApiError(400, '/api/v1/manual', '1.2.3.4', 'Invalid body');
    expect(classifyError(err)).toEqual({ kind: 'validation', message: err.message });
  });

  it('classifies ApiError 401 as authorization', () => {
    const err = new ApiError(401, '/api/v1/manual', '1.2.3.4', 'Unauthorized');
    expect(classifyError(err)).toEqual({ kind: 'authorization', message: err.message });
  });

  it('classifies ApiError 500 as server', () => {
    const err = new ApiError(500, '/api/v1/manual', '1.2.3.4', 'Crash');
    expect(classifyError(err)).toEqual({ kind: 'server', message: err.message });
  });

  it('classifies timeout errors', () => {
    const err = new Error('The operation was aborted due to timeout');
    expect(classifyError(err).kind).toBe('timeout');
  });

  it('classifies network disconnect errors', () => {
    const err = new TypeError('Failed to fetch');
    expect(classifyError(err).kind).toBe('disconnected');
  });
});
