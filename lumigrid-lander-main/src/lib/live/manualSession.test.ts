import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  ManualSession,
  classifyError,
  type ManualSessionSnapshot,
} from './manualSession';
import type { ManualDeskChannel } from '$lib/api/manual';
import * as transport from '$lib/api/transport';
import { ApiError } from '$lib/api/transport';
import { nodeAuthStore } from '$lib/auth/nodeAuth';

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
    vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => options?.method === 'DELETE' ? { released: true } : {
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
    vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => options?.method === 'DELETE' ? { released: true } : {
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

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

async function settle() { for (let i = 0; i < 12; i++) await Promise.resolve(); }

function receipt(options: transport.ApiFetchOptions | undefined, leaseMs = 15000) {
  if (options?.method === 'DELETE') return { released: true };
  const channels = JSON.parse(String(options?.body)).channels as { index: number }[];
  return { accepted: true, channelMask: channels.reduce((mask, value) => mask | (1 << value.index), 0), leaseMs };
}

describe('ManualSession asynchronous ownership and write ordering', () => {
  beforeEach(() => { vi.restoreAllMocks(); vi.useFakeTimers(); });
  afterEach(() => { vi.clearAllTimers(); vi.useRealTimers(); });

  it('does not revive a destroyed pending acquisition and queues cleanup before a new node', async () => {
    const pending = deferred<any>();
    const calls: string[] = [];
    vi.spyOn(transport, 'apiJson').mockImplementation(async (ip, _path, options) => {
      calls.push(`${options?.method}:${ip}`);
      if (calls.length === 1) return pending.promise;
      return receipt(options);
    });
    let ip = 'node-a'; let epoch = 0;
    const session = new ManualSession(undefined, target => {
      const generation = epoch;
      return { ip: target, isCurrent: () => target === ip && epoch === generation };
    });
    const acquireA = session.acquire(ip, makeChannels(), [0]); await settle();
    ip = 'node-b'; ++epoch; session.destroy('node-a');
    const acquireB = session.acquire(ip, makeChannels(), [8]);
    await settle();
    expect(calls).toEqual(['PUT:node-a']); // Wait for the sent PUT before DELETE/new acquisition.
    pending.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 });
    expect(await acquireA).toBe(false); expect(await acquireB).toBe(true);
    expect(calls).toEqual(['PUT:node-a', 'DELETE:node-a', 'PUT:node-b']);
    expect(session.controlledIndices).toEqual([8]);
    session.destroy(ip); await settle();
  });

  it('skips old-session cleanup instead of sending DELETE with replacement credentials', async () => {
    let auth: any = { sessionToken: 'old' };
    vi.spyOn(nodeAuthStore, 'getSession').mockImplementation(() => auth);
    const pending = deferred<any>();
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => options?.method === 'PUT' ? pending.promise : { released: true });
    const session = new ManualSession();
    const acquisition = session.acquire('node-a', makeChannels(), [0]); await settle();
    auth = { sessionToken: 'new' }; session.destroy('node-a');
    pending.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 }); await acquisition; await settle();
    expect(session.state).toBe('idle');
    expect(api.mock.calls.filter(call => call[2]?.method === 'DELETE')).toHaveLength(0);
    await vi.advanceTimersByTimeAsync(6000);
    expect(api).toHaveBeenCalledTimes(1);
  });

  it('ignores a late heartbeat failure after cancellation and re-acquisition at the same address', async () => {
    const heartbeat = deferred<any>(); let puts = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT' && ++puts === 2) return heartbeat.promise;
      return receipt(options);
    });
    const session = new ManualSession();
    await session.acquire('node-a', makeChannels(), [0]);
    await vi.advanceTimersByTimeAsync(5000);
    session.destroy('node-a');
    const next = session.acquire('node-a', makeChannels(), [8]);
    heartbeat.reject(new Error('Old connection lost')); await next; await settle();
    expect(session.state).toBe('active');
    expect(session.controlledIndices).toEqual([8]);
    expect(api.mock.calls.filter(call => call[2]?.method === 'DELETE')).toHaveLength(1);
    session.destroy('node-a'); await settle();
  });

  it('coalesces newer edits and never overlaps them with a heartbeat', async () => {
    const firstEdit = deferred<any>(); let puts = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT' && ++puts === 2) return firstEdit.promise;
      return receipt(options);
    });
    const session = new ManualSession(); const channels = makeChannels();
    await session.acquire('node-a', channels, [0]);
    channels[0].r = 1; const send = session.sendValues(channels); await settle();
    channels[0].r = 2; void session.sendValues(channels);
    channels[0].r = 3; void session.sendValues(channels);
    await vi.advanceTimersByTimeAsync(5000);
    expect(puts).toBe(2);
    firstEdit.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 }); await send;
    const values = api.mock.calls.filter(call => call[2]?.method === 'PUT').map(call => JSON.parse(String(call[2]?.body)).channels[0].r);
    expect(values).toEqual([255, 1, 3]);
    expect(session.snapshot().acceptedValues[0]).toMatchObject({ r: 3 });
    expect(session.snapshot().sending).toBe(false);
    session.destroy('node-a'); await settle();
  });

  it('renews only accepted values while newer browser draft values remain unsent', async () => {
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => receipt(options));
    const channels = makeChannels(); const session = new ManualSession();
    await session.acquire('node-a', channels, [0]);
    channels[0].r = 9; // A draft edit made outside the active-send workflow.
    await vi.advanceTimersByTimeAsync(5000);
    expect(JSON.parse(String(api.mock.calls[1][2]?.body)).channels[0].r).toBe(255);
    session.destroy('node-a'); await settle();
  });

  it('keeps target changes busy until their own queued replacement is acknowledged', async () => {
    const edit = deferred<any>(); const targets = deferred<any>(); let puts = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT') { ++puts; if (puts === 2) return edit.promise; if (puts === 3) return targets.promise; }
      return receipt(options);
    });
    const session = new ManualSession(); const channels = makeChannels();
    await session.acquire('node-a', channels, [0]);
    channels[0].r = 10; const sending = session.sendValues(channels); await settle();
    const updating = session.updateTargets('node-a', channels, [8]);
    edit.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 }); await sending; await settle();
    expect(session.state).toBe('updating');
    expect(session.snapshot().sending).toBe(true);
    expect(JSON.parse(String(api.mock.calls[2][2]?.body)).channels.map((ch: any) => ch.index)).toEqual([8]);
    targets.resolve({ accepted: true, channelMask: 256, leaseMs: 15000 }); await updating;
    expect(session.state).toBe('active'); expect(session.controlledIndices).toEqual([8]);
    session.destroy('node-a'); await settle();
  });

  it('cancels in-flight edits before release, rejects an invalid release receipt, and supports retry', async () => {
    const edit = deferred<any>(); let puts = 0; let deletes = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT' && ++puts === 2) return edit.promise;
      if (options?.method === 'DELETE' && ++deletes === 1) return { released: false };
      return receipt(options);
    });
    const session = new ManualSession(); const channels = makeChannels();
    await session.acquire('node-a', channels, [0]);
    channels[0].r = 3; const sending = session.sendValues(channels); await settle();
    channels[0].r = 4; void session.sendValues(channels);
    const releasing = session.release('node-a');
    edit.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 }); await sending;
    expect(await releasing).toBe(false);
    expect(session.state).toBe('error'); expect(session.snapshot().releaseUnconfirmed).toBe(true);
    expect(session.errorMessage).toMatch(/Release not confirmed/);
    expect(puts).toBe(2);
    expect(await session.release('node-a')).toBe(true);
    expect(session.state).toBe('idle'); expect(session.snapshot().releaseUnconfirmed).toBe(false);
    expect(api.mock.calls.map(call => call[2]?.method)).toEqual(['PUT', 'PUT', 'DELETE', 'DELETE']);
  });

  it('uses the returned lease duration and validates every renewal mask', async () => {
    let puts = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT' && ++puts > 1) return { accepted: true, channelMask: 256, leaseMs: 6000 };
      return receipt(options, 6000);
    });
    const session = new ManualSession(); await session.acquire('node-a', makeChannels(), [0]);
    expect(session.leaseRemainingSec).toBe(6);
    await vi.advanceTimersByTimeAsync(1999); expect(puts).toBe(1);
    await vi.advanceTimersByTimeAsync(1); expect(puts).toBe(2);
    expect(session.state).toBe('error'); expect(session.errorMessage).toMatch(/mask mismatch/);
    expect(api.mock.calls.filter(call => call[2]?.method === 'DELETE')).toHaveLength(1);
  });

  it('marks only matching telemetry as lease confirmation and stops when another mask appears', async () => {
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => receipt(options));
    const session = new ManualSession(); await session.acquire('node-a', makeChannels(), [0]);
    expect(session.snapshot().confirmed).toBe(false);
    session.onTelemetry({ active: true, channelMask: 1, leaseRemainingMs: 12000 });
    expect(session.snapshot().confirmed).toBe(true);
    session.onTelemetry({ active: true, channelMask: 256, leaseRemainingMs: 12000 });
    expect(session.state).toBe('lost'); expect(session.errorMessage).toMatch(/targets changed/);
    await vi.advanceTimersByTimeAsync(10000);
    expect(api).toHaveBeenCalledTimes(1); // Do not DELETE or renew a different operator's frame.
  });

  it('fails closed on a denied live edit instead of silently continuing heartbeat', async () => {
    let puts = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT' && ++puts === 2) return { accepted: false, channelMask: 1, leaseMs: 15000 };
      return receipt(options);
    });
    const session = new ManualSession(); const channels = makeChannels(); await session.acquire('node-a', channels, [0]);
    channels[0].r = 1; expect(await session.sendValues(channels)).toBe(false);
    expect(session.state).toBe('error'); expect(session.errorMessage).toMatch(/rejected/);
    await vi.advanceTimersByTimeAsync(10000);
    expect(puts).toBe(2); expect(api.mock.calls.filter(call => call[2]?.method === 'DELETE')).toHaveLength(1);
  });

  it.each(['negative receipt', 'HTTP failure'])('exposes failed cancellation cleanup (%s) and permits an owned retry', async failure => {
    const pending = deferred<any>(); let deletes = 0;
    const api = vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'PUT') return pending.promise;
      if (++deletes === 1) {
        if (failure === 'HTTP failure') throw new ApiError(403, '/api/v1/manual', 'node-a', 'Denied');
        return { released: false };
      }
      return { released: true };
    });
    const session = new ManualSession();
    const acquisition = session.acquire('node-a', makeChannels(), [0]); await settle();
    session.cancel('Manual request canceled.');
    expect(session.errorMessage).toMatch(/Release confirmation pending/);
    expect(session.snapshot().releaseUnconfirmed).toBe(false);
    pending.resolve({ accepted: true, channelMask: 1, leaseMs: 15000 });
    expect(await acquisition).toBe(false); await settle();
    expect(session.state).toBe('lost');
    expect(session.errorMessage).toMatch(/Release not confirmed/);
    expect(session.snapshot().releaseUnconfirmed).toBe(true);
    await vi.advanceTimersByTimeAsync(10000);
    expect(api.mock.calls.filter(call => call[2]?.method === 'PUT')).toHaveLength(1);
    expect(await session.release('node-a')).toBe(true);
    expect(session.state).toBe('idle');
    expect(session.snapshot().releaseUnconfirmed).toBe(false);
  });

  it('does not publish an old cleanup failure into a newer acquisition', async () => {
    const cleanup = deferred<any>(); let deletes = 0;
    vi.spyOn(transport, 'apiJson').mockImplementation(async (_ip, _path, options) => {
      if (options?.method === 'DELETE' && ++deletes === 1) return cleanup.promise;
      return receipt(options);
    });
    const session = new ManualSession(); await session.acquire('node-a', makeChannels(), [0]);
    session.cancel('Manual request canceled.'); await settle();
    const acquisition = session.acquire('node-a', makeChannels(), [8]);
    cleanup.resolve({ released: false }); await acquisition; await settle();
    expect(session.state).toBe('active');
    expect(session.controlledIndices).toEqual([8]);
    expect(session.errorMessage).toBeNull();
    expect(session.snapshot().releaseUnconfirmed).toBe(false);
    session.destroy('node-a'); await settle();
  });
});
